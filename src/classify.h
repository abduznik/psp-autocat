/*
 * classify.h — game classification rules (pure C, host-testable)
 */

#ifndef CLASSIFY_H
#define CLASSIFY_H

enum ac_kind {
    AC_FAVORITE = -1,   /* user-marked favorite (overrides classification) */
    AC_PSP = 0,         /* official PSP game */
    AC_PS1 = 1,         /* PSone classic / PS1 eboot */
    AC_EMULATOR = 2,    /* known emulator by title */
    AC_HOMEBREW = 3,    /* everything else */
    AC_UNKNOWN = 4      /* unreadable metadata */
};

/* Decide which category a game belongs to. */
enum ac_kind classify_game(const char *category,
                           const char *disc_id,
                           const char *title);

/* Folder name for a kind (CAT_xx_ prefix keeps XMB sorted). */
const char *ac_kind_folder(enum ac_kind kind);

#endif /* CLASSIFY_H */