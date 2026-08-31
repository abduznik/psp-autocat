/*
 * test_sfo.c — host unit test for the PARAM.SFO parser.
 * Builds a synthetic SFO in memory and checks parsing.
 */

#include <stdio.h>
#include <string.h>

#include "sfo.h"

static int failures = 0;

static void check(int cond, const char *label)
{
    if (cond) {
        printf("PASS %s\n", label);
    } else {
        printf("FAIL %s\n", label);
        failures++;
    }
}

int main(void)
{
    /* Build a minimal SFO:
     * header (20) + 2 entries (32) +
     * key table: "CATEGORY\0TITLE\0" +
     * data table: "PG\0\0\0SomeTitle\0"
     */
    unsigned char sfo[256];
    memset(sfo, 0, sizeof(sfo));
    sfo[0]=0x00; sfo[1]='P'; sfo[2]='S'; sfo[3]='F';  /* LE magic 0x46535000 */
    /* version */
    sfo[4]=1; sfo[5]=0; sfo[6]=0; sfo[7]=1;
    /* key_table_start = 20 + 32 = 52 */
    sfo[8]=52;
    /* data_table_start = 52 + 16 = 68 */
    sfo[12]=68;
    /* entries = 2 */
    sfo[16]=2;

    /* entry 0: CATEGORY = "PG" */
    sfo[20]=0;          /* key offset 0 */
    sfo[22]=0x04; sfo[23]=0x02;  /* fmt 0x0204 string */
    sfo[24]=3;          /* len */
    sfo[28]=3;          /* max_len */
    sfo[32]=0;          /* data offset 0 */

    /* entry 1: TITLE = "SomeTitle" */
    sfo[36]=9;          /* key offset 9 */
    sfo[38]=0x04; sfo[39]=0x02;
    sfo[40]=10;         /* len */
    sfo[44]=10;         /* max_len */
    sfo[48]=3;          /* data offset 3 */

    /* key table at 52 */
    memcpy(sfo+52, "CATEGORY\0", 9);
    memcpy(sfo+61, "TITLE\0", 6);
    /* data table at 68 */
    memcpy(sfo+68, "PG\0", 3);
    memcpy(sfo+71, "SomeTitle\0", 10);

    sfo_entry_t entries[8];
    int n = sfo_parse(sfo, sizeof(sfo), entries, 8);

    check(n == 2, "parsed 2 entries");
    check(strcmp(entries[0].key, "CATEGORY") == 0, "entry0 key CATEGORY");
    check(strcmp(entries[1].key, "TITLE") == 0, "entry1 key TITLE");
    check(strcmp(sfo_get_str(sfo, &entries[0]), "PG") == 0, "entry0 value PG");
    check(strcmp(sfo_get_str(sfo, &entries[1]), "SomeTitle") == 0, "entry1 value SomeTitle");

    /* integer fmt: change TITLE fmt to 0x0404 and write known data */
    sfo[38]=0x04; sfo[39]=0x04;          /* fmt = 0x0404 (int) */
    sfo[71]=0x2A; sfo[72]=0x00; sfo[73]=0; sfo[74]=0;  /* data = 42 */
    n = sfo_parse(sfo, sizeof(sfo), entries, 8);
    check(n == 2, "still 2 entries after fmt change");
    check(sfo_get_int(sfo, &entries[1]) == 42, "int fmt reads 42");
    check(strcmp(sfo_get_str(sfo, &entries[1]), "") == 0, "int fmt str -> empty");

    /* bad magic */
    sfo[0]='X';
    check(sfo_parse(sfo, sizeof(sfo), entries, 8) < 0, "bad magic rejected");
    /* bad magic offset-3 (u32 LE compare must fail too) */
    sfo[0]=0x00; sfo[1]='P'; sfo[2]='S'; sfo[3]='X';
    check(sfo_parse(sfo, sizeof(sfo), entries, 8) < 0, "bad magic u32 rejected");

    printf("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}