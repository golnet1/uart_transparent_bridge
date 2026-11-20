#include "charset.h"

#include <stdlib.h>
#include <string.h>

#include "nvs_flash.h"

/* По умолчанию CP866: пользовательский терминал Windows показывает
   приходящий UTF-8 как "╨║╨╛╨╝╨░╨╜╨┤╤Л". Переводим вывод в CP866. */
static int s_cp866 = 1;

void charset_init(void)
{
    nvs_handle_t h;
    if (nvs_open("uart_moyka", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 1;
        if (nvs_get_u8(h, "charset", &v) == ESP_OK) s_cp866 = v ? 1 : 0;
        nvs_close(h);
    }
}

int charset_get(void)
{
    return s_cp866;
}

void charset_set(int cp866)
{
    s_cp866 = cp866 ? 1 : 0;
    nvs_handle_t h;
    if (nvs_open("uart_moyka", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "charset", (uint8_t)s_cp866);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* Декодирование одного символа UTF-8. Не читает за завершающий '\0'. */
static uint32_t utf8_next(const unsigned char **pp)
{
    const unsigned char *p = *pp;
    unsigned char c = p[0];

    if (c < 0x80) { *pp = p + 1; return c; }

    if ((c & 0xE0) == 0xC0 && p[1] && (p[1] & 0xC0) == 0x80) {
        *pp = p + 2;
        return ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && p[1] && p[2] &&
        (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *pp = p + 3;
        return ((uint32_t)(c & 0x0F) << 12) |
               ((uint32_t)(p[1] & 0x3F) << 6) | (uint32_t)(p[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && p[1] && p[2] && p[3] &&
        (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        *pp = p + 4;
        return ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
               ((uint32_t)(p[2] & 0x3F) << 6) | (uint32_t)(p[3] & 0x3F);
    }

    *pp = p + 1;
    return 0xFFFD;   /* битый байт */
}

static uint8_t to_cp866(uint32_t u)
{
    if (u < 0x80) return (uint8_t)u;

    /* А..Я: U+0410..U+042F -> 0x80..0x9F */
    if (u >= 0x0410 && u <= 0x042F) return (uint8_t)(0x80 + (u - 0x0410));
    if (u == 0x0401) return 0xF0;   /* Ё */

    /* а..я: а..п = A0..AF, р..я = E0..EF */
    if (u >= 0x0430 && u <= 0x044F) {
        uint32_t i = u - 0x0430;
        return (uint8_t)(i < 0x10 ? 0xA0 + i : 0xE0 + (i - 0x10));
    }
    if (u == 0x0451) return 0xF1;   /* ё */

    if (u == 0x0404) return 0xF2;   /* Є */
    if (u == 0x0454) return 0xF3;   /* є */
    if (u == 0x0406) return 0xF4;   /* Ї */
    if (u == 0x0456) return 0xF5;   /* ї */
    if (u == 0x040E) return 0xF6;   /* Ў */
    if (u == 0x045E) return 0xF7;   /* ў */

    if (u == 0x00B0) return 0xF8;   /* ° */
    if (u == 0x2219) return 0xF9;   /* ∙ */
    if (u == 0x00B7) return 0xFA;   /* · */
    if (u == 0x221A) return 0xFB;   /* √ */
    if (u == 0x2116) return 0xFC;   /* № */
    if (u == 0x00A4) return 0xFD;   /* ¤ */
    if (u == 0x25A0) return 0xFE;   /* ■ */
    if (u == 0x00A0) return 0xFF;   /* неразрывный пробел */

    if (u == 0x2014 || u == 0x2013 || u == 0x2010) return '-';
    if (u == 0x2026) return '.';
    if (u == 0x2018 || u == 0x2019 || u == 0x201C || u == 0x201D) return '"';
    if (u == 0x20AC) return '$';

    return '?';
}

char *charset_convert(const char *src)
{
    if (!src) return NULL;
    if (!s_cp866) return strdup(src);

    size_t cap = strlen(src);
    char *out = malloc(cap + 1);
    if (!out) return NULL;

    const unsigned char *p = (const unsigned char *)src;
    size_t n = 0;
    while (*p && n < cap) {
        uint32_t u = utf8_next(&p);
        out[n++] = (char)to_cp866(u);
    }
    out[n] = '\0';
    return out;
}
