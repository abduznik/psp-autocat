/*
 * genre.c — offline title-keyword -> genre lookup table
 *
 * Same technique as classify.c's emulator-title detection: case-
 * insensitive substring match against a curated keyword list. This
 * is necessarily a small, hand-picked slice of well-known titles —
 * there is no genre field anywhere in a PARAM.SFO or UMD_DATA.BIN,
 * and matching by DISC_ID would require a verified ID database this
 * project doesn't have. Keyword matching degrades safely: an
 * unmatched or misspelled title just gets no genre and falls back
 * to the normal PSP/PS1/Emulator/Homebrew split in classify.c.
 */

#include <string.h>

#include "genre.h"

typedef struct {
    const char *keyword;
    const char *genre;
} genre_entry_t;

static const genre_entry_t table[] = {
    /* Action / Adventure */
    { "god of war",            "Action" },
    { "daxter",                "Action" },
    { "silent hill",           "Action" },
    { "uncharted",             "Action" },
    { "jak and daxter",        "Action" },
    { "assassin's creed",      "Action" },
    { "resistance",            "Action" },
    { "syphon filter",         "Action" },

    /* RPG */
    { "persona",               "RPG" },
    { "final fantasy",         "RPG" },
    { "kingdom hearts",        "RPG" },
    { "crisis core",           "RPG" },
    { "dissidia",              "RPG" },
    { "valkyria chronicles",   "RPG" },
    { "star ocean",            "RPG" },
    { "tactics ogre",          "RPG" },

    /* Racing */
    { "gran turismo",          "Racing" },
    { "midnight club",         "Racing" },
    { "ridge racer",           "Racing" },
    { "wipeout",               "Racing" },
    { "burnout",               "Racing" },
    { "need for speed",        "Racing" },
    { "motorstorm",            "Racing" },

    /* Fighting */
    { "tekken",                "Fighting" },
    { "street fighter",        "Fighting" },
    { "mortal kombat",         "Fighting" },
    { "soulcalibur",           "Fighting" },
    { "dead or alive",         "Fighting" },

    /* Sports */
    { "fifa",                  "Sports" },
    { "pro evolution soccer",  "Sports" },
    { "winning eleven",        "Sports" },
    { "nba",                   "Sports" },
    { "madden",                "Sports" },
    { "hot shots golf",        "Sports" },

    /* Puzzle / Party */
    { "locoroco",              "Puzzle" },
    { "patapon",               "Puzzle" },
    { "lumines",               "Puzzle" },
    { "me and my katamari",    "Puzzle" },
    { "puzzle bobble",         "Puzzle" },

    /* Tactical / Strategy / Shooter */
    { "metal gear",            "Tactical" },
    { "monster hunter",        "Action" },
    { "socom",                 "Shooter" },
    { "killzone",              "Shooter" },
    { "call of duty",          "Shooter" },

    { NULL, NULL }
};

static int starts_with_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        char c = *s, p = *prefix;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (p >= 'a' && p <= 'z') p -= 32;
        if (c != p) return 0;
        s++; prefix++;
    }
    return 1;
}

static int contains_ci(const char *haystack, const char *needle)
{
    if (!*needle) return 1;
    for (; *haystack; haystack++) {
        if (starts_with_ci(haystack, needle)) return 1;
    }
    return 0;
}

const char *genre_lookup(const char *title)
{
    int i;
    if (!title || !*title) return NULL;
    for (i = 0; table[i].keyword; i++) {
        if (contains_ci(title, table[i].keyword)) return table[i].genre;
    }
    return NULL;
}

/* ── disc-id table (for /ISO rips — UMD_DATA.BIN has no title) ────
 *
 * Every id below was read directly from a real UMD_DATA.BIN dump
 * (see the project's real-hardware test pass), never guessed. Add
 * to this table the same way: extract the real DISC_ID from the
 * actual ISO, don't type one in from memory. A wrong entry here
 * would silently misfile a real game, unlike an unmatched keyword
 * in genre_lookup() which just falls through safely. */
typedef struct {
    const char *id;    /* no-dash, upper-case */
    const char *genre;
} id_genre_entry_t;

static const id_genre_entry_t id_table[] = {
    { "UCUS98618", "Action" },    /* Daxter */
    { "UCUS98632", "Racing" },    /* Gran Turismo */
    { "UCUS98731", "Puzzle" },    /* LocoRoco 2 */
    { "UCUS98732", "Puzzle" },    /* Patapon 2 */
    { "ULUS10509", "Tactical" },  /* Metal Gear Solid: Peace Walker */
    { "ULUS10021", "Racing" },    /* Midnight Club 3: DUB Edition */
    { "ULUS10450", "Action" },    /* Silent Hill: Shattered Memories */
    { "ULUS10466", "Fighting" },  /* Tekken 6 */
    { "ULUS10512", "RPG" },       /* Persona 3 Portable */
    { "ULJM05800", "Action" },    /* Monster Hunter Portable 3rd (JP) */
    { NULL, NULL }
};

static void normalize_id(const char *in, char *out, int outsize)
{
    int j = 0;
    if (!in) { out[0] = 0; return; }
    for (; *in && j < outsize - 1; in++) {
        char c = *in;
        if (c == '-') continue;
        if (c >= 'a' && c <= 'z') c -= 32;
        out[j++] = c;
    }
    out[j] = 0;
}

const char *genre_lookup_by_id(const char *disc_id)
{
    char norm[32];
    int i;

    if (!disc_id || !*disc_id) return NULL;
    normalize_id(disc_id, norm, sizeof(norm));

    for (i = 0; id_table[i].id; i++) {
        if (strcmp(id_table[i].id, norm) == 0) return id_table[i].genre;
    }
    return NULL;
}
