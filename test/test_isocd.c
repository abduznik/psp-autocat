/*
 * test_isocd.c — host unit test for the ISO9660 UMD_DATA.BIN walker.
 * Builds a synthetic ISO image in memory (PVD + root dir + UMD_DATA.BIN)
 * and checks extraction. Runs on plain gcc, no PSP SDK.
 */

#include <stdio.h>
#include <string.h>

#include "isocd.h"
#include "classify.h"

static int failures = 0;

static void check(int cond, const char *label)
{
    if (cond) {
        printf("PASS %s\n", label);
    } else {
        printf("FAIL %s\n", label);
        failures++;
    }
}

/* Synthetic ISO image: 64 sectors of zeros,
 * PVD at sector 16, root dir at sector 20, UMD_DATA.BIN at 21. */
static unsigned char img[64 * 2048];

typedef struct { void *img; } mem_ctx;

static int mem_sector_read(void *ctx, unsigned int sector,
                          unsigned char *out, unsigned int n)
{
    mem_ctx *m = (mem_ctx *)ctx;
    unsigned char *base = (unsigned char *)m->img;
    memcpy(out, base + sector * 2048, n * 2048);
    return 0;
}

static void set_u32(unsigned char *p, unsigned int v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

/* Write an ISO9660 dir record at *pos; advances *pos by rec len. */
static void put_dir_record(unsigned char *slot, unsigned int *pos,
                           unsigned int extent, unsigned int size,
                           int is_dir, const char *name)
{
    unsigned int nl = strlen(name);
    unsigned int rec_len = 33 + nl + ((33 + nl) & 1); /* even pad */
    unsigned char *r = slot + *pos;

    r[0] = (unsigned char)rec_len;      /* record length */
    set_u32(r + 2, extent);             /* extent LSB */
    set_u32(r + 10, size);              /* data length LSB */
    r[25] = (unsigned char)(is_dir ? 2 : 0);
    r[32] = (unsigned char)nl;
    memcpy(r + 33, name, nl);
    *pos += rec_len;
}

static void build_image(void)
{
    unsigned int pos;

    memset(img, 0, sizeof(img));

    /* PVD at sector 16 */
    img[16 * 2048] = 1;                     /* type PVD */
    memcpy(img + 16 * 2048 + 1, "CD001", 5);
    img[16 * 2048 + 6] = 1;                 /* version */
    /* root dir record at PVD+156 */
    put_dir_record(img + 16 * 2048, &(unsigned int){156}, 20, 2048, 1, "\x00");

    /* root dir at sector 20: ".", "..", UMD_DATA.BIN — contiguous */
    pos = 0;
    put_dir_record(img + 20 * 2048, &pos, 20, 2048, 1, "\x00");
    put_dir_record(img + 20 * 2048, &pos, 20, 2048, 1, "\x01");
    put_dir_record(img + 20 * 2048, &pos, 21, 48, 0, "UMD_DATA.BIN;1");

    /* UMD_DATA.BIN content at sector 21 */
    memcpy(img + 21 * 2048, "UCUS-98618|CA818654A4441431|0001|G", 34);
}

int main(void)
{
    mem_ctx ctx = { img };
    unsigned char umd[256];
    char id[32];
    int n;

    build_image();

    n = isocd_read_umd_data(mem_sector_read, &ctx, umd, sizeof(umd));
    check(n == 48, "UMD_DATA.BIN read 48 bytes");

    isocd_extract_game_id(umd, n, id, sizeof(id));
    check(strcmp(id, "UCUS-98618") == 0, "game id UCUS-98618");

    /* classification via the same rules the plugin uses */
    check(classify_game("UG", "UCUS-98618", "") == AC_PSP, "UCUS -> PSP");
    check(classify_game("", "SLUS00875", "") == AC_PS1, "SLUS -> PS1");
    /* POP-FE conversion stamps ME -> PS1 */
    check(classify_game("ME", "SLPS01556", "PEPSIMAN") == AC_PS1, "ME -> PS1");

    printf("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}