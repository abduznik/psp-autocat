/*
 * genre.h — offline title-keyword -> genre lookup (pure C, host-testable)
 *
 * The PSP's own game metadata (PARAM.SFO) has no genre field, so this
 * matches known game titles by keyword — the same approach classify.c
 * already uses for emulator detection. Unmatched titles have no
 * genre; callers should fall back to the existing PSP/PS1/etc split.
 */

#ifndef GENRE_H
#define GENRE_H

/* Look up the genre for a game title (case-insensitive substring
 * match against a curated list of known titles). Returns NULL if
 * no match — that's the normal case for anything not in the table. */
const char *genre_lookup(const char *title);

/* Look up the genre for a disc id (e.g. "ULUS-10509", dashes
 * optional, case-insensitive). Used for /ISO rips, where UMD_DATA.BIN
 * only carries a disc id and no title. Entries here are only ever
 * added after being verified against a real dump — see genre.c.
 * Returns NULL if no match. */
const char *genre_lookup_by_id(const char *disc_id);

#endif /* GENRE_H */
