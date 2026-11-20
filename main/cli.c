#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"
#include "charset.h"
#include "auth.h"
#include "event_bus.h"
#include "uart_bridge.h"
#include "wifi.h"
#include "ota.h"
#include "mqtt_channels.h"
#include "time_sync.h"

#define USB_LINE_MAX 512

static cli_session_t s_usb_session;
static char s_usb_line[USB_LINE_MAX];
static size_t s_usb_len;

static void usb_out(void *ctx, const char *line)
{
    (void)ctx;
    uart_bridge_usb_print(line);
    uart_bridge_usb_print("\r\n");
}

void cli_session_init(cli_session_t *s, void *ctx,
                      void (*out)(void *ctx, const char *line))
{
    s->target = config_get()->usb_target;
    s->raw = false;
    s->ctx = ctx;
    s->out = out;
}

static void oprint(cli_session_t *s, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (s->out) s->out(s->ctx, buf);
}

static int target_from(const char *arg, int fallback)
{
    if (!arg) return fallback;
    if (!strcmp(arg, "1") || !strcasecmp(arg, "uart1")) return 1;
    if (!strcmp(arg, "2") || !strcasecmp(arg, "uart2")) return 2;
    if (!strcmp(arg, "3") || !strcasecmp(arg, "both") || !strcasecmp(arg, "all")) return 3;
    if (!strcmp(arg, "0") || !strcasecmp(arg, "usb")) return 0;
    return fallback;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void print_help(cli_session_t *s)
{
    oprint(s, "Команды (префикс '/' в USB = локальная команда):");
    oprint(s, "  help                    этот список");
    oprint(s, "  status                  состояние");
    oprint(s, "  target 1|2|both|usb     цель по умолчанию");
    oprint(s, "  raw on|off              весь ввод сразу на UART");
    oprint(s, "  send <1|2|both> <текст> отправить команду");
    oprint(s, "  sendhex <1|2|both> <AA BB CC>  отправить байты");
    oprint(s, "  baud1 <bps> | baud2 <bps>      скорость UART");
    oprint(s, "  autobaud 1|2|both|off [команды]  автоопределение по ответу OK/ERROR");
    oprint(s, "  bridge on|off   echo on|off    mirror on|off");
    oprint(s, "  syslog on|off               логи ESP-IDF на USB");
    oprint(s, "  log [N] | log clear         журнал команд");
    oprint(s, "  wifi <ssid> <password>      подключиться к AP");
    oprint(s, "  ota                       обновление с GitHub");
    oprint(s, "  otacheck                  проверить версию на GitHub");
    oprint(s, "  auto <ms> <1|2|both> <текст>  автокоманда ESP32 (0=выкл)");
    oprint(s, "  monitor on|off              (telnet) показывать трафик");
    oprint(s, "  charset cp866|utf8          кодировка вывода терминала");
    oprint(s, "  passwd <новый>              сменить пароль (путь в вебе + telnet)");
    oprint(s, "  cmdmode url|mqtt|both       канал HEX-команд (адрес / MQTT / оба)");
    oprint(s, "  mqtt <host[:port]|mqtt://host[:port]> <topic> [user] [pass]  MQTT");
    oprint(s, "  time                        текущее время (NTP)");
    oprint(s, "  ntp <server> [tz]           сервер NTP и часовой пояс");
    oprint(s, "  reboot");
}

static void print_status(cli_session_t *s)
{
    const app_config_t *c = config_get();
    uint32_t r1, r2, t1, t2;
    uart_bridge_counters(&r1, &r2, &t1, &t2);

    char ubuf[48];
    time_format_uptime(ubuf, sizeof(ubuf), (uint32_t)(esp_timer_get_time() / 1000000));
    oprint(s, "%s v%s | uptime %s", APP_NAME, APP_VERSION, ubuf);
    oprint(s, "UART1 %d bd (TX%d/RX%d)  RX:%lu TX:%lu", c->baud1,
           4, 5, (unsigned long)r1, (unsigned long)t1);
    oprint(s, "UART2 %d bd (TX25/RX26)  RX:%lu TX:%lu", c->baud2,
           (unsigned long)r2, (unsigned long)t2);
    if (c->auto_baud1 || c->auto_baud2)
        oprint(s, "автоскорость: UART1 %s, UART2 %s, команды '%s'",
               c->auto_baud1 ? (uart_bridge_detected_baud(1) ? "найдена" : "поиск") : "выкл",
               c->auto_baud2 ? (uart_bridge_detected_baud(2) ? "найдена" : "поиск") : "выкл",
               c->baud_probe);
    oprint(s, "bridge:%s echo:%s mirror:%s syslog:%s target:%s raw:%s charset:%s",
           c->bridge ? "on" : "off", c->echo ? "on" : "off",
           c->mirror ? "on" : "off", c->syslog ? "on" : "off",
           c->usb_target == 3 ? "both" : (c->usb_target == 2 ? "uart2" : "uart1"),
           s->raw ? "on" : "off", charset_get() ? "CP866" : "UTF-8");
    oprint(s, "WiFi AP %s STA %s (%s) RSSI %d", wifi_ip_ap(),
           wifi_sta_connected() ? wifi_ip_sta() : "-",
           wifi_sta_connected() ? "ok" : "нет", wifi_rssi());
    oprint(s, "OTA: %s", ota_busy() ? "в процессе" : ota_last_result());
}

static void do_send(cli_session_t *s, const char *arg, const char *rest, bool hex)
{
    int t = target_from(arg, s->target);
    if (!rest || !*rest) { oprint(s, "нет данных для отправки"); return; }

    if (hex) {
        uint8_t bytes[256];
        size_t n = 0;
        char copy[512];
        snprintf(copy, sizeof(copy), "%s", rest);
        char *save = NULL;
        for (char *tok = strtok_r(copy, " ,;", &save); tok; tok = strtok_r(NULL, " ,;", &save)) {
            if (n >= sizeof(bytes)) break;
            if (strlen(tok) == 1 && hexval(tok[0]) >= 0) {
                bytes[n++] = (uint8_t)hexval(tok[0]);
            } else if (strlen(tok) == 2 && hexval(tok[0]) >= 0 && hexval(tok[1]) >= 0) {
                bytes[n++] = (uint8_t)((hexval(tok[0]) << 4) | hexval(tok[1]));
            } else {
                oprint(s, "некорректный HEX: %s", tok);
                return;
            }
        }
        uart_bridge_send(t, bytes, n);
        oprint(s, "отправлено %u байт HEX", (unsigned)n);
    } else {
        char line[600];
        snprintf(line, sizeof(line), "%s\r\n", rest);
        uart_bridge_send(t, (const uint8_t *)line, strlen(line));
        oprint(s, "отправлено %u байт", (unsigned)strlen(line));
    }
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_restart();
}

void cli_process(cli_session_t *s, char *line)
{
    if (!s || !line) return;
    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') line++;
    if (!*line) return;

    if (s->raw) {
        char buf[600];
        snprintf(buf, sizeof(buf), "%s\r\n", line);
        uart_bridge_send(s->target, (const uint8_t *)buf, strlen(buf));
        oprint(s, "[raw -> %s] %s", s->target == 3 ? "both" :
               (s->target == 2 ? "UART2" : "UART1"), line);
        return;
    }

    if (*line == '/') line++;   /* разрешаем /help, /send ... */

    char *save = NULL;
    char *cmd = strtok_r(line, " \t", &save);
    if (!cmd) return;

    if (!strcasecmp(cmd, "help") || !strcmp(cmd, "?")) {
        print_help(s);
    } else if (!strcasecmp(cmd, "status")) {
        print_status(s);
    } else if (!strcasecmp(cmd, "target")) {
        char *a = strtok_r(NULL, " \t", &save);
        s->target = target_from(a, s->target);
        oprint(s, "цель: %s", s->target == 3 ? "both" :
               (s->target == 2 ? "UART2" : (s->target == 1 ? "UART1" : "USB")));
    } else if (!strcasecmp(cmd, "raw")) {
        char *a = strtok_r(NULL, " \t", &save);
        s->raw = a && (!strcasecmp(a, "on") || !strcmp(a, "1"));
        oprint(s, "raw: %s", s->raw ? "on" : "off");
    } else if (!strcasecmp(cmd, "send") || !strcasecmp(cmd, "sendhex")) {
        char *a = strtok_r(NULL, " \t", &save);
        char *rest = save;
        while (rest && (*rest == ' ' || *rest == '\t')) rest++;
        do_send(s, a, rest, !strcasecmp(cmd, "sendhex"));
    } else if (!strcasecmp(cmd, "baud1") || !strcasecmp(cmd, "baud2")) {
        char *a = strtok_r(NULL, " \t", &save);
        int b = a ? atoi(a) : 0;
        if (b < 1200 || b > 3000000) { oprint(s, "неверная скорость"); return; }
        app_config_t *c = config_mut();
        if (!strcasecmp(cmd, "baud1")) c->baud1 = b; else c->baud2 = b;
        uart_bridge_apply_config();
        config_save();
        oprint(s, "%s = %d", cmd, b);
    } else if (!strcasecmp(cmd, "autobaud")) {
        char *a = strtok_r(NULL, " \t", &save);
        char *probes = save;
        app_config_t *c = config_mut();
        bool on1 = false, on2 = false;
        if (!a || !*a) {
            oprint(s, "автоскорость: UART1 %s, UART2 %s, команды '%s'",
                   c->auto_baud1 ? "on" : "off", c->auto_baud2 ? "on" : "off", c->baud_probe);
            return;
        }
        if (!strcasecmp(a, "1")) on1 = true;
        else if (!strcasecmp(a, "2")) on2 = true;
        else if (!strcasecmp(a, "both") || !strcmp(a, "3")) on1 = on2 = true;
        else if (!strcasecmp(a, "off") || !strcmp(a, "0")) { }
        else { oprint(s, "использование: autobaud 1|2|both|off [AT|AT+GMR]"); return; }

        while (probes && (*probes == ' ' || *probes == '\t')) probes++;
        if (probes && *probes) {
            size_t n = strlen(probes);
            while (n && (probes[n - 1] == '\r' || probes[n - 1] == '\n')) probes[--n] = '\0';
            if (n) snprintf(c->baud_probe, sizeof(c->baud_probe), "%s", probes);
        }
        c->auto_baud1 = on1;
        c->auto_baud2 = on2;
        config_save();
        uart_bridge_apply_config();
        oprint(s, "автоскорость: UART1 %s, UART2 %s, команды '%s'",
               on1 ? "on" : "off", on2 ? "on" : "off", c->baud_probe);
    } else if (!strcasecmp(cmd, "bridge") || !strcasecmp(cmd, "echo") ||
               !strcasecmp(cmd, "mirror") || !strcasecmp(cmd, "syslog") ||
               !strcasecmp(cmd, "monitor")) {
        char *a = strtok_r(NULL, " \t", &save);
        bool on = a && (!strcasecmp(a, "on") || !strcmp(a, "1"));
        app_config_t *c = config_mut();
        if (!strcasecmp(cmd, "bridge")) c->bridge = on;
        else if (!strcasecmp(cmd, "echo")) c->echo = on;
        else if (!strcasecmp(cmd, "mirror")) c->mirror = on;
        else if (!strcasecmp(cmd, "syslog")) c->syslog = on;
        else { oprint(s, "monitor доступен в telnet; здесь используется raw/send"); return; }
        config_save();
        oprint(s, "%s: %s", cmd, on ? "on" : "off");
    } else if (!strcasecmp(cmd, "log")) {
        char *a = strtok_r(NULL, " \t", &save);
        if (a && !strcasecmp(a, "clear")) {
            event_bus_log_clear();
            oprint(s, "журнал очищен");
        } else {
            char *txt = event_bus_log_get(a ? atoi(a) : 50);
            if (txt && *txt) {
                char *p = txt;
                while (*p) {
                    char *e = strchr(p, '\n');
                    if (e) *e = '\0';
                    oprint(s, "%s", p);
                    if (!e) break;
                    p = e + 1;
                }
            } else {
                oprint(s, "журнал пуст");
            }
            free(txt);
        }
    } else if (!strcasecmp(cmd, "charset")) {
        char *a = strtok_r(NULL, " \t", &save);
        if (a && (!strcasecmp(a, "utf8") || !strcasecmp(a, "utf-8") || !strcasecmp(a, "utf"))) {
            charset_set(0);
        } else if (a && (!strcasecmp(a, "cp866") || !strcasecmp(a, "dos"))) {
            charset_set(1);
        }
        oprint(s, "кодировка вывода: %s", charset_get() ? "CP866" : "UTF-8");
    } else if (!strcasecmp(cmd, "wifi")) {
        char *ssid = strtok_r(NULL, " \t", &save);
        char *pass = save;
        while (pass && (*pass == ' ' || *pass == '\t')) pass++;
        if (!ssid || !pass) { oprint(s, "использование: wifi <ssid> <password>"); return; }
        app_config_t *c = config_mut();
        snprintf(c->wifi_ssid, sizeof(c->wifi_ssid), "%s", ssid);
        snprintf(c->wifi_pass, sizeof(c->wifi_pass), "%s", pass);
        config_save();
        wifi_reconnect();
        oprint(s, "WiFi сохранён: %s", ssid);
    } else if (!strcasecmp(cmd, "ota")) {
        ota_start_remote_update();
        oprint(s, "OTA запущена с GitHub");
    } else if (!strcasecmp(cmd, "otacheck")) {
        ota_check_now();
        oprint(s, "проверка версии запущена");
    } else if (!strcasecmp(cmd, "auto")) {
        char *a = strtok_r(NULL, " \t", &save);
        char *t = strtok_r(NULL, " \t", &save);
        char *rest = save;
        while (rest && (*rest == ' ' || *rest == '\t')) rest++;
        app_config_t *c = config_mut();
        c->auto_ms = a ? atoi(a) : 0;
        snprintf(c->auto_target, sizeof(c->auto_target), "%s", t ? t : "1");
        snprintf(c->auto_text, sizeof(c->auto_text), "%s", rest ? rest : "");
        config_save();
        oprint(s, "автокоманда: %d мс, цель %s, '%s'",
               c->auto_ms, c->auto_target, c->auto_text);
    } else if (!strcasecmp(cmd, "passwd")) {
        char *a = save;
        while (a && (*a == ' ' || *a == '\t')) a++;
        if (!a || !*a) { oprint(s, "использование: passwd <новый пароль>"); return; }
        /* убираем завершающий CR/LF, если попал в строку */
        size_t n = strlen(a);
        while (n && (a[n - 1] == '\r' || a[n - 1] == '\n')) a[--n] = '\0';
        if (n == 0) { oprint(s, "пароль не может быть пустым"); return; }
        if (auth_set_password(a)) {
            oprint(s, "пароль изменён, веб-адрес: /%s (telnet -- тот же пароль)", a);
        } else {
            oprint(s, "разрешены латиница, цифры и . _ - ~ (до 32 символов)");
        }
    } else if (!strcasecmp(cmd, "cmdmode")) {
        char *a = strtok_r(NULL, " \t", &save);
        app_config_t *c = config_mut();
        int old = c->cmd_mode;
        if (!a || !*a) {
            oprint(s, "канал HEX-команд: %s", c->cmd_mode == 0 ? "адресная строка" :
                   (c->cmd_mode == 1 ? "MQTT" : "адресная строка + MQTT"));
            return;
        }
        if (!strcasecmp(a, "url") || !strcmp(a, "0")) c->cmd_mode = 0;
        else if (!strcasecmp(a, "mqtt") || !strcmp(a, "1")) c->cmd_mode = 1;
        else if (!strcasecmp(a, "both") || !strcmp(a, "2")) c->cmd_mode = 2;
        else { oprint(s, "режим: url|mqtt|both"); return; }
        config_save();
        if (c->cmd_mode != old) mqtt_apply_config();
        oprint(s, "канал HEX-команд: %s", c->cmd_mode == 0 ? "адресная строка" :
               (c->cmd_mode == 1 ? "MQTT" : "адресная строка + MQTT"));
    } else if (!strcasecmp(cmd, "mqtt")) {
        char *uri = strtok_r(NULL, " \t", &save);
        char *topic = strtok_r(NULL, " \t", &save);
        char *user = strtok_r(NULL, " \t", &save);
        char *pass = strtok_r(NULL, " \t", &save);
        app_config_t *c = config_mut();
        if (!uri || !*uri) {
            oprint(s, "MQTT: %s, топик %s", c->mqtt_uri[0] ? c->mqtt_uri : "(не задан)", c->mqtt_topic);
            return;
        }
        snprintf(c->mqtt_uri, sizeof(c->mqtt_uri), "%s", uri);
        if (topic && *topic) snprintf(c->mqtt_topic, sizeof(c->mqtt_topic), "%s", topic);
        if (user) snprintf(c->mqtt_user, sizeof(c->mqtt_user), "%s", user);
        if (pass) snprintf(c->mqtt_pass, sizeof(c->mqtt_pass), "%s", pass);
        config_save();
        mqtt_apply_config();
        char eff[192];
        mqtt_normalize_uri(c->mqtt_uri, eff, sizeof(eff));
        oprint(s, "MQTT: %s / %s", eff, c->mqtt_topic);
    } else if (!strcasecmp(cmd, "time")) {
        char buf[40];
        time_sync_format(buf, sizeof(buf));
        oprint(s, "время: %s (%s)", buf, time_sync_synced() ? "NTP" : "не синхронизировано");
    } else if (!strcasecmp(cmd, "ntp")) {
        char *srv = strtok_r(NULL, " \t", &save);
        char *tz = strtok_r(NULL, " \t", &save);
        app_config_t *c = config_mut();
        if (!srv || !*srv) {
            oprint(s, "NTP: %s, TZ %s", c->ntp_server, c->ntp_tz);
            return;
        }
        bool changed = false;
        if (strcmp(c->ntp_server, srv)) { snprintf(c->ntp_server, sizeof(c->ntp_server), "%s", srv); changed = true; }
        if (tz && *tz && strcmp(c->ntp_tz, tz)) { snprintf(c->ntp_tz, sizeof(c->ntp_tz), "%s", tz); changed = true; }
        if (changed) { config_save(); time_sync_apply(); }
        char buf[40];
        time_sync_format(buf, sizeof(buf));
        oprint(s, "NTP: %s, TZ %s, время %s", c->ntp_server, c->ntp_tz, buf);
    } else if (!strcasecmp(cmd, "reboot")) {
        oprint(s, "перезагрузка...");
        xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    } else {
        oprint(s, "неизвестная команда: %s (help)", cmd);
    }
}

void cli_usb_bytes(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (c == '\r') continue;
        if (c == '\n') {
            s_usb_line[s_usb_len] = '\0';
            char copy[USB_LINE_MAX];
            snprintf(copy, sizeof(copy), "%s", s_usb_line);
            s_usb_len = 0;
            cli_process(&s_usb_session, copy);
            usb_out(NULL, ">");
            continue;
        }
        if (s_usb_len + 1 < sizeof(s_usb_line)) s_usb_line[s_usb_len++] = (char)c;
    }
}

void cli_init_usb(void)
{
    cli_session_init(&s_usb_session, NULL, usb_out);
    char hello[160];
    snprintf(hello, sizeof(hello),
             "%s готов. Введите текст -> UART, '/help' для команд.", APP_NAME);
    usb_out(NULL, hello);
    usb_out(NULL, ">");
}
