/*
 * autocat.c — AutoCat organizer core
 *
 * Walks /PSP/GAME/, parses EBOOT.PBP PARAM.SFO, classifies,
 * and renames game folders into CAT_xx category folders.
 * Never touches: hidden dirs, CAT_* dirs, non-EBOOT dirs.
 */

#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

#include "autocat.h"
#include "sfo.h"
#include "classify.h"

#define REPORT_PATH "ms0:/seplugins/autocat_report.txt"

static const char *game_roots[2] = { "ms0:/PSP/GAME", "ef0:/PSP/GAME" };

/* ── helpers ─────────────────────────────────────────────── */

static int is_categorized(const char *name)
{
    /* skip GCLite-style CAT_* folders and "XXname" sorted folders */
    if (strncmp(name, "CAT_", 4) == 0) return 1;
    if (name[0] >= '0' && name[0] <= '9' &&
        name[1] >= '0' && name[1] <= '9' && name[2] == '_') return 1;
    return 0;
}

static int dir_has_eboot(const char *root, const char *name)
{
    char path[140];
    SceIoStat st;

    snprintf(path, sizeof(path), "%s/%s/EBOOT.PBP", root, name);
    return sceIoGetstat(path, &st) >= 0;
}

/* Read PARAM.SFO out of an EBOOT.PBP into buf. Returns SFO size or <0. */
static int read_eboot_sfo(const char *root, const char *name,
                           unsigned char *buf, int bufsize)
{
    char path[140];
    SceUID fd;
    unsigned char hdr[0x28];
    unsigned int offsets[8];
    unsigned int sfo_off, sfo_size;
    int n;

    snprintf(path, sizeof(path), "%s/%s/EBOOT.PBP", root, name);
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;

    n = sceIoRead(fd, hdr, sizeof(hdr));
    if (n == (int)sizeof(hdr)) {
        memcpy(offsets, hdr + 8, sizeof(offsets));
        sfo_off = offsets[0];
        sfo_size = offsets[1] - offsets[0];
        if (sfo_size <= (unsigned int)bufsize) {
            sceIoLseek(fd, sfo_off, PSP_SEEK_SET);
            n = sceIoRead(fd, buf, sfo_size);
        } else {
            n = -1;
        }
    } else {
        n = -1;
    }
    sceIoClose(fd);
    return n;
}

/* ── organizer ───────────────────────────────────────────── */

static void write_report_line(SceUID rfd, const char *line)
{
    if (rfd >= 0) {
        sceIoWrite(rfd, line, strlen(line));
    }
}

int autocat_run_all(void)
{
    int root_idx;
    const char *root = NULL;
    SceUID dfd = -1;
    SceIoDirent dir;
    SceUID rfd = -1;
    int moved = 0;
    char report[256];

    /* find a working game root (ms0: first, then ef0: PSP Go) */
    for (root_idx = 0; root_idx < 2; root_idx++) {
        dfd = sceIoDopen(game_roots[root_idx]);
        if (dfd >= 0) {
            root = game_roots[root_idx];
            break;
        }
    }
    if (dfd < 0) return 0; /* no game folder at all */

    /* report file (append) */
    rfd = sceIoOpen(REPORT_PATH,
                    PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);

    snprintf(report, sizeof(report),
             "\n== AutoCat run on %s ==\n", root);
    write_report_line(rfd, report);

    while (sceIoDread(dfd, &dir) > 0) {
        const char *name = dir.d_name;
        unsigned char sfo[4096];
        sfo_entry_t entries[32];
        int sfo_size, count, i;
        const char *category = "", *disc_id = "", *title = "";
        enum ac_kind kind;
        const char *folder;
        char old_path[140], new_path[140];
        int suffix = 0;

        /* skip junk and already-organized entries */
        if (name[0] == '.') continue;
        if (!FIO_S_ISDIR(dir.d_stat.st_mode)) continue;
        if (is_categorized(name)) continue;
        if (!dir_has_eboot(root, name)) continue;

        /* parse PARAM.SFO */
        sfo_size = read_eboot_sfo(root, name, sfo, sizeof(sfo));
        if (sfo_size <= 0) {
            snprintf(report, sizeof(report), "SKIP %s (unreadable EBOOT)\n", name);
            write_report_line(rfd, report);
            continue;
        }
        count = sfo_parse(sfo, (unsigned int)sfo_size, entries, 32);

        /* extract keys we care about */
        for (i = 0; i < count; i++) {
            if (strcmp(entries[i].key, "CATEGORY") == 0)
                category = sfo_get_str(sfo, &entries[i]);
            else if (strcmp(entries[i].key, "DISC_ID") == 0)
                disc_id = sfo_get_str(sfo, &entries[i]);
            else if (strcmp(entries[i].key, "TITLE") == 0)
                title = sfo_get_str(sfo, &entries[i]);
        }

        kind = classify_game(category, disc_id, title);
        folder = ac_kind_folder(kind);

        if (kind == AC_UNKNOWN) {
            snprintf(report, sizeof(report),
                     "SKIP %s (unreadable EBOOT)\n", name);
            write_report_line(rfd, report);
            continue;
        }

        /* build category dir if missing */
        snprintf(old_path, sizeof(old_path), "%s/%s", root, folder);
        sceIoMkdir(old_path, 0777);

        /* rename: root/name -> root/folder/name  (collision -> _2, _3..) */
        snprintf(old_path, sizeof(old_path), "%s/%s", root, name);
        for (;;) {
            if (suffix == 0) {
                snprintf(new_path, sizeof(new_path), "%s/%s/%s",
                         root, folder, name);
            } else {
                snprintf(new_path, sizeof(new_path), "%s/%s/%s_%d",
                         root, folder, name, suffix);
            }
            if (sceIoGetstat(new_path, NULL) < 0) break; /* slot free */
            suffix++;
            if (suffix > 99) break;
        }
        if (suffix > 99) continue;

        if (sceIoRename(old_path, new_path) >= 0) {
            moved++;
            snprintf(report, sizeof(report),
                     "MOVE %s -> %s/  [%s|%s] \"%s\"\n",
                     name, folder, category, disc_id, title);
        } else {
            snprintf(report, sizeof(report),
                     "FAIL %s (rename error, in use?)\n", name);
        }
        write_report_line(rfd, report);
    }

    sceIoDclose(dfd);
    if (rfd >= 0) sceIoClose(rfd);
    return moved ? 1 : 0; /* nonzero = something was organized this run */
}