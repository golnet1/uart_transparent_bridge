#include "ota.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include "event_bus.h"

static const char *TAG = "ota";
static volatile bool s_busy;
static char s_last[160] = "не выполнялась";

static void ota_task(void *arg)
{
    char *url = (char *)arg;
    s_busy = true;
    snprintf(s_last, sizeof(s_last), "загрузка...");
    event_bus_publish_str(EV_OTA, "OTA: начало обновления");

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        snprintf(s_last, sizeof(s_last), "успешно, перезагрузка");
        event_bus_publish_str(EV_OTA, "OTA: прошивка установлена, перезагрузка");
        vTaskDelay(pdMS_TO_TICKS(1200));
        esp_restart();
    } else {
        snprintf(s_last, sizeof(s_last), "ошибка: %s", esp_err_to_name(err));
        char msg[192];
        snprintf(msg, sizeof(msg), "OTA ошибка: %s", esp_err_to_name(err));
        event_bus_publish_str(EV_OTA, msg);
        ESP_LOGE(TAG, "%s", msg);
    }

    s_busy = false;
    free(url);
    vTaskDelete(NULL);
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
}

void ota_start(const char *url)
{
    if (!url || !*url) return;
    if (s_busy) {
        event_bus_publish_str(EV_OTA, "OTA уже выполняется");
        return;
    }
    char *copy = strdup(url);
    if (!copy) return;
    s_busy = true;
    xTaskCreate(ota_task, "ota", 8192, copy, 5, NULL);
}

bool ota_busy(void) { return s_busy; }
const char *ota_last_result(void) { return s_last; }
