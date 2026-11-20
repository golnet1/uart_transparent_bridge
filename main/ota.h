#pragma once

#include "app.h"
#include "esp_err.h"

/* Прошивка и версия лежат в публичном репозитории. Указанный в адресе путь
   .../tree/main/build соответствует raw-ссылке на PassThroughUART.bin. */
#define OTA_VERSION_URL  "https://raw.githubusercontent.com/golnet1/uart_transparent_bridge/main/version.txt"
#define OTA_FIRMWARE_URL "https://raw.githubusercontent.com/golnet1/uart_transparent_bridge/main/build/PassThroughUART.bin"

void ota_init(void);
void ota_start_url(const char *url);
void ota_start_remote_update(void);
void ota_check_now(void);

bool ota_busy(void);
const char *ota_last_result(void);
bool ota_update_available(void);
const char *ota_remote_version(void);

/* Загрузка файла из веба (POST /api/ota/file) напрямую в OTA-слот. */
esp_err_t ota_stream_begin(void);
esp_err_t ota_stream_write(const uint8_t *data, size_t len);
esp_err_t ota_stream_finish(void);
void ota_stream_abort(void);
