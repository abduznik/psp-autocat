/*
 * cso.c — compressed ISO (.cso) sector reader
 *
 * CSO layout:
 *   header (0x18 bytes): magic "CISO", header_size,
 *                        plain_size (u64, low 32 used), block_size (u32)
 *   index table: (block_count + 1) x u32. index[i] = absolute offset of
 *                compressed block i; high bit set => block stored plain.
 *   block data: zlib streams of block_size bytes each.
 *
 * Index entries are read ON DEMAND (seek per block) so no large heap
 * allocation is needed on the PSP. Blocks are inflated into a single
 * reusable malloc'd buffer.
 */

#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "cso.h"

#define CSO_MAGIC 0x4F534943u /* "CISO" LE */
#define HEADER_SIZE 0x18

static unsigned int rd32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

/* Read one index entry (4 bytes) at logical position i. */
static int cso_index_entry(cso_ctx_t *c, unsigned int i,
                           unsigned int *out)
{
    unsigned char b[4];
    if (sceIoLseek((SceUID)c->fh, HEADER_SIZE + (SceOff)i * 4,
                   PSP_SEEK_SET) < 0)
        return -1;
    if (sceIoRead((SceUID)c->fh, b, 4) != 4) return -1;
    *out = rd32(b);
    return 0;
}

int cso_open(cso_ctx_t *c, const char *path)
{
    unsigned char hdr[HEADER_SIZE];
    SceUID fd;

    memset(c, 0, sizeof(*c));
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;

    if (sceIoRead(fd, hdr, sizeof(hdr)) != (int)sizeof(hdr) ||
        rd32(hdr) != CSO_MAGIC) {
        sceIoClose(fd);
        return -1;
    }

    c->plain_size = rd32(hdr + 8);
    /* block_size field default */
    c->block_size = rd32(hdr + 16);
    if (c->block_size == 0) c->block_size = CSO_BLOCK;

    c->block_count =
        (c->plain_size + c->block_size - 1) / c->block_size;
    c->fh = (void *)fd;
    return 0;
}

void cso_close(cso_ctx_t *c)
{
    if (c->fh) sceIoClose((SceUID)c->fh);
    memset(c, 0, sizeof(*c));
}
/* Inflate CSO block i into out (out must hold block_size bytes). */
static int cso_inflate_block(cso_ctx_t *c, unsigned int i,
                             unsigned char *out)
{
    uLongf dest_len = (uLongf)c->block_size;
    unsigned int off, next, comp_len;
    unsigned char *comp;
    int ret;

    if (cso_index_entry(c, i, &off) != 0) return -1;
    if (cso_index_entry(c, i + 1, &next) != 0) return -1;

    if (off & 0x80000000u) {
        /* stored uncompressed */
        unsigned int base = off & 0x7FFFFFFFu;
        if (sceIoLseek((SceUID)c->fh, base, PSP_SEEK_SET) < 0) return -1;
        if (sceIoRead((SceUID)c->fh, out, c->block_size) !=
            (int)c->block_size)
            return -1;
        return 0;
    }

    comp_len = next - off;
    if (comp_len > 16 * 1024 * 1024) return -1; /* sanity */
    comp = (unsigned char *)malloc(comp_len ? comp_len : 1);
    if (!comp) return -1;
    if (sceIoLseek((SceUID)c->fh, off, PSP_SEEK_SET) < 0) {
        free(comp);
        return -1;
    }
    if (sceIoRead((SceUID)c->fh, comp, comp_len) != (int)comp_len) {
        free(comp);
        return -1;
    }
    ret = uncompress(out, &dest_len, comp, comp_len);
    free(comp);
    return ret == Z_OK ? 0 : -1;
}

int cso_read_sectors(void *ctx, unsigned int sector,
                     unsigned char *out, unsigned int n)
{
    cso_ctx_t *c = (cso_ctx_t *)ctx;
    unsigned char *block = NULL;
    unsigned int i, j;
    int r = 0;

    block = (unsigned char *)malloc(c->block_size);
    if (!block) return -1;

    for (i = 0; i < n; i++) {
        unsigned long long abs = (unsigned long long)(sector + i) * CSO_BLOCK;
        unsigned int block_idx;
        unsigned int in_block;

        if (abs >= c->plain_size) { r = -1; break; }

        block_idx = (unsigned int)(abs / c->block_size);
        in_block = (unsigned int)(abs % c->block_size);

        if (cso_inflate_block(c, block_idx, block) != 0) { r = -1; break; }

        for (j = 0; j < CSO_BLOCK; j++) {
            unsigned long long src = (unsigned long long)block_idx *
                                         c->block_size +
                                     in_block + j;
            if (src < c->plain_size) {
                out[i * CSO_BLOCK + j] = block[in_block + j];
            }
        }
    }

    free(block);
    return r;
}