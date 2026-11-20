#include <stdarg.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "app.h"
#include "config.h"
#include "event_bus.h"
#include "uart_bridge.h"
#include "cli.h"
#include "charset.h"
#include "auth.h"
#include "wifi.h"
#include "web.h"
#include "telnet.h"
#include "ota.h"
#include "mqtt_channels.h"
#include "time_sync.h"

static const char *TAG = "main";

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
static int app_log_vprintf(const char *fmt, va_list ap)
#else
static void app_log_vprintf(const char *fmt, va_list ap)
#endif
{
    char buf[768];
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    event_bus_publish_str(EV_SYS, buf);
    return n;
#else
    vsnprintf(buf, sizeof(buf), fmt, ap);
    event_bus_publish_str(EV_SYS, buf);
#endif
}

void app_main(void)
{
    /* NVS для конфигурации */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    event_bus_init();
    config_load();
    charset_init();
    auth_init();
    esp_log_set_vprintf(app_log_vprintf);

    ESP_LOGI(TAG, "запуск %s v%s", APP_NAME, APP_VERSION);

    uart_bridge_init();
    cli_init_usb();
    wifi_init();
    time_sync_init();
    web_init();
    telnet_init();
    ota_init();
    mqtt_init();

    char banner[256];
    snprintf(banner, sizeof(banner),
             "\r\n=== %s v%s ===\r\n"
             "Web : http://%s  (AP %s)\r\n"
             "Telnet: port 23\r\n"
             "UART1 %d bd <-> UART2 %d bd, USB-UART0 115200 bd\r\n"
             "'/help' - команды, '/' - префикс локальной команды\r\n> ",
             APP_NAME, APP_VERSION,
             wifi_sta_connected() ? wifi_ip_sta() : wifi_ip_ap(),
             wifi_ip_ap(), config_get()->baud1, config_get()->baud2);
    uart_bridge_usb_print(banner);

    char ready[96];
    snprintf(ready, sizeof(ready), "%s готов к работе", APP_NAME);
    event_bus_publish_str(EV_SYS, ready);
}
