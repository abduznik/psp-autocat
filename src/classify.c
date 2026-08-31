/*
 * classify.c — game classification rules
 *
 * Priority order:
 *   1. CATEGORY "PG" or DISC_ID SL/SC prefix  -> PS1
 *   2. CATEGORY "MS"/"UG"/"MG" or UL/UC/NPU prefix -> PSP
 *   3. Emulator title keywords                 -> Emulators
 *   4. else                                    -> Homebrew
 *
 * Uses PARAM.SFO categories as-is; only ASCII, case-insensitive.
 * No PSP dependencies, unit-testable on the host.
 */

#include <string.h>

#include "classify.h"

static const char *const emulator_titles[] = {
    "gpsp", "gpSP", "snes9x", "nesterj", "nester", "mgba", "daedalus",
    "cps1psp", "cps2psp", "mvspsp", "picodrive", "masterboy", "sms",
    "fbalpha", "fba", "mednafen", "pcsx", "dgen", "genplus",
    NULL
};

static int starts_with_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        char c = *s;
        char p = *prefix;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (p >= 'a' && p <= 'z') p -= 32;
        if (c != p) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int contains_ci(const char *haystack, const char *needle)
{
    size_t nl = strlen(needle);
    if (nl == 0) return 1;
    while (*haystack) {
        if (starts_with_ci(haystack, needle)) return 1;
        haystack++;
    }
    return 0;
}

static int is_emulator_title(const char *title)
{
    int i;
    if (!title) return 0;
    for (i = 0; emulator_titles[i]; i++) {
        if (contains_ci(title, emulator_titles[i])) return 1;
    }
    return 0;
}

enum ac_kind classify_game(const char *category,
                           const char *disc_id,
                           const char *title)
{
    /* PS1: category PG, or classic Sony ID prefix (SLUS, SLES, SCUS, ...) */
    if (category && strcmp(category, "PG") == 0) return AC_PS1;
    if (disc_id && (starts_with_ci(disc_id, "SL") ||
                    starts_with_ci(disc_id, "SC"))) {
        /* SL/SC prefixes are almost always PS1 (SLUS, SLES, SCUS, ...) */
        return AC_PS1;
    }

    /* Emulators by recognizable title — must beat the generic MS/UG/MG
     * template category, since emulator homebrew is often stamped "MS"
     * by the build tool. A real PSP title cannot be an emulator name. */
    if (is_emulator_title(title)) return AC_EMULATOR;

    /* official PSP: category MS/UG/MG, or PSP ID prefix (ULUS, ULES, UCUS, NPUG...) */
    if (category &&
        (strcmp(category, "MS") == 0 ||
         strcmp(category, "UG") == 0 ||
         strcmp(category, "MG") == 0)) {
        /* MG is a template stamp — still a standard eboot */
        return AC_PSP;
    }
    if (disc_id &&
        (starts_with_ci(disc_id, "UL") ||
         starts_with_ci(disc_id, "UC") ||
         starts_with_ci(disc_id, "NPU") ||
         starts_with_ci(disc_id, "NPE") ||
         starts_with_ci(disc_id, "NPH"))) {
        return AC_PSP;
    }

    /* emulators by recognizable title */
    if (is_emulator_title(title)) return AC_EMULATOR;

    return AC_HOMEBREW;
}

const char *ac_kind_folder(enum ac_kind kind)
{
    switch (kind) {
    case AC_PSP:       return "CAT_01_PSP";
    case AC_PS1:       return "CAT_02_PS1";
    case AC_EMULATOR:  return "CAT_03_Emulators";
    case AC_HOMEBREW:  return "CAT_04_Homebrew";
    case AC_UNKNOWN:   /* fallthrough */
    default:           return "CAT_99_Uncategorized";
    }
}