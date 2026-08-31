/*
 * isocd.h — minimal ISO9660 walker for PSP UMD images
 *
 * Pure C, no PSP dependencies. Reads sectors through a callback so the
 * same walker works on plain .iso files and .cso (compressed) images.
 * Its only job here: find UMD_DATA.BIN in the root directory — that
 * 48-byte file carries the game id ("UCUS-98618|...").
 */

#ifndef ISOCD_H
#define ISOCD_H

/* Sector read callback. Returns 0 on success, nonzero on error.
 * ctx is passed through. n is number of 2048-byte sectors. */
typedef int (*isocd_read_fn)(void *ctx, unsigned int sector,
                             unsigned char *out, unsigned int n);

/* Walk the image: read PVD (sector 16), root dir, find UMD_DATA.BIN,
 * copy its raw content (up to maxlen bytes) into out. Returns bytes
 * read (>0) on success, 0 if not found, negative on error. */
int isocd_read_umd_data(isocd_read_fn readfn, void *ctx,
                        unsigned char *out, int maxlen);

/* Extract the game id from a UMD_DATA.BIN blob ("UCUS-98618|...|..").
 * Copies the first pipe-delimited field into id (maxlen incl NUL). */
void isocd_extract_game_id(const unsigned char *blob, int len,
                           char *id, int maxlen);

#endif /* ISOCD_H */