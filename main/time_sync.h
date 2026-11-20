#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void  time_sync_init(void);
void  time_sync_apply(void);
bool  time_sync_synced(void);
int64_t time_sync_epoch(void);          /* секунды Unix, 0 если время не синхронизировано */
int   time_sync_offset_min(void);       /* смещение локального времени от UTC в минутах */
void  time_sync_format(char *buf, size_t cap);  /* "dd.mm.yyyy HH:MM:SS" */
void  time_format_uptime(char *buf, size_t cap, uint32_t seconds); /* с → мин/с → ч/мин → д/ч/мин */
