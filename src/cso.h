/*
 * cso.h — compressed ISO (.cso) sector reader for PSP
 *
 * Thin zlib wrapper exposing the same sector callback interface as
 * isocd.h, so the ISO9660 walker works on CSO images too.
 */

#ifndef CSO_H
#define CSO_H

#define CSO_BLOCK 2048

typedef struct {
    unsigned int plain_size;      /* uncompressed ISO size in bytes */
    unsigned int block_size;      /* bytes per block (usually 2048)*/
    unsigned int block_count;
    void *fh;                     /* SceUID stored as pointer-sized */
} cso_ctx_t;

/* Open a CSO. Returns 0 ok, negative on error. */
int cso_open(cso_ctx_t *c, const char *path);

void cso_close(cso_ctx_t *c);

/* isocd_read_fn compatible */
int cso_read_sectors(void *ctx, unsigned int sector,
                     unsigned char *out, unsigned int n);

#endif /* CSO_H */