/*
 * isocd.c — minimal ISO9660 walker (PSP UMD images)
 *
 * Path: PVD (sector 16) -> root directory record -> root dir entries
 *       -> UMD_DATA.BIN -> game id.
 *
 * Layout notes (all little-endian LSB first):
 *   PVD:   type=1 at 0, "CD001" at 1, root dir record at offset 156.
 *   Dir record: len@0, extent u32@2, size u32@10, flags@25,
 *               name_len@32, name@33.
 *   Flags bit0 = file, bit1 = directory. Dot entries skipped.
 *
 * Verified against real PSP UMD dumps (Daxter: root extent 22,
 * UMD_DATA.BIN at extent 71, size 48).
 */

#include <string.h>

#include "isocd.h"

#define ISO_SECTOR 2048

/* Read one 2048-byte sector through the callback. */
static int read_sector(isocd_read_fn readfn, void *ctx,
                       unsigned int sector, unsigned char *out)
{
    return readfn(ctx, sector, out, 1);
}

/* Root directory record inside the PVD (sector 16, offset 156). */
static const unsigned char *pvd_root_record(const unsigned char *pvd)
{
    return pvd + 156;
}

static void rec_fields(const unsigned char *r,
                       unsigned int *extent, unsigned int *size,
                       int *is_dir, unsigned int *name_len,
                       const unsigned char **name)
{
    if (extent)
        *extent = r[2] | (r[3] << 8) | (r[4] << 16) | (r[5] << 24);
    if (size)
        *size = r[10] | (r[11] << 8) | (r[12] << 16) | (r[13] << 24);
    if (is_dir)
        *is_dir = (r[25] & 2) ? 1 : 0;
    if (name_len)
        *name_len = r[32];
    if (name)
        *name = r + 33;
}

static int name_eq(const unsigned char *name, unsigned int len,
                   const char *want)
{
    unsigned int i;
    for (i = 0; want[i]; i++) {
        if (i >= len) return 0;
        /* ISO9660 d-characters are upper-case ASCII; compare case-insensitive */
        unsigned char a = name[i];
        unsigned char b = (unsigned char)want[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    return want[i] == 0 ? 1 : 0;
}

/* Parse a directory (extent+size), find an entry by name,
 * fill *out extent/size/is_dir. Returns 1 if found. */
static int dir_find_entry(isocd_read_fn readfn, void *ctx,
                          unsigned int dir_extent, unsigned int dir_size,
                          const char *want_name,
                          unsigned int *out_extent, unsigned int *out_size,
                          int *out_is_dir)
{
    /* static: 16KB won't fit on a VSH plugin stack */
    static unsigned char buf[8 * ISO_SECTOR];
    unsigned int off = 0;
    unsigned int nsec = (dir_size + ISO_SECTOR - 1) / ISO_SECTOR;

    if (nsec > 8) nsec = 8; /* dirs are tiny on UMD; cap at 16KB */
    if (readfn(ctx, dir_extent, buf, nsec) != 0) return 0;

    while (off < dir_size && off < sizeof(buf)) {
        unsigned char l = buf[off];
        unsigned int extent, size, name_len;
        const unsigned char *name;
        int is_dir;

        if (l == 0) {
            /* end of sector pad */
            off = (off / ISO_SECTOR + 1) * ISO_SECTOR;
            continue;
        }
        if (l < 34) return 0; /* corrupt */

        rec_fields(buf + off, &extent, &size, &is_dir, &name_len, &name);

        if (name_len > 0 && name[0] != 0x00 && name[0] != 0x01) {
            if (name_eq(name, name_len, want_name)) {
                *out_extent = extent;
                *out_size = size;
                *out_is_dir = is_dir;
                return 1;
            }
        }
        off += l;
    }
    return 0;
}

int isocd_read_umd_data(isocd_read_fn readfn, void *ctx,
                        unsigned char *out, int maxlen)
{
    static unsigned char pvd[ISO_SECTOR];
    static unsigned char blob[8 * ISO_SECTOR];
    const unsigned char *root_rec;
    unsigned int root_extent = 0, root_size = 0;
    unsigned int umd_extent = 0, umd_size = 0;
    unsigned int dummy_extent = 0, dummy_size = 0;
    int dummy_is_dir = 0, umd_is_dir = 0;
    int n, i;

    if (readfn(ctx, 16, pvd, 1) != 0) return -1;
    if (pvd[0] != 1) return -1;              /* not a PVD */
    if (memcmp(pvd + 1, "CD001", 5) != 0) return -1;

    root_rec = pvd_root_record(pvd);
    rec_fields(root_rec, &root_extent, &root_size, &dummy_is_dir,
               NULL, NULL);

    if (!dir_find_entry(readfn, ctx,
                        root_extent, root_size,
                        "UMD_DATA.BIN",
                        &umd_extent, &umd_size, &umd_is_dir)) {
        return 0; /* no UMD_DATA.BIN */
    }

    if (umd_is_dir) return 0;
    n = (int)umd_size;
    if (n > (int)sizeof(blob)) n = (int)sizeof(blob);
    n = (n + ISO_SECTOR - 1) / ISO_SECTOR;

    if (readfn(ctx, umd_extent, blob, (unsigned int)n) != 0) return -1;
    if (umd_size > (unsigned int)maxlen) umd_size = maxlen;
    for (i = 0; i < (int)umd_size; i++) out[i] = blob[i];
    return (int)umd_size;
}

void isocd_extract_game_id(const unsigned char *blob, int len,
                           char *id, int maxlen)
{
    int i = 0;
    while (i < len && i < maxlen - 1 && blob[i] != '|' && blob[i] != 0) {
        id[i] = (char)blob[i];
        i++;
    }
    id[i] = 0;
}