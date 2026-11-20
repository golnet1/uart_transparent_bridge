#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void mqtt_init(void);
void mqtt_apply_config(void);
bool mqtt_connected(void);
int  mqtt_target(void);                                          /* текущая цель для <base>/code */
void mqtt_publish_sent(int target, const uint8_t *data, size_t len); /* <base>/tx/code + /tx/target */
void mqtt_normalize_uri(const char *src, char *dst, size_t cap); /* host[:port] → mqtt://host:port */
