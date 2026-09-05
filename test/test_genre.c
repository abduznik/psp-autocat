/*
 * test_genre.c — host unit tests for AutoCat's title-keyword genre
 * lookup. Runs on the CI runner (plain gcc, no PSP SDK needed).
 * exit 0 = all pass, non-zero = failure.
 */

#include <stdio.h>
#include <string.h>

#include "genre.h"

static int failures = 0;

#define CHECK(title, expected)                                        \
    do {                                                              \
        const char *got = genre_lookup(title);                       \
        const char *label = (title) ? (title) : "(null)";             \
        int ok = (got == NULL && (expected) == NULL) ||               \
                 (got && (expected) && strcmp(got, (expected)) == 0); \
        if (!ok) {                                                    \
            printf("FAIL \"%s\": got %s want %s\n", label,           \
                   got ? got : "(null)",                              \
                   (expected) ? (expected) : "(null)");               \
            failures++;                                               \
        } else {                                                      \
            printf("PASS \"%s\" -> %s\n", label, got ? got : "(none)"); \
        }                                                              \
    } while (0)

int main(void)
{
    CHECK("God of War: Chains of Olympus", "Action");
    CHECK("Gran Turismo", "Racing");
    CHECK("Tekken 6", "Fighting");
    CHECK("Shin Megami Tensei: Persona 3 Portable", "RPG");
    CHECK("Patapon 2", "Puzzle");
    CHECK("Metal Gear Solid: Peace Walker", "Tactical");
    CHECK("Monster Hunter Portable 3rd", "Action");
    CHECK("Midnight Club 3: DUB Edition", "Racing");
    CHECK("Some Random Homebrew Tool", NULL);
    CHECK("", NULL);
    CHECK(NULL, NULL);

    printf("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
