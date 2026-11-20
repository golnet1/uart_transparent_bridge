#include "mqtt_channels.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

#include "app.h"
#include "config.h"
#include "event_bus.h"
#include "uart_bridge.h"
#include "wifi.h"

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client;
static QueueHandle_t s_evt_q;
static bool s_connected;
static int  s_cmd_target = 3;  /* цель, заданная через <base>/target */
static char s_uri[192];   /* нормализованный адрес брокера */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Пayload команды — ASCII HEX: "41 54 0D", "41540D", "0x41,0x54" */
static size_t hex_to_bytes(const char *src, size_t len, uint8_t *out, size_t cap)
{
    size_t n = 0;
    int hi = -1;
    for (size_t i = 0; i < len && n < cap; i++) {
        char c = src[i];
        if (c == '0' && i + 1 < len && (src[i + 1] == 'x' || src[i + 1] == 'X')) { i++; continue; }
        int v = hexval(c);
        if (v < 0) continue;                 /* пробелы, запятые, \r\n */
        if (hi < 0) hi = v;
        else { out[n++] = (uint8_t)((hi << 4) | v); hi = -1; }
    }
    if (hi >= 0 && n < cap) out[n++] = (uint8_t)hi;   /* нечётная цифра = 0x0N */
    return n;
}

static void topic_result(char *buf, size_t cap)
{
    snprintf(buf, cap, "%s/result", config_get()->mqtt_topic);
}
static void topic_traffic(char *buf, size_t cap)
{
    snprintf(buf, cap, "%s/traffic", config_get()->mqtt_topic);
}

static bool topic_is(const char *topic, int len, const char *expected)
{
    size_t n = strlen(expected);
    return topic && len == (int)n && strncmp(topic, expected, n) == 0;
}

/* "0|1|2|3" или "usb|uart1|uart2|both|all" → 0..3 */
static int parse_target(const char *src, size_t len, int fallback)
{
    if (!src || !len) return fallback;
    char buf[24];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, src, n);
    buf[n] = '\0';
    char *p = buf;
    while (*p && isspace((unsigned char)*p)) p++;
    char *e = p + strlen(p);
    while (e > p && isspace((unsigned char)e[-1])) *--e = '\0';
    if (!*p) return fallback;

    if (!strcmp(p, "0") || !strcasecmp(p, "usb")) return 0;
    if (!strcmp(p, "1") || !strcasecmp(p, "uart1")) return 1;
    if (!strcmp(p, "2") || !strcasecmp(p, "uart2")) return 2;
    if (!strcmp(p, "3") || !strcasecmp(p, "both") || !strcasecmp(p, "all")) return 3;

    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end && *end == '\0' && v >= 0 && v <= 3) return (int)v;
    return fallback;
}

static const char *target_name(int t)
{
    return t == 0 ? "USB" : (t == 1 ? "UART1" : (t == 2 ? "UART2" : "оба UART"));
}

static void traffic_task(void *arg)
{
    (void)arg;
    app_msg_t *msg;
    while (xQueueReceive(s_evt_q, &msg, portMAX_DELAY) == pdTRUE) {
        if (s_connected && s_client && msg) {
            char topic[96];
            topic_traffic(topic, sizeof(topic));
            esp_mqtt_client_publish(s_client, topic, msg->line, 0, 0, 0);
        }
        free(msg);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args; (void)base;
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)event_data;
    if (!ev || !ev->client) return;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED: {
        s_connected = true;
        const char *base = config_get()->mqtt_topic;
        /* Достаточно только "rx/#": по спецификации MQTT он совпадает и с
           самим "rx", и с "rx/1|2|3". Отдельная подписка на "rx" приводила
           к двойной доставке команды и задвоению её в журнале. */
        static const char *const subs[] = {"rx/#", "code", "target"};
        char topic[96];
        for (size_t i = 0; i < sizeof(subs) / sizeof(subs[0]); i++) {
            snprintf(topic, sizeof(topic), "%s/%s", base, subs[i]);
            esp_mqtt_client_subscribe(ev->client, topic, 0);
            ESP_LOGI(TAG, "подписка: %s", topic);
        }
        snprintf(topic, sizeof(topic), "%s/status", base);
        esp_mqtt_client_publish(ev->client, topic, "online", 0, 0, true);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "отключен");
        break;
    case MQTT_EVENT_DATA: {
        const char *base = config_get()->mqtt_topic;
        char t_code[96], t_target[96];
        snprintf(t_code, sizeof(t_code), "%s/code", base);
        snprintf(t_target, sizeof(t_target), "%s/target", base);

        /* <base>/target задаёт цель отдельно: 0..3 или usb/uart1/uart2/both */
        if (topic_is(ev->topic, ev->topic_len, t_target)) {
            int t = parse_target(ev->data, ev->data_len, s_cmd_target);
            s_cmd_target = t;
            ESP_LOGI(TAG, "цель MQTT: %s (%d)", target_name(t), t);
            break;
        }

        /* <base>/code — код отдельно, цель берём из <base>/target.
           <base>/rx и суффикс /1|/2|/3 — команда одним топиком. */
        const bool separate_code = topic_is(ev->topic, ev->topic_len, t_code);
        int target = separate_code ? s_cmd_target : config_get()->cmd_target;
        if (!separate_code && ev->topic_len >= 2 && ev->topic &&
            ev->topic[ev->topic_len - 2] == '/' &&
            ev->topic[ev->topic_len - 1] >= '0' && ev->topic[ev->topic_len - 1] <= '3') {
            target = ev->topic[ev->topic_len - 1] - '0';
        }

        uint8_t bytes[512];
        size_t n = hex_to_bytes(ev->data, ev->data_len, bytes, sizeof(bytes));
        char result[96];
        if (n) {
            uart_bridge_send(target, bytes, n);
            snprintf(result, sizeof(result), "OK bytes=%u target=%d", (unsigned)n, target);
            ESP_LOGI(TAG, "команда HEX%s: %u байт -> %s",
                     separate_code ? " [code]" : "", (unsigned)n, target_name(target));
        } else {
            snprintf(result, sizeof(result), "ERROR: empty/invalid HEX");
        }
        char topic[96];
        topic_result(topic, sizeof(topic));
        esp_mqtt_client_publish(ev->client, topic, result, 0, 0, false);
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "ошибка MQTT");
        break;
    default:
        break;
    }
}

static size_t uri_append(char *dst, size_t pos, size_t cap, const char *src, size_t len)
{
    if (!dst || !cap || pos >= cap - 1) return pos;
    if (len > cap - 1 - pos) len = cap - 1 - pos;
    if (len) memcpy(dst + pos, src, len);
    return pos + len;
}

/* "host", "host:1883", "mqtt://host" → "mqtt://host:1883" */
void mqtt_normalize_uri(const char *src, char *dst, size_t cap)
{
    if (!dst || !cap) return;
    dst[0] = '\0';
    if (!src || !*src) return;

    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') src++;
    const char *end = src + strlen(src);
    while (end > src && (end[-1] == ' ' || end[-1] == '\t' ||
                         end[-1] == '\r' || end[-1] == '\n')) end--;
    if (end == src) return;

    /* Схема — это "xxx://" только до первого '/' */
    const char *sep = NULL;
    for (const char *p = src; p < end && (size_t)(p - src) < 16; p++) {
        if (p > src && p[0] == ':' && p + 2 < end && p[1] == '/' && p[2] == '/' &&
            !memchr(src, '/', (size_t)(p - src))) { sep = p; break; }
    }
    const bool has_scheme = (sep != NULL);
    const char *auth = has_scheme ? sep + 3 : src;
    if (auth >= end) { uri_append(dst, 0, cap, src, (size_t)(end - src)); return; }

    const char *auth_end = auth;
    while (auth_end < end && *auth_end != '/') auth_end++;
    size_t alen = (size_t)(auth_end - auth);
    if (!alen) { uri_append(dst, 0, cap, src, (size_t)(end - src)); return; }

    const char *colon = NULL;
    if (*auth == '[') {                       /* IPv6 в квадратных скобках */
        const char *rb = auth;
        while (rb < auth_end && *rb != ']') rb++;
        if (rb < auth_end && rb + 1 < auth_end && rb[1] == ':') colon = rb + 1;
    } else {
        for (const char *p = auth; p < auth_end; p++) {
            if (*p == ':') { colon = p; break; }
        }
    }
    const bool has_port = (colon != NULL);

    size_t pos = 0;
    if (has_scheme) pos = uri_append(dst, pos, cap, src, (size_t)(auth - src));
    else            pos = uri_append(dst, pos, cap, "mqtt://", 7);
    pos = uri_append(dst, pos, cap, auth, alen);
    if (!has_port)  pos = uri_append(dst, pos, cap, ":1883", 5);
    pos = uri_append(dst, pos, cap, auth_end, (size_t)(end - auth_end));
    dst[pos] = '\0';
}

static void start_client(void)
{
    const app_config_t *c = config_get();
    if (s_client) return;                       /* не создаём второй клиент */
    if (c->mqtt_uri[0] == '\0' || (c->cmd_mode != 1 && c->cmd_mode != 2)) return;
    if (!wifi_sta_connected()) return;           /* только после получения STA IP */

    mqtt_normalize_uri(c->mqtt_uri, s_uri, sizeof(s_uri));
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = s_uri,
        .credentials.username = c->mqtt_user[0] ? c->mqtt_user : NULL,
        .credentials.authentication.password = c->mqtt_pass[0] ? c->mqtt_pass : NULL,
        .session.keepalive = 60,
        .network.reconnect_timeout_ms = 2000,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) { ESP_LOGE(TAG, "не удалось создать клиент"); return; }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) ESP_LOGE(TAG, "start: %s", esp_err_to_name(err));
    else ESP_LOGI(TAG, "подключение к %s", s_uri);
}

/* STA получил IP — теперь можно подключаться к брокеру. Повторные события
   игнорируются: start_client() не создаёт клиент, если он уже жив. */
static void sta_got_ip_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    start_client();
}

void mqtt_init(void)
{
    if (s_evt_q) return;
    s_evt_q = xQueueCreate(64, sizeof(app_msg_t *));
    event_bus_subscribe(s_evt_q, SUB_TRAFFIC);
    xTaskCreate(traffic_task, "mqtt_traffic", 4096, NULL, 4, NULL);
    s_cmd_target = config_get()->cmd_target;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        sta_got_ip_handler, NULL, NULL));
    start_client();   /* на случай, если STA уже подключён */
}

void mqtt_apply_config(void)
{
    if (s_client) {
        s_connected = false;
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_cmd_target = config_get()->cmd_target;
    start_client();
}

bool mqtt_connected(void) { return s_connected; }
int  mqtt_target(void)    { return s_cmd_target; }

/* Публикуем каждую отправленную команду отдельно: код и цель.
   Вызывается из uart_bridge_send(), поэтому покрывает веб/URL/MQTT/CLI/автокоманду. */
void mqtt_publish_sent(int target, const uint8_t *data, size_t len)
{
    if (!s_connected || !s_client || !data || !len) return;

    char *hex = malloc(len * 2 + 1);
    if (!hex) return;
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; i++) {
        hex[i * 2]     = digits[data[i] >> 4];
        hex[i * 2 + 1] = digits[data[i] & 0x0F];
    }
    hex[len * 2] = '\0';

    /* retain=true: брокер хранит последние значения, поэтому топики видны
       даже если подписаться после отправки команды. Устройство не подписано
       на эти топики — петель обратной связи нет. */
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/tx/code", config_get()->mqtt_topic);
    esp_mqtt_client_publish(s_client, topic, hex, 0, 0, true);
    snprintf(topic, sizeof(topic), "%s/tx/target", config_get()->mqtt_topic);
    char tbuf[8];
    snprintf(tbuf, sizeof(tbuf), "%d", target);
    esp_mqtt_client_publish(s_client, topic, tbuf, 0, 0, true);
    free(hex);
}
