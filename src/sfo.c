/*
 * sfo.c — minimal PARAM.SFO parser
 *
 * Format: PSF header (20 bytes) + N x 16-byte entries.
 * String keys are NUL-terminated; data lives in a table.
 * Pure C, no PSP dependencies — unit-testable on the host.
 *
 * Reference: https://www.psdevwiki.com/psp/PARAM.SFO
 */

#include <string.h>

#include "sfo.h"

#define SFO_MAGIC 0x46535000u /* 00 50 53 46 ("PSF" LE) — verified against real eboots */

#define KEY_TABLE_OFF 8
#define DATA_TABLE_OFF 12
#define ENTRY_COUNT_OFF 16

int sfo_parse(const unsigned char *buf, unsigned int size,
              sfo_entry_t *out, int max_entries)
{
    unsigned int key_table, data_table, count;
    unsigned int i;

    if (size < 20) return -1;

    /* magic is a little-endian u32: bytes are 00 50 53 46 */
    if (buf[0] != 0x00 || buf[1] != 'P' || buf[2] != 'S' || buf[3] != 'F')
        return -1;

    /* key_table_start, data_table_start, entries */
    key_table = buf[8] | (buf[9] << 8) | (buf[10] << 16) | (buf[11] << 24);
    data_table = buf[12] | (buf[13] << 8) | (buf[14] << 16) | (buf[15] << 24);
    count = buf[16] | (buf[17] << 8) | (buf[18] << 16) | (buf[19] << 24);

    if (count > (unsigned int)max_entries) count = (unsigned int)max_entries;

    for (i = 0; i < count; i++) {
        unsigned int p = 20 + i * 16;
        unsigned int key_off, fmt, len_, max_len, data_off;
        sfo_entry_t *e = &out[i];

        if (p + 16 > size) { count = i; break; }

        key_off = buf[p] | (buf[p + 1] << 8);
        fmt = buf[p + 2] | (buf[p + 3] << 8);
        len_ = buf[p + 4] | (buf[p + 5] << 8) |
               (buf[p + 6] << 16) | (buf[p + 7] << 24);
        max_len = buf[p + 8] | (buf[p + 9] << 8) |
                  (buf[p + 10] << 16) | (buf[p + 11] << 24);
        data_off = buf[p + 12] | (buf[p + 13] << 8) |
                   (buf[p + 14] << 16) | (buf[p + 15] << 24);

        e->key = (const char *)buf + key_table + key_off;
        e->fmt = fmt;
        e->len = len_;
        e->max_len = max_len;
        e->data_off = data_table + data_off;
    }
    return (int)count;
}

const char *sfo_get_str(const unsigned char *buf, const sfo_entry_t *e)
{
    if (e->fmt != 0x0204) return "";
    return (const char *)buf + e->data_off;
}

unsigned int sfo_get_int(const unsigned char *buf, const sfo_entry_t *e)
{
    unsigned int v = 0;
    if (e->fmt != 0x0404) return 0;
    v = buf[e->data_off] | (buf[e->data_off + 1] << 8) |
        (buf[e->data_off + 2] << 16) | (buf[e->data_off + 3] << 24);
    return v;
}