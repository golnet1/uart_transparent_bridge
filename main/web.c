#include "web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

#include "app.h"
#include "auth.h"
#include "config.h"
#include "event_bus.h"
#include "uart_bridge.h"
#include "wifi.h"
#include "ota.h"
#include "mqtt_channels.h"
#include "time_sync.h"

/* HTTPD держит 3 сокета внутри, CONFIG_LWIP_MAX_SOCKETS=16 → до 13 клиентских. */
#define WS_MAX_CLIENTS 8

extern const char *web_ui_html(void);

#define AUTH_COOKIE "PTUART"

static const char *TAG = "web";
static httpd_handle_t s_httpd;

typedef struct {
    int fd;
    bool used;
} ws_client_t;

static ws_client_t s_ws[WS_MAX_CLIENTS];
static SemaphoreHandle_t s_ws_mu;
static QueueHandle_t s_evt_q;

/* ---------- утилиты ---------- */

static char *read_body(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 8192) return NULL;
    char *buf = malloc(total + 1);
    if (!buf) return NULL;
    int off = 0;
    while (off < total) {
        int r = httpd_req_recv(req, buf + off, total - off);
        if (r <= 0) { free(buf); return NULL; }
        off += r;
    }
    buf[off] = '\0';
    return buf;
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, s ? s : "{}", s ? strlen(s) : 2);
    if (s) cJSON_free(s);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t send_ok(httpd_req_t *req, bool ok, const char *msg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", ok);
    cJSON_AddStringToObject(root, "msg", msg ? msg : "");
    return send_json(req, root);
}

static int target_from_json(cJSON *root, int fallback)
{
    cJSON *t = cJSON_GetObjectItem(root, "target");
    if (!t) return fallback;
    if (cJSON_IsNumber(t)) {
        int v = t->valueint;
        return (v >= 0 && v <= 3) ? v : fallback;
    }
    if (cJSON_IsString(t)) {
        const char *s = t->valuestring;
        if (!strcmp(s, "1") || !strcasecmp(s, "uart1")) return 1;
        if (!strcmp(s, "2") || !strcasecmp(s, "uart2")) return 2;
        if (!strcmp(s, "3") || !strcasecmp(s, "both") || !strcasecmp(s, "all")) return 3;
        if (!strcmp(s, "0") || !strcasecmp(s, "usb")) return 0;
    }
    return fallback;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t parse_hex(const char *src, uint8_t *dst, size_t cap)
{
    /* Парсим без копирования: любые разделители, "41540D" и нечётная цифра = 0x0N.
       Большой локальный буфер здесь нельзя держать — стек HTTPD-задачи ограничен. */
    size_t n = 0;
    int hi = -1;
    if (!src) return 0;
    for (const char *p = src; *p && n < cap; p++) {
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) { p++; continue; }
        int v = hexval(*p);
        if (v < 0) continue;                 /* пробелы, запятые, \r\n и т.п. */
        if (hi < 0) hi = v;
        else { dst[n++] = (uint8_t)((hi << 4) | v); hi = -1; }
    }
    if (hi >= 0 && n < cap) dst[n++] = (uint8_t)hi;
    return n;
}

static const char *token_from_req(httpd_req_t *req, char *buf, size_t cap)
{
    char cookie[160];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK)
        return NULL;
    const char *p = strstr(cookie, AUTH_COOKIE "=");
    if (!p) return NULL;
    p += sizeof(AUTH_COOKIE);   /* перепрыгиваем "PTUART=" */
    size_t n = 0;
    while (*p && *p != ';' && *p != ' ' && n + 1 < cap) buf[n++] = *p++;
    buf[n] = '\0';
    return n ? buf : NULL;
}

static bool authorized(httpd_req_t *req)
{
    char token[AUTH_TOKEN_LEN];
    const char *t = token_from_req(req, token, sizeof(token));
    return t && auth_token_valid(t);
}

static esp_err_t need_auth(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    return send_ok(req, false, "Unauthorized");
}

static void set_auth_cookie(httpd_req_t *req, const char *token)
{
    /* HTTPD не копирует значение заголовка — оно должно жить после выхода
       из функции (до фактической отправки ответа). HTTPD обрабатывает
       запросы последовательно в одном задании, поэтому static безопасен. */
    static char h[160];
    snprintf(h, sizeof(h), AUTH_COOKIE "=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=86400", token);
    httpd_resp_set_hdr(req, "Set-Cookie", h);
}

/* ---------- HTTP handlers ---------- */

/* Секретный путь в URL и есть пароль. Например /sec — открывает панель.
   Без пути — отдельное сообщение, с неверным путём — своё. */
static esp_err_t secret_get(httpd_req_t *req)
{
    const char *path = req->uri;
    /* req->uri содержит и query-строку: пароль — только путь до '?' */
    char seg[64] = "";
    if (path && path[0] == '/') {
        const char *q = strchr(path, '?');
        size_t n = q ? (size_t)(q - path - 1) : strlen(path) - 1;
        if (n > 0 && n < sizeof(seg)) { memcpy(seg, path + 1, n); seg[n] = '\0'; }
    }
    if (seg[0] && auth_check_password(seg)) {
        char token[AUTH_TOKEN_LEN];
        if (auth_login(seg, token, sizeof(token)))
            set_auth_cookie(req, token);

        /* Каналы «через адресную строку»:
           /sec?hex=41%2054%200D&target=3   — отправить HEX
           /sec?log=50                      — прочитать журнал
           /sec                             — сама панель */
        char query[256];
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            char val[192];
            if (httpd_query_key_value(query, "hex", val, sizeof(val)) == ESP_OK) {
                httpd_resp_set_type(req, "text/plain; charset=utf-8");
                if (config_get()->cmd_mode == 1) {   /* выбран только MQTT */
                    httpd_resp_set_status(req, "403 Forbidden");
                    return httpd_resp_sendstr(req, "ERROR: URL channel disabled (MQTT mode)");
                }
                uint8_t bytes[512];
                size_t n = parse_hex(val, bytes, sizeof(bytes));
                int target = config_get()->cmd_target;
                char tv[8];
                if (httpd_query_key_value(query, "target", tv, sizeof(tv)) == ESP_OK) {
                    int t = atoi(tv);
                    if (t >= 0 && t <= 3) target = t;
                }
                char out[96];
                httpd_resp_set_type(req, "text/plain; charset=utf-8");
                if (n) {
                    uart_bridge_send(target, bytes, n);
                    snprintf(out, sizeof(out), "OK bytes=%u target=%d", (unsigned)n, target);
                } else {
                    httpd_resp_set_status(req, "400 Bad Request");
                    snprintf(out, sizeof(out), "ERROR: empty/invalid HEX");
                }
                return httpd_resp_send(req, out, strlen(out));
            }
            if (httpd_query_key_value(query, "log", val, sizeof(val)) == ESP_OK) {
                int k = atoi(val);
                if (k < 1 || k > 500) k = 50;
                char *txt = event_bus_log_get(k);
                httpd_resp_set_type(req, "text/plain; charset=utf-8");
                esp_err_t e = httpd_resp_send(req, txt ? txt : "", txt ? strlen(txt) : 0);
                free(txt);
                return e;
            }
        }

        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        const char *html = web_ui_html();
        return httpd_resp_send(req, html, strlen(html));
    }
    const char *msg = !seg[0] ? "Password is not specified" : "Wrong password";
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, msg, strlen(msg));
}

static esp_err_t passwd_post(httpd_req_t *req)
{
    if (!authorized(req)) return need_auth(req);
    char *body = read_body(req);
    if (!body) return send_ok(req, false, "Password is not specified");
    cJSON *root = cJSON_Parse(body);
    free(body);
    cJSON *old = root ? cJSON_GetObjectItem(root, "old") : NULL;
    cJSON *nw  = root ? cJSON_GetObjectItem(root, "new") : NULL;
    if (!old || !cJSON_IsString(old) || !cJSON_IsString(nw) || !nw->valuestring[0]) {
        if (root) cJSON_Delete(root);
        return send_ok(req, false, "Password is not specified");
    }
    if (!auth_check_password(old->valuestring)) {
        cJSON_Delete(root);
        return send_ok(req, false, "Wrong password");
    }
    if (!auth_set_password(nw->valuestring)) {
        cJSON_Delete(root);
        return send_ok(req, false, "Invalid password: A-Z a-z 0-9 . _ - ~ (max 32)");
    }
    char token[AUTH_TOKEN_LEN];
    auth_login(nw->valuestring, token, sizeof(token));
    set_auth_cookie(req, token);
    char msg[128];
    snprintf(msg, sizeof(msg), "Password changed. New address: /%s", nw->valuestring);
    cJSON_Delete(root);
    return send_ok(req, true, msg);
}

static esp_err_t status_get(httpd_req_t *req)
{
    if (!authorized(req)) return need_auth(req);
    const app_config_t *c = config_get();
    uint32_t r1, r2, t1, t2;
    uart_bridge_counters(&r1, &r2, &t1, &t2);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", APP_VERSION);
    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    char ubuf[48];
    time_format_uptime(ubuf, sizeof(ubuf), uptime_s);
    cJSON_AddNumberToObject(root, "uptime", (double)uptime_s);
    cJSON_AddStringToObject(root, "uptime_text", ubuf);
    cJSON_AddNumberToObject(root, "baud1", c->baud1);
    cJSON_AddNumberToObject(root, "baud2", c->baud2);
    cJSON_AddBoolToObject(root, "bridge", c->bridge);
    cJSON_AddBoolToObject(root, "echo", c->echo);
    cJSON_AddBoolToObject(root, "mirror", c->mirror);
    cJSON_AddBoolToObject(root, "syslog", c->syslog);
    cJSON_AddBoolToObject(root, "auto_baud1", c->auto_baud1);
    cJSON_AddBoolToObject(root, "auto_baud2", c->auto_baud2);
    cJSON_AddStringToObject(root, "baud_probe", c->baud_probe);
    cJSON_AddNumberToObject(root, "baud1_detected", uart_bridge_detected_baud(1));
    cJSON_AddNumberToObject(root, "baud2_detected", uart_bridge_detected_baud(2));
    cJSON_AddNumberToObject(root, "usb_target", c->usb_target);
    cJSON_AddNumberToObject(root, "auto_ms", c->auto_ms);
    cJSON_AddStringToObject(root, "auto_target", c->auto_target);
    cJSON_AddStringToObject(root, "auto_text", c->auto_text);
    cJSON_AddNumberToObject(root, "log_lines", event_bus_log_lines());
    cJSON_AddBoolToObject(root, "auto_update", c->auto_update);
    cJSON_AddBoolToObject(root, "ota_busy", ota_busy());
    cJSON_AddStringToObject(root, "ota_last", ota_last_result());
    cJSON_AddStringToObject(root, "ota_remote", ota_remote_version());
    cJSON_AddBoolToObject(root, "ota_available", ota_update_available());
    cJSON_AddStringToObject(root, "cmd_mode_name",
                            c->cmd_mode == 0 ? "URL" : (c->cmd_mode == 1 ? "MQTT" : "URL+MQTT"));

    char mqtt_uri[192];
    mqtt_normalize_uri(c->mqtt_uri, mqtt_uri, sizeof(mqtt_uri));
    cJSON *mqtt = cJSON_CreateObject();
    cJSON_AddNumberToObject(mqtt, "mode", c->cmd_mode);
    cJSON_AddNumberToObject(mqtt, "target", c->cmd_target);
    cJSON_AddNumberToObject(mqtt, "target_effective", mqtt_target());
    cJSON_AddStringToObject(mqtt, "uri", c->mqtt_uri);
    cJSON_AddStringToObject(mqtt, "uri_effective", mqtt_uri);
    cJSON_AddStringToObject(mqtt, "topic", c->mqtt_topic);
    cJSON_AddStringToObject(mqtt, "user", c->mqtt_user);
    cJSON_AddBoolToObject(mqtt, "connected", mqtt_connected());
    cJSON_AddItemToObject(root, "mqtt", mqtt);

    char tbuf[40];
    time_sync_format(tbuf, sizeof(tbuf));
    cJSON *clk = cJSON_CreateObject();
    cJSON_AddBoolToObject(clk, "synced", time_sync_synced());
    cJSON_AddStringToObject(clk, "server", c->ntp_server);
    cJSON_AddStringToObject(clk, "tz", c->ntp_tz);
    cJSON_AddStringToObject(clk, "str", tbuf);
    cJSON_AddNumberToObject(clk, "epoch", (double)time_sync_epoch());
    cJSON_AddNumberToObject(clk, "offset", time_sync_offset_min());
    cJSON_AddItemToObject(root, "time", clk);

    cJSON *cnt = cJSON_CreateObject();
    cJSON_AddNumberToObject(cnt, "rx1", r1);
    cJSON_AddNumberToObject(cnt, "rx2", r2);
    cJSON_AddNumberToObject(cnt, "tx1", t1);
    cJSON_AddNumberToObject(cnt, "tx2", t2);
    cJSON_AddItemToObject(root, "counters", cnt);

    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi, "ap_ip", wifi_ip_ap());
    cJSON_AddStringToObject(wifi, "sta_ip", wifi_ip_sta());
    cJSON_AddBoolToObject(wifi, "connected", wifi_sta_connected());
    cJSON_AddBoolToObject(wifi, "ap_enabled", wifi_ap_enabled());
    cJSON_AddNumberToObject(wifi, "rssi", wifi_rssi());
    cJSON_AddStringToObject(wifi, "ssid", c->wifi_ssid);
    cJSON_AddItemToObject(root, "wifi", wifi);

    return send_json(req, root);
}

#define CFG_EXPORT_FORMAT "pass-through-uart-config"

typedef struct {
    bool baud_changed;
    bool wifi_changed;
    bool mqtt_changed;
    bool time_changed;
    bool auth_changed;
} cfg_apply_flags_t;

/* Общий разбор JSON настроек: используется и веб-формой, и импортом файла.
   cfg — объект с полями конфигурации, root — весь документ (там же auth_hash). */
static void config_apply_json(cJSON *cfg, cJSON *root, bool import_mode,
                              cfg_apply_flags_t *f)
{
    app_config_t *c = config_mut();
    cJSON *j;

    if ((j = cJSON_GetObjectItem(cfg, "baud1")) && cJSON_IsNumber(j)) {
        int v = j->valueint;
        if (v >= 1200 && v <= 3000000 && v != c->baud1) { c->baud1 = v; f->baud_changed = true; }
    }
    if ((j = cJSON_GetObjectItem(cfg, "baud2")) && cJSON_IsNumber(j)) {
        int v = j->valueint;
        if (v >= 1200 && v <= 3000000 && v != c->baud2) { c->baud2 = v; f->baud_changed = true; }
    }
    if ((j = cJSON_GetObjectItem(cfg, "auto_baud1")) && cJSON_IsBool(j)) {
        bool v = cJSON_IsTrue(j);
        if (v != c->auto_baud1) { c->auto_baud1 = v; f->baud_changed = true; }
    }
    if ((j = cJSON_GetObjectItem(cfg, "auto_baud2")) && cJSON_IsBool(j)) {
        bool v = cJSON_IsTrue(j);
        if (v != c->auto_baud2) { c->auto_baud2 = v; f->baud_changed = true; }
    }
    if ((j = cJSON_GetObjectItem(cfg, "baud_probe")) && cJSON_IsString(j)) {
        if (strcmp(c->baud_probe, j->valuestring)) {
            snprintf(c->baud_probe, sizeof(c->baud_probe), "%s", j->valuestring);
            f->baud_changed = true;
        }
    }
    if ((j = cJSON_GetObjectItem(cfg, "bridge")) && cJSON_IsBool(j)) c->bridge = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(cfg, "echo")) && cJSON_IsBool(j)) c->echo = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(cfg, "mirror")) && cJSON_IsBool(j)) c->mirror = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(cfg, "syslog")) && cJSON_IsBool(j)) c->syslog = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(cfg, "usb_target")) && cJSON_IsNumber(j)) {
        int v = j->valueint;
        if (v >= 0 && v <= 3) c->usb_target = v;
    }
    if ((j = cJSON_GetObjectItem(cfg, "auto_ms")) && cJSON_IsNumber(j)) {
        int v = j->valueint;
        c->auto_ms = (v < 0 || v > 3600000) ? 0 : v;
    }
    if ((j = cJSON_GetObjectItem(cfg, "auto_target")) && cJSON_IsString(j))
        snprintf(c->auto_target, sizeof(c->auto_target), "%s", j->valuestring);
    if ((j = cJSON_GetObjectItem(cfg, "auto_text")) && cJSON_IsString(j))
        snprintf(c->auto_text, sizeof(c->auto_text), "%s", j->valuestring);
    if ((j = cJSON_GetObjectItem(cfg, "wifi_ssid")) && cJSON_IsString(j)) {
        if (strcmp(c->wifi_ssid, j->valuestring)) {
            snprintf(c->wifi_ssid, sizeof(c->wifi_ssid), "%s", j->valuestring);
            f->wifi_changed = true;
        }
    }
    if ((j = cJSON_GetObjectItem(cfg, "wifi_pass")) && cJSON_IsString(j)) {
        if (strcmp(c->wifi_pass, j->valuestring)) {
            snprintf(c->wifi_pass, sizeof(c->wifi_pass), "%s", j->valuestring);
            f->wifi_changed = true;
        }
    }
    if ((j = cJSON_GetObjectItem(cfg, "auto_update")) && cJSON_IsBool(j))
        c->auto_update = cJSON_IsTrue(j);

    /* Каналы команд и время */
    if ((j = cJSON_GetObjectItem(cfg, "cmd_mode")) && cJSON_IsNumber(j)) {
        int v = j->valueint;
        if (v >= 0 && v <= 2 && v != c->cmd_mode) { c->cmd_mode = v; f->mqtt_changed = true; }
    }
    if ((j = cJSON_GetObjectItem(cfg, "cmd_target")) && cJSON_IsNumber(j)) {
        int v = j->valueint;
        if (v >= 1 && v <= 3 && v != c->cmd_target) { c->cmd_target = v; f->mqtt_changed = true; }
    }
    if ((j = cJSON_GetObjectItem(cfg, "mqtt_uri")) && cJSON_IsString(j)) {
        if (strcmp(c->mqtt_uri, j->valuestring)) {
            snprintf(c->mqtt_uri, sizeof(c->mqtt_uri), "%s", j->valuestring);
            f->mqtt_changed = true;
        }
    }
    if ((j = cJSON_GetObjectItem(cfg, "mqtt_topic")) && cJSON_IsString(j) && j->valuestring[0]) {
        if (strcmp(c->mqtt_topic, j->valuestring)) {
            snprintf(c->mqtt_topic, sizeof(c->mqtt_topic), "%s", j->valuestring);
            f->mqtt_changed = true;
        }
    }
    if ((j = cJSON_GetObjectItem(cfg, "mqtt_user")) && cJSON_IsString(j)) {
        if (strcmp(c->mqtt_user, j->valuestring)) {
            snprintf(c->mqtt_user, sizeof(c->mqtt_user), "%s", j->valuestring);
            f->mqtt_changed = true;
        }
    }
    /* В веб-форме пустой пароль = «оставить прежний»; при импорте = очистить. */
    if ((j = cJSON_GetObjectItem(cfg, "mqtt_pass")) && cJSON_IsString(j) &&
        (import_mode || j->valuestring[0])) {
        if (strcmp(c->mqtt_pass, j->valuestring)) {
            snprintf(c->mqtt_pass, sizeof(c->mqtt_pass), "%s", j->valuestring);
            f->mqtt_changed = true;
        }
    }
    if ((j = cJSON_GetObjectItem(cfg, "ntp_server")) && cJSON_IsString(j) && j->valuestring[0]) {
        if (strcmp(c->ntp_server, j->valuestring)) {
            snprintf(c->ntp_server, sizeof(c->ntp_server), "%s", j->valuestring);
            f->time_changed = true;
        }
    }
    if ((j = cJSON_GetObjectItem(cfg, "ntp_tz")) && cJSON_IsString(j) && j->valuestring[0]) {
        if (strcmp(c->ntp_tz, j->valuestring)) {
            snprintf(c->ntp_tz, sizeof(c->ntp_tz), "%s", j->valuestring);
            f->time_changed = true;
        }
    }

    /* Хеш веб-пароля есть только в файле резервной копии. */
    if (import_mode && root) {
        j = cJSON_GetObjectItem(root, "auth_hash");
        if (cJSON_IsString(j) && auth_restore_hash_hex(j->valuestring))
            f->auth_changed = true;
    }
}

static void config_finish(const cfg_apply_flags_t *f)
{
    config_save();
    if (f->baud_changed) uart_bridge_apply_config();
    if (f->wifi_changed) wifi_reconnect();
    if (f->mqtt_changed) mqtt_apply_config();
    if (f->time_changed) time_sync_apply();
}

static esp_err_t config_post(httpd_req_t *req)
{
    if (!authorized(req)) return need_auth(req);
    char *body = read_body(req);
    if (!body) return send_ok(req, false, "нет тела запроса");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return send_ok(req, false, "неверный JSON");

    cfg_apply_flags_t f = {0};
    config_apply_json(root, root, false, &f);
    config_finish(&f);
    cJSON_Delete(root);

    event_bus_publish_str(EV_SYS, "настройки сохранены");
    return send_ok(req, true, "настройки сохранены");
}

/* GET /api/config/export — файл со всеми настройками (включая хеш веб-пароля). */
static esp_err_t config_export_get(httpd_req_t *req)
{
    if (!authorized(req)) return need_auth(req);
    const app_config_t *c = config_get();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "format", CFG_EXPORT_FORMAT);
    cJSON_AddNumberToObject(root, "config_format", 1);
    cJSON_AddStringToObject(root, "app_version", APP_VERSION);
    char stamp[48];
    time_sync_format(stamp, sizeof(stamp));
    cJSON_AddStringToObject(root, "saved_at", stamp);
    char hash[65];
    auth_export_hash_hex(hash, sizeof(hash));
    cJSON_AddStringToObject(root, "auth_hash", hash);

    cJSON *cfg = cJSON_CreateObject();
    cJSON_AddNumberToObject(cfg, "baud1", c->baud1);
    cJSON_AddNumberToObject(cfg, "baud2", c->baud2);
    cJSON_AddBoolToObject(cfg, "bridge", c->bridge);
    cJSON_AddBoolToObject(cfg, "echo", c->echo);
    cJSON_AddBoolToObject(cfg, "mirror", c->mirror);
    cJSON_AddBoolToObject(cfg, "syslog", c->syslog);
    cJSON_AddNumberToObject(cfg, "usb_target", c->usb_target);
    cJSON_AddNumberToObject(cfg, "auto_ms", c->auto_ms);
    cJSON_AddStringToObject(cfg, "auto_target", c->auto_target);
    cJSON_AddStringToObject(cfg, "auto_text", c->auto_text);
    cJSON_AddStringToObject(cfg, "wifi_ssid", c->wifi_ssid);
    cJSON_AddStringToObject(cfg, "wifi_pass", c->wifi_pass);
    cJSON_AddBoolToObject(cfg, "auto_update", c->auto_update);
    cJSON_AddNumberToObject(cfg, "cmd_mode", c->cmd_mode);
    cJSON_AddNumberToObject(cfg, "cmd_target", c->cmd_target);
    cJSON_AddStringToObject(cfg, "mqtt_uri", c->mqtt_uri);
    cJSON_AddStringToObject(cfg, "mqtt_topic", c->mqtt_topic);
    cJSON_AddStringToObject(cfg, "mqtt_user", c->mqtt_user);
    cJSON_AddStringToObject(cfg, "mqtt_pass", c->mqtt_pass);
    cJSON_AddStringToObject(cfg, "ntp_server", c->ntp_server);
    cJSON_AddStringToObject(cfg, "ntp_tz", c->ntp_tz);
    cJSON_AddBoolToObject(cfg, "auto_baud1", c->auto_baud1);
    cJSON_AddBoolToObject(cfg, "auto_baud2", c->auto_baud2);
    cJSON_AddStringToObject(cfg, "baud_probe", c->baud_probe);
    cJSON_AddItemToObject(root, "config", cfg);

    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) return send_ok(req, false, "не удалось сформировать файл");

    char disp[128];
    snprintf(disp, sizeof(disp), "attachment; filename=\"pass-through-uart-%s.json\"", APP_VERSION);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    httpd_resp_send(req, text, strlen(text));
    cJSON_free(text);
    return ESP_OK;
}

/* POST /api/config/import — восстановление из файла, выгруженного экспортом. */
static esp_err_t config_import_post(httpd_req_t *req)
{
    if (!authorized(req)) return need_auth(req);
    char *body = read_body(req);
    if (!body) return send_ok(req, false, "нет тела запроса");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return send_ok(req, false, "неверный JSON");

    cJSON *j = cJSON_GetObjectItem(root, "format");
    if (cJSON_IsString(j) && strcmp(j->valuestring, CFG_EXPORT_FORMAT)) {
        cJSON_Delete(root);
        return send_ok(req, false, "неверный формат файла настроек");
    }
    j = cJSON_GetObjectItem(root, "auth_hash");
    if (cJSON_IsString(j) && j->valuestring[0] && !auth_hash_hex_valid(j->valuestring)) {
        cJSON_Delete(root);
        return send_ok(req, false, "неверный хеш пароля в файле");
    }
    cJSON *cfg = cJSON_GetObjectItem(root, "config");
    if (!cJSON_IsObject(cfg)) cfg = root;   /* совместимость с плоским JSON */
    if (!cJSON_GetObjectItem(cfg, "baud1") && !cJSON_GetObjectItem(cfg, "wifi_ssid")) {
        cJSON_Delete(root);
        return send_ok(req, false, "в файле нет настроек");
    }

    cfg_apply_flags_t f = {0};
    config_apply_json(cfg, root, true, &f);
    config_finish(&f);
    cJSON_Delete(root);

    if (f.auth_changed) {
        /* Хеш пароля восстановлен — продлеваем сессию новым токеном. */
        char cookie[160];
        snprintf(cookie, sizeof(cookie),
                 AUTH_COOKIE "=%s; Path=/; Max-Age=86400; SameSite=Lax",
                 auth_current_token());
        httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    }
    event_bus_publish_str(EV_SYS, "настройки восстановлены из файла");
    return send_ok(req, true, "настройки восстановлены из файла");
}

static esp_err_t send_post(httpd_req_t *req)
{
    if (!authorized(req)) return need_auth(req);
    char *body = read_body(req);
    if (!body) return send_ok(req, false, "нет тела запроса");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return send_ok(req, false, "неверный JSON");

    /* Цель не указана -> оба UART. Явно можно задать 0/1/2/3 или
       "usb"/"uart1"/"uart2"/"both" ("all"). */
    int t = target_from_json(root, 3);
    cJSON *jdata = cJSON_GetObjectItem(root, "data");
    cJSON *jfmt = cJSON_GetObjectItem(root, "format");
    const char *fmt = (jfmt && cJSON_IsString(jfmt)) ? jfmt->valuestring : "text";
    if (!jdata || !cJSON_IsString(jdata)) {
        cJSON_Delete(root);
        return send_ok(req, false, "нет данных");
    }

    char line[2048];
    size_t n = 0;
    if (!strcasecmp(fmt, "hex")) {
        uint8_t bytes[1024];
        n = parse_hex(jdata->valuestring, bytes, sizeof(bytes));
        uart_bridge_send(t, bytes, n);
    } else {
        snprintf(line, sizeof(line), "%s", jdata->valuestring);
        n = strlen(line);
        if (n == 0 || line[n - 1] != '\n') {
            if (n + 2 < sizeof(line)) { strcat(line, "\r\n"); n += 2; }
        }
        uart_bridge_send(t, (const uint8_t *)line, n);
    }
    cJSON_Delete(root);

    const char *tname = t == 3 ? "оба UART" :
                        (t == 2 ? "UART2" : (t == 1 ? "UART1" : "USB"));
    char msg[96];
    snprintf(msg, sizeof(msg), "отправлено %u байт, цель %s", (unsigned)n, tname);
    return send_ok(req, true, msg);
}

static esp_err_t log_get(httpd_req_t *req)
{
    if (!authorized(req)) return need_auth(req);
    int n = 200;
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(query, "n", val, sizeof(val)) == ESP_OK) n = atoi(val);
    }
    char *txt = event_bus_log_get(n);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (txt && *txt) {
        char *p = txt;
        while (*p) {
            char *e = strchr(p, '\n');
            if (e) *e = '\0';
            cJSON_AddItemToArray(arr, cJSON_CreateString(p));
            if (!e) break;
            p = e + 1;
        }
    }
    cJSON_AddItemToObject(root, "lines", arr);
    free(txt);
    return send_json(req, root);
}

static esp_err_t log_clear_post(httpd_req_t *req)
{
    if (!authorized(req)) return need_auth(req);
    event_bus_log_clear();
    return send_ok(req, true, "журнал очищен");
}

static esp_err_t wifi_scan_get(httpd_req_t *req)
{
    if (!authorized(req)) return need_auth(req);
    wifi_ap_t aps[16];
    int n = wifi_scan(aps, 16);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", aps[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", aps[i].rssi);
        cJSON_AddNumberToObject(o, "channel", aps[i].channel);
        cJSON_AddBoolToObject(o, "auth", aps[i].auth);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddItemToObject(root, "aps", arr);
    return send_json(req, root);
}

/* POST /api/ota — обновление с GitHub, URL зашит в прошивке. */
static esp_err_t ota_post(httpd_req_t *req)
{
    if (!authorized(req)) return need_auth(req);
    ota_start_remote_update();
    return send_ok(req, true, "OTA запущена");
}

/* POST /api/ota/check — разовая проверка версии на GitHub. */
static esp_err_t ota_check_post(httpd_req_t *req)
{
    if (!authorized(req)) return need_auth(req);
    ota_check_now();
    return send_ok(req, true, "проверка запущена");
}

/* POST /api/ota/file — прошивка локальным .bin, тело запроса — файл целиком. */
static esp_err_t ota_file_post(httpd_req_t *req)
{
    if (!authorized(req)) return need_auth(req);
    int total = req->content_len;
    if (total <= 0) return send_ok(req, false, "файл прошивки пуст");

    esp_err_t err = ota_stream_begin();
    if (err != ESP_OK) {
        char msg[96];
        snprintf(msg, sizeof(msg), "OTA: %s", esp_err_to_name(err));
        return send_ok(req, false, msg);
    }

    uint8_t buf[4096];
    int off = 0;
    while (off < total) {
        int chunk = total - off;
        if (chunk > (int)sizeof(buf)) chunk = sizeof(buf);
        int r = httpd_req_recv(req, (char *)buf, chunk);
        if (r <= 0) {
            ota_stream_abort();
            return send_ok(req, false, "ошибка приёма файла");
        }
        err = ota_stream_write(buf, (size_t)r);
        if (err != ESP_OK) {
            ota_stream_abort();
            char msg[96];
            snprintf(msg, sizeof(msg), "OTA: %s", esp_err_to_name(err));
            return send_ok(req, false, msg);
        }
        off += r;
    }

    err = ota_stream_finish();
    if (err != ESP_OK) {
        char msg[96];
        snprintf(msg, sizeof(msg), "OTA: %s", esp_err_to_name(err));
        return send_ok(req, false, msg);
    }
    return send_ok(req, true, "OTA запущена");
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static esp_err_t reboot_post(httpd_req_t *req)
{
    if (!authorized(req)) return need_auth(req);
    esp_err_t e = send_ok(req, true, "перезагрузка");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return e;
}

/* ---------- WebSocket ---------- */

static void ws_add(int fd)
{
    xSemaphoreTake(s_ws_mu, portMAX_DELAY);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (!s_ws[i].used) { s_ws[i].used = true; s_ws[i].fd = fd; break; }
    }
    xSemaphoreGive(s_ws_mu);
}

static void ws_remove(int fd)
{
    xSemaphoreTake(s_ws_mu, portMAX_DELAY);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (s_ws[i].used && s_ws[i].fd == fd) { s_ws[i].used = false; s_ws[i].fd = -1; }
    }
    xSemaphoreGive(s_ws_mu);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (!authorized(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return httpd_resp_send(req, NULL, 0);
    }
    if (req->method == HTTP_GET) {
        ws_add(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t frame = { 0 };
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) return err;
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        ws_remove(httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    if (frame.len) {
        uint8_t *buf = calloc(1, frame.len + 1);
        if (buf) {
            frame.payload = buf;
            httpd_ws_recv_frame(req, &frame, frame.len);
            /* клиент может прислать текстовую команду — просто игнорируем */
            free(buf);
        }
    }
    return ESP_OK;
}

static void ws_events_task(void *arg)
{
    (void)arg;
    app_msg_t *msg;
    while (xQueueReceive(s_evt_q, &msg, portMAX_DELAY) == pdTRUE) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "type", msg->type);
        cJSON_AddNumberToObject(root, "ts", (double)msg->ts_ms);
        cJSON_AddStringToObject(root, "line", msg->line);
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        free(msg);

        if (json && s_httpd) {
            httpd_ws_frame_t frame = {
                .payload = (uint8_t *)json,
                .len = strlen(json),
                .type = HTTPD_WS_TYPE_TEXT,
                .final = true,
            };
            xSemaphoreTake(s_ws_mu, portMAX_DELAY);
            for (int i = 0; i < WS_MAX_CLIENTS; i++) {
                if (s_ws[i].used) {
                    httpd_ws_send_frame_async(s_httpd, s_ws[i].fd, &frame);
                }
            }
            xSemaphoreGive(s_ws_mu);
        }
        if (json) cJSON_free(json);
    }
}

/* ---------- инициализация ---------- */

void web_init(void)
{
    s_ws_mu = xSemaphoreCreateMutex();
    for (int i = 0; i < WS_MAX_CLIENTS; i++) s_ws[i].fd = -1;
    s_evt_q = xQueueCreate(128, sizeof(app_msg_t *));
    event_bus_subscribe(s_evt_q, SUB_ALL);
    xTaskCreate(ws_events_task, "ws_events", 8192, NULL, 5, NULL);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 16;
    cfg.max_open_sockets = 13;  /* 3 зарезервированы HTTPD, остаётся 10 под WS/API */
    cfg.uri_match_fn = httpd_uri_match_wildcard;  /* нужен для ловушки внешних путей */
    cfg.stack_size = 6144;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;

    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "не удалось запустить HTTP сервер");
        return;
    }

    httpd_uri_t uris[] = {
        { .uri = "/api/passwd",    .method = HTTP_POST, .handler = passwd_post },
        { .uri = "/api/status",    .method = HTTP_GET,  .handler = status_get },
        { .uri = "/api/config",         .method = HTTP_POST, .handler = config_post },
        { .uri = "/api/config/export",  .method = HTTP_GET,  .handler = config_export_get },
        { .uri = "/api/config/import",  .method = HTTP_POST, .handler = config_import_post },
        { .uri = "/api/send",      .method = HTTP_POST, .handler = send_post },
        { .uri = "/api/log",       .method = HTTP_GET,  .handler = log_get },
        { .uri = "/api/log/clear", .method = HTTP_POST, .handler = log_clear_post },
        { .uri = "/api/wifi/scan", .method = HTTP_GET,  .handler = wifi_scan_get },
        { .uri = "/api/ota",       .method = HTTP_POST, .handler = ota_post },
        { .uri = "/api/ota/check", .method = HTTP_POST, .handler = ota_check_post },
        { .uri = "/api/ota/file",  .method = HTTP_POST, .handler = ota_file_post },
        { .uri = "/api/reboot",    .method = HTTP_POST, .handler = reboot_post },
        { .uri = "/ws",            .method = HTTP_GET,  .handler = ws_handler,
          .is_websocket = true },
        /* Ловушка секретного пути — registering last: wildcard перехватывает
           всё, что не совпало с точными обработчиками выше. */
        { .uri = "/*",             .method = HTTP_GET,  .handler = secret_get },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_httpd, &uris[i]);
    }
    ESP_LOGI(TAG, "web server started on port 80");
}
