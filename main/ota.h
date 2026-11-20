#pragma once

#include "app.h"

void ota_init(void);
void ota_start(const char *url);
bool ota_busy(void);
const char *ota_last_result(void);
