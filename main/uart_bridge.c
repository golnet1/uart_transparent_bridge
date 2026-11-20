#include "uart_bridge.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "driver/uart.h"
#include "config.h"
#include "charset.h"
#include "event_bus.h"
#include "cli.h"
#include "mqtt_channels.h"

#ifndef UART1_TX_PIN
#define UART1_TX_PIN 4
#endif
#ifndef UART1_RX_PIN
#define UART1_RX_PIN 5
#endif
#ifndef UART2_TX_PIN
#define UART2_TX_PIN 25
#endif
#ifndef UART2_RX_PIN
#define UART2_RX_PIN 26
#endif
#ifndef USB_TX_PIN
#define USB_TX_PIN 1
#endif
#ifndef USB_RX_PIN
#define USB_RX_PIN 3
#endif

#define PORT1 UART_NUM_1
#define PORT2 UART_NUM_2
#define PORTA UART_NUM_0   /* USB-UART bridge (UART0) */

static const char *TAG = "uart_bridge";
static uint32_t s_rx1, s_rx2, s_tx1, s_tx2;
static bool s_usb_ready;

/* Автоопределение скорости: состояние выключателей и результат опроса */
static volatile bool s_detect_pending[3];
static volatile bool s_detect_active[3];
static int  s_detected_baud[3];
static bool s_prev_auto[3];
static int  s_prev_baud[3];
static char s_prev_probe[64];

static const char *full_target(int t)
{
    if (t == 3) return "USB/both";
    if (t == 2) return "UART2";
    if (t == 1) return "UART1";
    return "USB";
}

static void mirror_to_usb(const char *tag, const uint8_t *d, size_t n)
{
    char line[640];
    size_t p = snprintf(line, sizeof(line), "\r\n[%s] ", tag);
    size_t shown = n < 96 ? n : 96;
    for (size_t i = 0; i < shown && p + 4 < sizeof(line); i++) {
        p += snprintf(line + p, sizeof(line) - p, "%02X ", d[i]);
    }
    if (n > shown && p + 4 < sizeof(line)) p += snprintf(line + p, sizeof(line) - p, "...");
    if (p + 4 < sizeof(line)) p += snprintf(line + p, sizeof(line) - p, " |\"");
    for (size_t i = 0; i < shown && p + 4 < sizeof(line); i++) {
        uint8_t c = d[i];
        if (c == '\r')      p += snprintf(line + p, sizeof(line) - p, "\\r");
        else if (c == '\n') p += snprintf(line + p, sizeof(line) - p, "\\n");
        else if (c == '\t') p += snprintf(line + p, sizeof(line) - p, "\\t");
        else if (c >= 0x20 && c < 0x7f) p += snprintf(line + p, sizeof(line) - p, "%c", c);
        else p += snprintf(line + p, sizeof(line) - p, ".");
    }
    if (p + 4 < sizeof(line)) snprintf(line + p, sizeof(line) - p, "\"|");
    uart_write_bytes(PORTA, line, strlen(line));
}

static void forward(ev_type_t ev, uart_port_t dst,
                    const char *mirror_tag, const uint8_t *d, size_t n)
{
    if (config_get()->bridge) uart_write_bytes(dst, d, n);
    if (dst == PORT1) s_tx1 += n;
    if (dst == PORT2) s_tx2 += n;
    event_bus_publish(ev, d, n);
    if (config_get()->mirror) mirror_to_usb(mirror_tag, d, n);
}

/* --- Автоопределение скорости по ответу на понятные команды --- */
static bool has_reply(const uint8_t *d, size_t n)
{
    char buf[192];
    size_t len = n < sizeof(buf) - 1 ? n : sizeof(buf) - 1;
    memcpy(buf, d, len);
    buf[len] = '\0';
    for (size_t i = 0; i < len; i++) buf[i] = (char)tolower((unsigned char)buf[i]);
    return strstr(buf, "ok") != NULL || strstr(buf, "error") != NULL;
}

/* "AT|AT+GMR" -> {"AT", "AT+GMR"}; пустое поле -> "AT" */
static int split_probes(char out[4][32])
{
    const char *src = config_get()->baud_probe;
    int n = 0;
    while (*src && n < 4) {
        while (*src == '|' || *src == ';' || *src == ' ' || *src == ',') src++;
        if (!*src) break;
        size_t k = 0;
        while (*src && *src != '|' && *src != ';' && *src != ',' && k < 31)
            out[n][k++] = *src++;
        out[n][k] = '\0';
        n++;
    }
    if (!n) { strcpy(out[0], "AT"); n = 1; }
    return n;
}

static int detect_baud(uart_port_t port, int ch)
{
    static const int bauds[] = {1200, 2400, 4800, 9600, 19200, 38400,
                                57600, 115200, 230400, 460800, 921600};
    char probes[4][32];
    int np = split_probes(probes);
    int fallback = (ch == 1) ? config_get()->baud1 : config_get()->baud2;
    int found = 0;
    char matched[32] = "";

    s_detect_active[ch] = true;
    for (size_t i = 0; i < sizeof(bauds) / sizeof(bauds[0]) && !found; i++) {
        uart_set_baudrate(port, bauds[i]);
        uart_flush_input(port);
        for (int p = 0; p < np && !found; p++) {
            char line[40];
            int len = snprintf(line, sizeof(line), "%s\r\n", probes[p]);
            uart_write_bytes(port, line, len);

            uint8_t resp[160];
            size_t total = 0;
            for (int waited = 0; waited < 450 && !found; waited += 20) {
                if (total >= sizeof(resp) - 1) break;
                int r = uart_read_bytes(port, resp + total,
                                        sizeof(resp) - 1 - total, pdMS_TO_TICKS(20));
                if (r <= 0) continue;
                total += (size_t)r;
                resp[total] = '\0';
                if (has_reply(resp, total)) {
                    found = bauds[i];
                    snprintf(matched, sizeof(matched), "%s", probes[p]);
                }
            }
        }
    }
    s_detect_active[ch] = false;
    s_detected_baud[ch] = found;

    int applied = found ? found : fallback;
    uart_set_baudrate(port, applied);
    if (found)
        ESP_LOGI(TAG, "UART%d: скорость %d бод определена по команде '%s'",
                 ch, found, matched);
    else
        ESP_LOGW(TAG, "UART%d: нет ответа на '%s', оставлена скорость %d бод",
                 ch, config_get()->baud_probe, fallback);
    return applied;
}

static void uart_rx_task(void *arg)
{
    uintptr_t ch = (uintptr_t)arg;   /* 1, 2 или 0 (USB) */
    uart_port_t port = (ch == 1) ? PORT1 : (ch == 2) ? PORT2 : PORTA;
    uint8_t buf[128];

    while (1) {
        if (ch == 1 || ch == 2) {
            if (s_detect_pending[ch]) {
                s_detect_pending[ch] = false;
                detect_baud(port, ch);
                continue;
            }
            /* во время опроса не пересылаем пробные байты на другой порт */
            if (s_detect_active[1] || s_detect_active[2]) continue;
        }
        int n = uart_read_bytes(port, buf, sizeof(buf), pdMS_TO_TICKS(20));
        if (n <= 0) continue;

        if (ch == 1) {
            s_rx1 += n;
            forward(EV_FROM1, PORT2, "UART1", buf, n);
            if (config_get()->echo) uart_write_bytes(PORT1, buf, n);
        } else if (ch == 2) {
            s_rx2 += n;
            forward(EV_FROM2, PORT1, "UART2", buf, n);
            if (config_get()->echo) uart_write_bytes(PORT2, buf, n);
        } else {
            /* USB: собираем строку команд */
            event_bus_publish(EV_FROMUSB, buf, n);
            cli_usb_bytes(buf, n);
        }
    }
}

static int parse_target_str(const char *s)
{
    if (!s) return 1;
    if (!strcmp(s, "1") || !strncmp(s, "uart1", 5)) return 1;
    if (!strcmp(s, "2") || !strncmp(s, "uart2", 5)) return 2;
    if (!strcmp(s, "both") || !strcmp(s, "3") || !strcmp(s, "all")) return 3;
    if (!strcmp(s, "usb") || !strcmp(s, "0")) return 0;
    return 1;
}

static void auto_task(void *arg)
{
    (void)arg;
    while (1) {
        const app_config_t *c = config_get();
        if (c->auto_ms <= 0 || !c->auto_text[0] || !strcmp(c->auto_target, "0")) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        int t = parse_target_str(c->auto_target);
        char line[160];
        snprintf(line, sizeof(line), "%s\r\n", c->auto_text);
        uart_bridge_send(t, (const uint8_t *)line, strlen(line));
        vTaskDelay(pdMS_TO_TICKS(c->auto_ms));
    }
}

static esp_err_t setup_port(uart_port_t port, int baud, int tx_pin, int rx_pin)
{
    uart_config_t cfg = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(port, &cfg);
    if (err != ESP_OK) return err;
    err = uart_set_pin(port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;
    return uart_driver_install(port, 512, 256, 16, NULL, 0);
}

void uart_bridge_init(void)
{
    const app_config_t *c = config_get();

    ESP_ERROR_CHECK(setup_port(PORT1, c->baud1, UART1_TX_PIN, UART1_RX_PIN));
    ESP_ERROR_CHECK(setup_port(PORT2, c->baud2, UART2_TX_PIN, UART2_RX_PIN));

    esp_err_t usb_err = setup_port(PORTA, 115200, USB_TX_PIN, USB_RX_PIN);
    if (usb_err != ESP_OK) {
        ESP_LOGW(TAG, "не удалось установить драйвер UART0 (USB): %s",
                 esp_err_to_name(usb_err));
    } else {
        s_usb_ready = true;
    }

    s_prev_auto[1] = c->auto_baud1;
    s_prev_auto[2] = c->auto_baud2;
    s_prev_baud[1] = c->baud1;
    s_prev_baud[2] = c->baud2;
    snprintf(s_prev_probe, sizeof(s_prev_probe), "%s", c->baud_probe);
    s_detect_pending[1] = c->auto_baud1;   /* опрос начнётся в задаче RX */
    s_detect_pending[2] = c->auto_baud2;

    xTaskCreate(uart_rx_task, "uart1_rx", 6144, (void *)(uintptr_t)1, 12, NULL);
    xTaskCreate(uart_rx_task, "uart2_rx", 6144, (void *)(uintptr_t)2, 12, NULL);
    if (usb_err == ESP_OK) {
        /* USB RX вызывает CLI -> event_bus -> format_message (глубокая цепочка snprintf) */
        xTaskCreate(uart_rx_task, "usb_rx", 10240, (void *)(uintptr_t)0, 12, NULL);
    }
    xTaskCreate(auto_task,   "auto_cmd", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "UART1 TX%d/RX%d @%d, UART2 TX%d/RX%d @%d, USB UART0 @115200",
             UART1_TX_PIN, UART1_RX_PIN, c->baud1,
             UART2_TX_PIN, UART2_RX_PIN, c->baud2);
}

bool uart_bridge_usb_ready(void)
{
    return s_usb_ready;
}

void uart_bridge_apply_config(void)
{
    const app_config_t *c = config_get();
    for (int ch = 1; ch <= 2; ch++) {
        uart_port_t port = (ch == 1) ? PORT1 : PORT2;
        int baud = (ch == 1) ? c->baud1 : c->baud2;
        bool autob = (ch == 1) ? c->auto_baud1 : c->auto_baud2;

        if (!autob) {
            s_detect_pending[ch] = false;
            s_detected_baud[ch] = 0;
            uart_set_baudrate(port, baud);
        } else if (!s_prev_auto[ch] || s_prev_baud[ch] != baud ||
                   strcmp(s_prev_probe, c->baud_probe)) {
            /* включили выключатель, сменили скорость или команды — переопределяем */
            s_detect_pending[ch] = true;
        }
        s_prev_auto[ch] = autob;
        s_prev_baud[ch] = baud;
    }
    snprintf(s_prev_probe, sizeof(s_prev_probe), "%s", c->baud_probe);
}

int uart_bridge_detected_baud(int ch)
{
    return (ch == 1 || ch == 2) ? s_detected_baud[ch] : 0;
}

void uart_bridge_send(int target, const uint8_t *data, size_t len)
{
    if (!data || !len) return;

    if (target == 1 || target == 3) {
        uart_write_bytes(PORT1, data, len);
        s_tx1 += len;
        event_bus_publish(EV_TO1, data, len);
        if (config_get()->mirror) mirror_to_usb("LOCAL > UART1", data, len);
    }
    if (target == 2 || target == 3) {
        uart_write_bytes(PORT2, data, len);
        s_tx2 += len;
        event_bus_publish(EV_TO2, data, len);
        if (config_get()->mirror) mirror_to_usb("LOCAL > UART2", data, len);
    }
    if (target == 0) {
        uart_write_bytes(PORTA, data, len);
    }
    mqtt_publish_sent(target, data, len);
}

void uart_bridge_send_str(int target, const char *str)
{
    if (str) uart_bridge_send(target, (const uint8_t *)str, strlen(str));
}

void uart_bridge_usb_print(const char *str)
{
    if (!str) return;
    char *conv = charset_convert(str);
    const char *out = conv ? conv : str;
    uart_write_bytes(PORTA, (const uint8_t *)out, strlen(out));
    free(conv);
}

void uart_bridge_counters(uint32_t *rx1, uint32_t *rx2,
                          uint32_t *tx1, uint32_t *tx2)
{
    if (rx1) *rx1 = s_rx1;
    if (rx2) *rx2 = s_rx2;
    if (tx1) *tx1 = s_tx1;
    if (tx2) *tx2 = s_tx2;
}

const char *uart_bridge_target_name(int t)
{
    return full_target(t);
}
