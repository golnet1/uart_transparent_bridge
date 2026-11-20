#include "config.h"

#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "config";
static app_config_t s_cfg;
static bool s_loaded;

static void defaults(app_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->baud1 = 115200;
    c->baud2 = 9600;
    c->bridge = true;
    c->echo = false;
    c->mirror = true;
    c->syslog = false;
    c->usb_target = 3;          /* по умолчанию команды с USB идут на оба UART */
    c->auto_ms = 0;
    strcpy(c->auto_target, "1");
    strcpy(c->auto_text, "AT");
    strcpy(c->wifi_ssid, "");
    strcpy(c->wifi_pass, "");
    strcpy(c->ota_url, "");   /* legacy-поле: осталось в структуре для совместимости NVS */
    c->cmd_mode = 0;
    c->cmd_target = 3;
    strcpy(c->mqtt_uri, "");
    strcpy(c->mqtt_topic, "pass-through-uart");
    strcpy(c->mqtt_user, "");
    strcpy(c->mqtt_pass, "");
    strcpy(c->ntp_server, "pool.ntp.org");
    strcpy(c->ntp_tz, "MSK-3");
    c->auto_baud1 = false;
    c->auto_baud2 = false;
    strcpy(c->baud_probe, "AT|AT+GMR");
    c->auto_update = false;
}

void config_load(void)
{
    defaults(&s_cfg);
    s_loaded = true;

    nvs_handle_t h;
    esp_err_t err = nvs_open("uart_moyka", NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS нет сохранённой конфигурации, используем значения по умолчанию");
        return;
    }

    /* Новые поля добавляются в конец структуры: копируем столько, сколько
       сохранено, остальное остаётся от defaults(). Так не теряется Wi-Fi
       и другие настройки после обновления прошивки. */
    size_t len = sizeof(s_cfg);
    if (nvs_get_blob(h, "cfg", &s_cfg, &len) == ESP_OK && len <= sizeof(s_cfg) && len > 0) {
        /* защита от мусора в NVS */
        if (s_cfg.baud1 < 1200 || s_cfg.baud1 > 3000000) s_cfg.baud1 = 115200;
        if (s_cfg.baud2 < 1200 || s_cfg.baud2 > 3000000) s_cfg.baud2 = 9600;
        if (s_cfg.usb_target < 1 || s_cfg.usb_target > 3) s_cfg.usb_target = 3;
        if (s_cfg.cmd_mode < 0 || s_cfg.cmd_mode > 2) s_cfg.cmd_mode = 0;
        if (s_cfg.cmd_target < 1 || s_cfg.cmd_target > 3) s_cfg.cmd_target = 3;
        s_cfg.mqtt_uri[sizeof(s_cfg.mqtt_uri) - 1] = '\0';
        s_cfg.mqtt_topic[sizeof(s_cfg.mqtt_topic) - 1] = '\0';
        s_cfg.mqtt_user[sizeof(s_cfg.mqtt_user) - 1] = '\0';
        s_cfg.mqtt_pass[sizeof(s_cfg.mqtt_pass) - 1] = '\0';
        s_cfg.ntp_server[sizeof(s_cfg.ntp_server) - 1] = '\0';
        s_cfg.ntp_tz[sizeof(s_cfg.ntp_tz) - 1] = '\0';
        if (!s_cfg.ntp_server[0]) strcpy(s_cfg.ntp_server, "pool.ntp.org");
        if (!s_cfg.ntp_tz[0]) strcpy(s_cfg.ntp_tz, "MSK-3");
        s_cfg.wifi_ssid[sizeof(s_cfg.wifi_ssid) - 1] = '\0';
        s_cfg.wifi_pass[sizeof(s_cfg.wifi_pass) - 1] = '\0';
        s_cfg.ota_url[sizeof(s_cfg.ota_url) - 1] = '\0';
        s_cfg.auto_target[sizeof(s_cfg.auto_target) - 1] = '\0';
        s_cfg.auto_text[sizeof(s_cfg.auto_text) - 1] = '\0';
        s_cfg.baud_probe[sizeof(s_cfg.baud_probe) - 1] = '\0';
        if (!s_cfg.baud_probe[0]) strcpy(s_cfg.baud_probe, "AT");
        ESP_LOGI(TAG, "конфигурация загружена из NVS");
    }
    nvs_close(h);
}

void config_save(void)
{
    if (!s_loaded) return;
    nvs_handle_t h;
    esp_err_t err = nvs_open("uart_moyka", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "не удалось открыть NVS: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_blob(h, "cfg", &s_cfg, sizeof(s_cfg));
    if (err == ESP_OK) err = nvs_commit(h);
    if (err != ESP_OK) ESP_LOGW(TAG, "ошибка сохранения: %s", esp_err_to_name(err));
    nvs_close(h);
}

void config_reset(void)
{
    defaults(&s_cfg);
    config_save();
}

const app_config_t *config_get(void) { return &s_cfg; }
app_config_t       *config_mut(void) { return &s_cfg; }
