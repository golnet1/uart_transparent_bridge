#include "event_bus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_timer.h"
#include "driver/uart.h"
#include "config.h"
#include "uart_bridge.h"
#include "time_sync.h"

#define LOG_CAPACITY (32 * 1024)
#define MAX_SUBSCRIBERS 10
#define DISPLAY_BYTES 96

typedef struct {
    QueueHandle_t q;
    uint32_t mask;
} subscriber_t;

static subscriber_t s_subs[MAX_SUBSCRIBERS];
static SemaphoreHandle_t s_mu;
static portMUX_TYPE s_publish_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_publishing;
static char s_log[LOG_CAPACITY];
static size_t s_log_head;   /* позиция следующей записи */
static size_t s_log_used;
static bool s_ready;

void event_bus_init(void)
{
    s_mu = xSemaphoreCreateMutex();
    s_ready = true;
}

void event_bus_subscribe(QueueHandle_t q, uint32_t mask)
{
    if (!q) return;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (s_subs[i].q == NULL) {
            s_subs[i].q = q;
            s_subs[i].mask = mask;
            break;
        }
    }
    xSemaphoreGive(s_mu);
}

void event_bus_unsubscribe(QueueHandle_t q)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (s_subs[i].q == q) {
            s_subs[i].q = NULL;
            s_subs[i].mask = 0;
        }
    }
    xSemaphoreGive(s_mu);
}

static const char *tag_of(ev_type_t t)
{
    switch (t) {
        case EV_FROM1:   return "UART1 > UART2";
        case EV_FROM2:   return "UART2 > UART1";
        case EV_FROMUSB: return "USB  > UART";
        case EV_TO1:     return "LOCAL > UART1";
        case EV_TO2:     return "LOCAL > UART2";
        case EV_SYS:     return "SYS";
        case EV_OTA:     return "OTA";
        case EV_WIFI:    return "WIFI";
        default:         return "?";
    }
}

static void append_bytes(char *buf, size_t cap, const uint8_t *d, size_t n)
{
    size_t p = strlen(buf);
    size_t shown = n < DISPLAY_BYTES ? n : DISPLAY_BYTES;

    /* Пробелы между байтами не печатаем: наружу уходит плотный HEX, например 41540D */
    for (size_t i = 0; i < shown && p + 4 < cap; i++) {
        p += snprintf(buf + p, cap - p, "%02X", d[i]);
    }
    if (n > shown && p + 4 < cap) {
        p += snprintf(buf + p, cap - p, "...");
    }
    if (p + 4 < cap) {
        p += snprintf(buf + p, cap - p, " |\"");
    }
    for (size_t i = 0; i < shown && p + 4 < cap; i++) {
        uint8_t c = d[i];
        if (c == '\r')      p += snprintf(buf + p, cap - p, "\\r");
        else if (c == '\n') p += snprintf(buf + p, cap - p, "\\n");
        else if (c == '\t') p += snprintf(buf + p, cap - p, "\\t");
        else if (c >= 0x20 && c < 0x7f) p += snprintf(buf + p, cap - p, "%c", c);
        else p += snprintf(buf + p, cap - p, ".");
    }
    if (p + 4 < cap) snprintf(buf + p, cap - p, "\"|");
}

static char *format_message(ev_type_t type, const uint8_t *data, size_t len, bool text)
{
    char *out = malloc(768);
    if (!out) return NULL;

    if (time_sync_synced()) {
        /* После NTP в журнале пишем реальное локальное время */
        time_t now = time(NULL);
        struct tm ti;
        if (localtime_r(&now, &ti)) {
            snprintf(out, 768, "[%02d.%02d %02d:%02d:%02d] [%s] ",
                     ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min,
                     ti.tm_sec, tag_of(type));
        } else {
            snprintf(out, 768, "[--.-- --:--:--] [%s] ", tag_of(type));
        }
    } else {
        /* До синхронизации — время с моментa загрузки */
        int64_t ms = esp_timer_get_time() / 1000;
        int h = (int)((ms / 3600000) % 24);
        int m = (int)((ms / 60000) % 60);
        int s = (int)((ms / 1000) % 60);
        int msec = (int)(ms % 1000);
        snprintf(out, 768, "[%02d:%02d:%02d.%03d] [%s] ",
                 h, m, s, msec, tag_of(type));
    }
    if (data && len) {
        if (text) {
            /* Строковые события (SYS/WIFI/OTA) — обычный текст, не hex-дамп */
            size_t p = strlen(out);
            snprintf(out + p, 768 - p, "%.*s", (int)len, (const char *)data);
        } else {
            append_bytes(out, 768, data, len);
        }
    }
    /* ESP-IDF присылает логи уже с '\n' на конце. log_add() добавляет свой
       разделитель, поэтому без этой очистки в журнале появляются пустые строки
       и счётчик считает их за отдельные сообщения. */
    size_t n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = '\0';
    return out;
}

static void log_add(const char *line)
{
    size_t l = strlen(line) + 1;   /* включая \0 as separator */
    xSemaphoreTake(s_mu, portMAX_DELAY);
    if (l > LOG_CAPACITY) {
        s_log_head = 0;
        s_log_used = 0;
    } else {
        /* вытесняем самые старые байты, пока не хватит места */
        while (l > LOG_CAPACITY - s_log_used) {
            s_log_head = (s_log_head + 1) % LOG_CAPACITY;
            s_log_used--;
        }
    }
    for (size_t i = 0; i < l; i++) {
        s_log[(s_log_head + i) % LOG_CAPACITY] = (i + 1 < l) ? line[i] : '\n';
    }
    s_log_head = (s_log_head + l) % LOG_CAPACITY;
    s_log_used += l;
    xSemaphoreGive(s_mu);
}

static void publish_internal(ev_type_t type, const uint8_t *data, size_t len, bool text)
{
    if (!s_ready || type >= EV_MAX) return;

    /* Защита от рекурсии: uart_write_bytes() при ошибке пишет в ESP_LOG,
       а тот снова вызывает publish. */
    portENTER_CRITICAL(&s_publish_mux);
    if (s_publishing) {
        portEXIT_CRITICAL(&s_publish_mux);
        return;
    }
    s_publishing = true;
    portEXIT_CRITICAL(&s_publish_mux);

    char *line = format_message(type, data, len, text);
    if (!line) goto done;

    log_add(line);

    /* На ранней стадии загрузки драйвер UART0 ещё не установлен */
    if (type == EV_SYS && config_get()->syslog && uart_bridge_usb_ready()) {
        uart_bridge_usb_print(line);
        uart_bridge_usb_print("\r\n");
    }

    xSemaphoreTake(s_mu, portMAX_DELAY);
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!s_subs[i].q || !(s_subs[i].mask & EV_BIT(type))) continue;
        size_t l = strlen(line);
        app_msg_t *msg = malloc(sizeof(app_msg_t) + l + 1);
        if (!msg) break;
        msg->type = type;
        msg->ts_ms = esp_timer_get_time() / 1000;
        memcpy(msg->line, line, l + 1);
        if (xQueueSend(s_subs[i].q, &msg, 0) != pdTRUE) free(msg);
    }
    xSemaphoreGive(s_mu);
    free(line);

done:
    portENTER_CRITICAL(&s_publish_mux);
    s_publishing = false;
    portEXIT_CRITICAL(&s_publish_mux);
}

void event_bus_publish(ev_type_t type, const uint8_t *data, size_t len)
{
    publish_internal(type, data, len, false);
}

void event_bus_publish_str(ev_type_t type, const char *str)
{
    if (!str) return;
    publish_internal(type, (const uint8_t *)str, strlen(str), true);
}

char *event_bus_log_get(int n)
{
    if (n <= 0) n = 200;
    if (n > 2000) n = 2000;

    xSemaphoreTake(s_mu, portMAX_DELAY);
    size_t used = s_log_used;
    char *linear = malloc(used + 1);
    if (!linear) { xSemaphoreGive(s_mu); return NULL; }
    for (size_t i = 0; i < used; i++) {
        linear[i] = s_log[(s_log_head - used + i + LOG_CAPACITY) % LOG_CAPACITY];
    }
    linear[used] = '\0';
    xSemaphoreGive(s_mu);

    /* Нужно (n+1)-й перевод строки с конца: он стоит ПЕРЕД последними n
       строками. Искали n-й — получалось на одну строку меньше. Если всего
       строк не больше n, (n+1)-й не найдётся и вернётся весь буфер. */
    size_t start = 0;
    int seen = 0;
    for (size_t i = used; i > 0; i--) {
        if (linear[i - 1] == '\n') {
            seen++;
            if (seen == n + 1) { start = i; break; }
        }
    }
    char *res = strdup(linear + start);
    free(linear);
    return res ? res : strdup("");
}

int event_bus_log_lines(void)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    int cnt = 0;
    for (size_t i = 0; i < s_log_used; i++) {
        if (s_log[(s_log_head - s_log_used + i + LOG_CAPACITY) % LOG_CAPACITY] == '\n') cnt++;
    }
    xSemaphoreGive(s_mu);
    return cnt;
}

void event_bus_log_clear(void)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_log_head = 0;
    s_log_used = 0;
    s_log[0] = '\0';
    xSemaphoreGive(s_mu);
}
