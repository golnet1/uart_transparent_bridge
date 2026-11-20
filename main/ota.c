#include "ota.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "event_bus.h"
#include "config.h"
#include "wifi.h"

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

static const char *TAG = "ota";
static volatile bool s_busy;
static volatile bool s_checking;
static char s_last[160] = "не выполнялась";
static char s_remote[24] = "";
static volatile bool s_available;

static esp_ota_handle_t s_stream;
static const esp_partition_t *s_stream_part;

static void ota_fail(esp_err_t err)
{
    snprintf(s_last, sizeof(s_last), "ошибка: %s", esp_err_to_name(err));
    char msg[192];
    snprintf(msg, sizeof(msg), "OTA ошибка: %s", esp_err_to_name(err));
    event_bus_publish_str(EV_OTA, msg);
    ESP_LOGE(TAG, "%s", msg);
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

/* ---------- Загрузка по HTTPS (обновление с GitHub) ---------- */

#define OTA_HTTP_ATTEMPTS 3
#define OTA_HTTP_TIMEOUT_MS 20000

static void ota_task(void *arg)
{
    char *url = (char *)arg;
    s_busy = true;
    snprintf(s_last, sizeof(s_last), "загрузка...");
    event_bus_publish_str(EV_OTA, "OTA: начало обновления");

    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= OTA_HTTP_ATTEMPTS; attempt++) {
        /* На всякий случай ждём STA — без IP HTTPS-запрос оборвётся сразу. */
        int waited = 0;
        while (!wifi_sta_connected() && waited < 20) {
            vTaskDelay(pdMS_TO_TICKS(500));
            waited += 500;
        }
        if (!wifi_sta_connected()) {
            err = ESP_ERR_WIFI_NOT_CONNECT;
            event_bus_publish_str(EV_OTA, "OTA: нет STA, повторим");
        } else {
            esp_http_client_config_t http_cfg = {
                .url = url,
                .timeout_ms = OTA_HTTP_TIMEOUT_MS,
                .keep_alive_enable = false,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
                .crt_bundle_attach = esp_crt_bundle_attach,
#endif
            };
            esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };
            err = esp_https_ota(&ota_cfg);
        }

        if (err == ESP_OK) break;
        if (attempt < OTA_HTTP_ATTEMPTS) {
            char msg[128];
            snprintf(msg, sizeof(msg), "OTA: повторная попытка %d/%d: %s",
                     attempt + 1, OTA_HTTP_ATTEMPTS, esp_err_to_name(err));
            event_bus_publish_str(EV_OTA, msg);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }

    if (err == ESP_OK) {
        snprintf(s_last, sizeof(s_last), "успешно, перезагрузка");
        event_bus_publish_str(EV_OTA, "OTA: прошивка установлена, перезагрузка");
        xTaskCreate(reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
    } else {
        ota_fail(err);
        s_busy = false;
    }
    free(url);
    vTaskDelete(NULL);
}

void ota_start_url(const char *url)
{
    if (!url || !*url) return;
    if (s_busy) {
        event_bus_publish_str(EV_OTA, "OTA уже выполняется");
        return;
    }
    char *copy = strdup(url);
    if (!copy) return;
    s_busy = true;
    xTaskCreate(ota_task, "ota", 16384, copy, 5, NULL);
}

void ota_start_remote_update(void)
{
    ota_start_url(OTA_FIRMWARE_URL);
}

/* ---------- Загрузка локального файла из браузера ---------- */

esp_err_t ota_stream_begin(void)
{
    if (s_busy) return ESP_ERR_INVALID_STATE;
    s_stream_part = esp_ota_get_next_update_partition(NULL);
    if (!s_stream_part) return ESP_ERR_NOT_FOUND;

    esp_err_t err = esp_ota_begin(s_stream_part, OTA_SIZE_UNKNOWN, &s_stream);
    if (err != ESP_OK) return err;

    s_busy = true;
    snprintf(s_last, sizeof(s_last), "загрузка...");
    event_bus_publish_str(EV_OTA, "OTA: начало обновления");
    return ESP_OK;
}

esp_err_t ota_stream_write(const uint8_t *data, size_t len)
{
    if (!s_busy || !s_stream || !data || !len) return ESP_ERR_INVALID_STATE;
    return esp_ota_write(s_stream, data, len);
}

esp_err_t ota_stream_finish(void)
{
    if (!s_busy || !s_stream) return ESP_ERR_INVALID_STATE;

    esp_err_t err = esp_ota_end(s_stream);
    s_stream = 0;
    if (err != ESP_OK) {
        ota_fail(err);
        s_busy = false;
        return err;
    }
    err = esp_ota_set_boot_partition(s_stream_part);
    if (err != ESP_OK) {
        ota_fail(err);
        s_busy = false;
        return err;
    }
    snprintf(s_last, sizeof(s_last), "успешно, перезагрузка");
    event_bus_publish_str(EV_OTA, "OTA: прошивка установлена, перезагрузка");
    xTaskCreate(reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
    return ESP_OK;   /* s_busy остаётся true до перезагрузки */
}

void ota_stream_abort(void)
{
    if (s_stream) {
        esp_ota_abort(s_stream);
        s_stream = 0;
    }
    s_busy = false;
}

/* ---------- Проверка версии на GitHub ---------- */

static bool fetch_remote_version(char *out, size_t cap)
{
    esp_http_client_config_t cfg = {
        .url = OTA_VERSION_URL,
        .timeout_ms = 10000,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK &&
        esp_http_client_fetch_headers(client) > 0 &&
        esp_http_client_get_status_code(client) == 200) {
        char buf[96];
        int n = esp_http_client_read(client, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            char *p = buf;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            char *e = p + strlen(p);
            while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
            bool valid = *p != '\0';
            for (char *q = p; *q; q++) {
                if ((*q < '0' || *q > '9') && *q != '.') { valid = false; break; }
            }
            if (valid && strlen(p) < cap) {
                strcpy(out, p);
                ok = true;
            }
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

static int version_cmp(const char *a, const char *b)
{
    int va[4] = {0, 0, 0, 0}, vb[4] = {0, 0, 0, 0};
    sscanf(a, "%d.%d.%d.%d", &va[0], &va[1], &va[2], &va[3]);
    sscanf(b, "%d.%d.%d.%d", &vb[0], &vb[1], &vb[2], &vb[3]);
    for (int i = 0; i < 4; i++) {
        if (va[i] > vb[i]) return 1;
        if (va[i] < vb[i]) return -1;
    }
    return 0;
}

static void check_version(void)
{
    char ver[24];
    if (!fetch_remote_version(ver, sizeof(ver))) {
        event_bus_publish_str(EV_OTA, "не удалось проверить версию прошивки");
        return;
    }
    snprintf(s_remote, sizeof(s_remote), "%s", ver);
    bool available = version_cmp(ver, APP_VERSION) > 0;
    s_available = available;

    char msg[160];
    if (available) {
        snprintf(msg, sizeof(msg), "доступна прошивка v%s (сейчас v%s)", ver, APP_VERSION);
        event_bus_publish_str(EV_OTA, msg);
        if (config_get()->auto_update) {
            event_bus_publish_str(EV_OTA, "автообновление прошивки");
            ota_start_remote_update();
        }
    } else {
        snprintf(msg, sizeof(msg), "проверка версии: актуальна v%s", ver);
        event_bus_publish_str(EV_OTA, msg);
    }
}

static void check_loop_task(void *arg)
{
    (void)arg;
    while (1) {
        while (!wifi_sta_connected()) vTaskDelay(pdMS_TO_TICKS(5000));
        check_version();
        vTaskDelay(pdMS_TO_TICKS(24 * 60 * 60 * 1000));
    }
}

static void check_once_task(void *arg)
{
    (void)arg;
    check_version();
    s_checking = false;
    vTaskDelete(NULL);
}

void ota_check_now(void)
{
    if (s_checking) return;
    s_checking = true;
    if (xTaskCreate(check_once_task, "ota_check", 10240, NULL, 5, NULL) != pdPASS)
        s_checking = false;
}

void ota_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        esp_ota_img_states_t state;
        if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
            if (state == ESP_OTA_IMG_PENDING_VERIFY) {
                esp_ota_mark_app_valid_cancel_rollback();
                event_bus_publish_str(EV_OTA, "OTA: прошивка подтверждена");
            }
        }
    }
    xTaskCreate(check_loop_task, "ota_daily", 10240, NULL, 5, NULL);
}

bool ota_busy(void) { return s_busy; }
const char *ota_last_result(void) { return s_last; }
bool ota_update_available(void) { return s_available; }
const char *ota_remote_version(void) { return s_remote; }
