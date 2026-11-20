#pragma once

#include "app.h"

void event_bus_init(void);
void event_bus_subscribe(QueueHandle_t q, uint32_t mask);
void event_bus_unsubscribe(QueueHandle_t q);

/* data may be NULL, then len is ignored */
void event_bus_publish(ev_type_t type, const uint8_t *data, size_t len);
void event_bus_publish_str(ev_type_t type, const char *str);

/* Возвращает malloc-строку (последние n строк, каждая с \n). Вызывающий free(). */
char *event_bus_log_get(int n);
int   event_bus_log_lines(void);
void  event_bus_log_clear(void);
