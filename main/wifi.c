#include "wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "lwip/ip4_addr.h"
#include "config.h"
#include "event_bus.h"

#define AP_PASS "12345678"

static const char *TAG = "wifi";
static char s_ip_ap[16] = "192.168.4.1";
static char s_ip_sta[16] = "";
static bool s_connected;
static bool s_ap_enabled = true;   /* STA подключён -> AP выключаем */
static bool s_ap_off_by_sta;

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)arg; (void)data;

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip_sta, sizeof(s_ip_sta), IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        char msg[96];
        snprintf(msg, sizeof(msg), "STA подключён, IP %s", s_ip_sta);
        event_bus_publish_str(EV_WIFI, msg);
        /* Точка доступа не нужна, когда есть подключение к другой сети */
        if (s_ap_enabled && esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK) {
            s_ap_enabled = false;
            s_ap_off_by_sta = true;
            event_bus_publish_str(EV_WIFI, "AP выключен (есть STA)");
            ESP_LOGI(TAG, "AP выключен, остался только STA");
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip_sta[0] = '\0';
        if (s_ap_off_by_sta && esp_wifi_set_mode(WIFI_MODE_APSTA) == ESP_OK) {
            s_ap_enabled = true;
            s_ap_off_by_sta = false;
            event_bus_publish_str(EV_WIFI, "AP включён повторно (STA отключён)");
            ESP_LOGI(TAG, "AP включён повторно");
        }
        event_bus_publish_str(EV_WIFI, "STA отключён, повторная попытка...");
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        event_bus_publish_str(EV_WIFI, "клиент подключён к точке доступа");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        event_bus_publish_str(EV_WIFI, "клиент отключён от точки доступа");
    }
}

void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    (void)ap; (void)sta;

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                       &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                       &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_ap_config_t ap_cfg = {
        .ssid = APP_SSID,
        .ssid_len = strlen(APP_SSID),
        .password = AP_PASS,
        .authmode = WIFI_AUTH_WPA2_PSK,
        .max_connection = 4,
        .channel = 1,
        .ssid_hidden = 0,
        .beacon_interval = 100,
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, (wifi_config_t *)&ap_cfg));

    const app_config_t *c = config_get();
    if (c->wifi_ssid[0]) {
        wifi_sta_config_t sta_cfg;
        memset(&sta_cfg, 0, sizeof(sta_cfg));
        snprintf((char *)sta_cfg.ssid, sizeof(sta_cfg.ssid), "%s", c->wifi_ssid);
        snprintf((char *)sta_cfg.password, sizeof(sta_cfg.password), "%s", c->wifi_pass);
        sta_cfg.threshold.authmode = WIFI_AUTH_WPA_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, (wifi_config_t *)&sta_cfg));
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    if (c->wifi_ssid[0]) esp_wifi_connect();

    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &info) == ESP_OK) {
        snprintf(s_ip_ap, sizeof(s_ip_ap), IPSTR, IP2STR(&info.ip));
    }
    ESP_LOGI(TAG, "AP: %s / %s, IP %s", APP_SSID, AP_PASS, s_ip_ap);
}

void wifi_reconnect(void)
{
    const app_config_t *c = config_get();
    if (!c->wifi_ssid[0]) return;

    wifi_sta_config_t sta_cfg;
    memset(&sta_cfg, 0, sizeof(sta_cfg));
    snprintf((char *)sta_cfg.ssid, sizeof(sta_cfg.ssid), "%s", c->wifi_ssid);
    snprintf((char *)sta_cfg.password, sizeof(sta_cfg.password), "%s", c->wifi_pass);
    sta_cfg.threshold.authmode = WIFI_AUTH_WPA_PSK;
    esp_wifi_set_config(WIFI_IF_STA, (wifi_config_t *)&sta_cfg);
    esp_wifi_connect();
}

bool wifi_sta_connected(void) { return s_connected; }
bool wifi_ap_enabled(void)     { return s_ap_enabled; }

int wifi_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
    return 0;
}

const char *wifi_ip_ap(void) { return s_ip_ap; }
const char *wifi_ip_sta(void) { return s_ip_sta; }

int wifi_scan(wifi_ap_t *out, int max)
{
    if (!out || max <= 0) return 0;

    wifi_scan_config_t cfg = {
        .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = false,
    };
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK) return 0;

    uint16_t found = max > 16 ? 16 : (uint16_t)max;
    wifi_ap_record_t aps[16];
    if (esp_wifi_scan_get_ap_records(&found, aps) != ESP_OK) return 0;

    int n = 0;
    for (int i = 0; i < found && n < max; i++) {
        snprintf(out[n].ssid, sizeof(out[n].ssid), "%s", (const char *)aps[i].ssid);
        out[n].rssi = aps[i].rssi;
        out[n].channel = aps[i].primary;
        out[n].auth = aps[i].authmode != WIFI_AUTH_OPEN;
        n++;
    }
    return n;
}
