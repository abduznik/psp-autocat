/*
 * test_classify.c — host unit tests for AutoCat classification.
 * Runs on the CI runner (plain gcc, no PSP SDK needed).
 * exit 0 = all pass, non-zero = failure.
 */

#include <stdio.h>
#include <string.h>

#include "classify.h"

static int failures = 0;

#define CHECK(kind_expr, expected_kind, label)                        \
    do {                                                              \
        enum ac_kind got = (kind_expr);                               \
        if (got != (expected_kind)) {                                 \
            printf("FAIL %s: got %d want %d\n",                       \
                   label, (int)got, (int)(expected_kind));            \
            failures++;                                               \
        } else {                                                      \
            printf("PASS %s\n", label);                               \
        }                                                             \
    } while (0)

int main(void)
{
    /* PS1 detection */
    CHECK(classify_game("PG", "SLUS-00800", "Legacy of Kain"), AC_PS1, "cat=PG -> PS1");
    CHECK(classify_game("MS", "SLES-01423", "Colin McRae Rally"), AC_PS1, "SLES id -> PS1");
    CHECK(classify_game("",   "SCUS-94492", "Resident Evil DC"), AC_PS1, "SCUS id -> PS1");
    CHECK(classify_game("UG", "SLPS-12345", "Ridge Racer Type 4"), AC_PS1, "SLPS id -> PS1");

    /* PSP detection */
    CHECK(classify_game("MS", "ULUS-10536", "God of War"), AC_PSP, "cat=MS -> PSP");
    CHECK(classify_game("UG", "ULES-00812", "GTA Liberty City Stories"), AC_PSP, "cat=UG -> PSP");
    CHECK(classify_game("MG", "UCJS10041", "PSPdisp"), AC_PSP, "cat=MG template -> PSP");
    CHECK(classify_game("",   "NPUG-80611", "Daily Trainer"), AC_PSP, "NPUG id -> PSP");
    CHECK(classify_game("",   "ULJM-05371", "Monster Hunter Portable"), AC_PSP, "ULJM id -> PSP");

    /* Emulator detection by title */
    CHECK(classify_game("MS", "ABCD12345", "gPSP Kai"), AC_EMULATOR, "gPSP title -> Emu");
    CHECK(classify_game("",   "",          "Snes9xTYL 0.4.2"), AC_EMULATOR, "Snes9x title -> Emu");
    CHECK(classify_game("MS", "XYZ",       "NesterJ"), AC_EMULATOR, "NesterJ title -> Emu");
    CHECK(classify_game("HG", "",          "Daedalus X64"), AC_EMULATOR, "Daedalus title -> Emu");

    /* Homebrew / other */
    CHECK(classify_game("HG", "",          "PSP Filer"), AC_HOMEBREW, "cat=HG -> Homebrew");
    CHECK(classify_game("",   "",          "SomeRandomHomebrew"), AC_HOMEBREW, "no meta -> Homebrew");
    CHECK(classify_game("",   "ZZZZ",      "Not a real game"), AC_HOMEBREW, "unknown id -> Homebrew");

    /* Folder names */
    if (strcmp(ac_kind_folder(AC_PSP), "CAT_01_PSP") != 0) {
        printf("FAIL folder PSP\n"); failures++;
    }
    if (strcmp(ac_kind_folder(AC_PS1), "CAT_02_PS1") != 0) {
        printf("FAIL folder PS1\n"); failures++;
    }
    if (strcmp(ac_kind_folder(AC_EMULATOR), "CAT_03_Emulators") != 0) {
        printf("FAIL folder Emulators\n"); failures++;
    }
    if (strcmp(ac_kind_folder(AC_HOMEBREW), "CAT_04_Homebrew") != 0) {
        printf("FAIL folder Homebrew\n"); failures++;
    }

    printf("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}