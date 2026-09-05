/*
 * autocat.c — AutoCat organizer core
 *
 * Walks /PSP/GAME (EBOOT.PBP folders) and /ISO (.iso/.cso rips),
 * classifies each game from its metadata, and renames it into the
 * matching CAT_xx category folder.
 *
 * /PSP/GAME  -> classification via EBOOT.PBP PARAM.SFO
 * /ISO/*.iso -> classification via ISO9660 UMD_DATA.BIN game id
 * /ISO/*.cso -> classification via CSO decompression + UMD_DATA.BIN
 *
 * Category folders live next to the source:
 *   /PSP/GAME/CAT_01_PSP/..., /ISO/CAT_01_PSP/...
 * PRO/ME CFW merge same-named categories across both roots on the XMB.
 *
 * Never touches: hidden dirs, CAT_* folders, non-game files.
 */

#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "autocat.h"
#include "sfo.h"
#include "classify.h"
#include "isocd.h"
#include "cso.h"
#include "genre.h"

#define REPORT_PATH "ms0:/seplugins/autocat_report.txt"

/* ms0: only for now — ef0: (PSP Go internal storage) is untested on
 * real hardware and scanning a device that isn't present has caused
 * a hard freeze in this codebase before (see favtool's original
 * unguarded ef0: scan). Re-add once someone can verify on a real Go. */
static const char *game_roots[1] = { "ms0:" };

/* FIO_S_ISDIR(st_mode) is the textbook PSPSDK check, but on real
 * hardware's FAT driver st_mode isn't reliably populated the same way
 * emulators/host testing led us to expect — the official PSPSDK kernel
 * sample (src/samples/kernel/fileio) checks st_attr & FIO_SO_IFDIR
 * instead. Check both so this works regardless of which field the
 * actual on-device IO driver fills in. */
static int entry_is_dir(const SceIoStat *st)
{
    if (st->st_attr & FIO_SO_IFDIR) return 1;
    if (FIO_S_ISDIR(st->st_mode)) return 1;
    return 0;
}

/* ── helpers ─────────────────────────────────────────────── */

static int is_categorized(const char *name)
{
    /* skip GCLite-style CAT_* folders and "XXname" sorted folders */
    if (strncmp(name, "CAT_", 4) == 0) return 1;
    if (name[0] >= '0' && name[0] <= '9' &&
        name[1] >= '0' && name[1] <= '9' && name[2] == '_') return 1;
    return 0;
}

/* the Favorites folder itself is fair game for re-classification (in
 * case a favorite marker gets removed later), unlike every other
 * CAT_ folder which is left alone once sorted */
static int is_locked_category(const char *name)
{
    return is_categorized(name) && strcmp(name, "CAT_00_Favorites") != 0;
}

/* Genre only makes sense for real PSP/PS1 games — an emulator or
 * homebrew title isn't "a genre", it's a tool. Build "CAT_01_PSP" or,
 * if the title matches a known game, "CAT_01_PSP/CAT_Action". */
static void build_kind_folder(enum ac_kind kind, const char *title,
                              char *out, int outsize)
{
    const char *base = ac_kind_folder(kind);
    const char *genre = NULL;

    if (kind == AC_PSP || kind == AC_PS1)
        genre = genre_lookup(title);

    if (genre)
        snprintf(out, outsize, "%s/CAT_%s", base, genre);
    else
        snprintf(out, outsize, "%s", base);
}

static int dir_has_eboot(const char *root, const char *name)
{
    char path[140];
    SceIoStat st;

    snprintf(path, sizeof(path), "%s/%s/EBOOT.PBP", root, name);
    return sceIoGetstat(path, &st) >= 0;
}

/* Drop a file named FAVORITE inside a /PSP/GAME/<game>/ folder to pin
 * it to CAT_00_Favorites regardless of its real classification. */
static int dir_has_favorite_marker(const char *root, const char *name)
{
    char path[140];
    snprintf(path, sizeof(path), "%s/%s/FAVORITE", root, name);
    return sceIoGetstat(path, NULL) >= 0;
}

/* For /ISO rips (single files, nothing to drop a marker "inside"),
 * use a sidecar: Game.iso -> Game.iso.favorite next to it. */
static int has_favorite_sidecar(const char *root, const char *name)
{
    char path[160];
    snprintf(path, sizeof(path), "%s/%s.favorite", root, name);
    return sceIoGetstat(path, NULL) >= 0;
}

static int has_suffix(const char *name, const char *suffix)
{
    size_t nl = strlen(name);
    size_t sl = strlen(suffix);
    if (nl < sl) return 0;
    return strcasecmp(name + nl - sl, suffix) == 0;
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

/* ── /ISO readers ────────────────────────────────────────── */

/* Plain .iso: read sectors straight from file. */
typedef struct {
    SceUID fd;
} iso_file_ctx;

static int iso_sector_read(void *ctx, unsigned int sector,
                           unsigned char *out, unsigned int n)
{
    iso_file_ctx *c = (iso_file_ctx *)ctx;
    if (sceIoLseek(c->fd, (SceOff)sector * 2048, PSP_SEEK_SET) < 0)
        return -1;
    if (sceIoRead(c->fd, out, n * 2048) != (int)(n * 2048))
        return -1;
    return 0;
}

/* ── organizer: /PSP/GAME eboots ─────────────────────────── */

/* Opens, writes, and closes the report file on every single call
 * instead of writing through one long-lived fd. Slower, but every
 * line is guaranteed to actually hit the filesystem before whatever
 * comes next runs — critical for debugging a hang/crash, where a
 * buffered-but-unflushed line would otherwise vanish along with
 * whatever caused the crash, leaving no trace of how far it got. */
static void write_report_line(SceUID unused_rfd, const char *line)
{
    SceUID fd;
    (void)unused_rfd;
    fd = sceIoOpen(REPORT_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd < 0) return;
    sceIoWrite(fd, line, strlen(line));
    sceIoClose(fd);
}

static int organize_eboots(const char *root, SceUID rfd,
                           const char *self_folder_name)
{
    SceUID dfd;
    SceIoDirent dir;
    int moved = 0;
    int raw_entries = 0;
    char report[256];

    dfd = sceIoDopen(root);
    snprintf(report, sizeof(report), "DEBUG organize_eboots(%s) dopen=%s\n",
             root, dfd >= 0 ? "OK" : "FAILED");
    write_report_line(rfd, report);
    if (dfd < 0) return 0;

    while (sceIoDread(dfd, &dir) > 0) {
        const char *name = dir.d_name;
        raw_entries++;
        /* static: 4KB sfo won't fit on the VSH plugin stack */
        static unsigned char sfo[4096];
        sfo_entry_t entries[32];
        int sfo_size, count, i;
        const char *category = "", *disc_id = "", *title = "";
        enum ac_kind kind;
        char folder[64];
        char old_path[140], new_path[140];
        int suffix = 0;

        /* one line the moment we see this entry, before touching it at
         * all — if something hangs/crashes downstream, this is the
         * last line in the report and tells us exactly which entry it
         * was on. Deliberately unconditional, before every skip check. */
        snprintf(report, sizeof(report), "  ENTRY[%d] \"%s\"\n", raw_entries, name);
        write_report_line(rfd, report);

        /* never touch the folder we're currently running from — renaming
         * a homebrew's own backing directory out from under its running
         * executable is what caused a real crash/shutdown on hardware */
        if (self_folder_name && strcmp(name, self_folder_name) == 0) {
            write_report_line(rfd, "    -> skip (self)\n");
            continue;
        }

        /* skip junk and already-organized entries (Favorites folder
         * excepted below via is_locked_category, but that's walked in
         * the promote_favorites() pass, not here) */
        if (name[0] == '.') continue;
        if (!entry_is_dir(&dir.d_stat)) {
            write_report_line(rfd, "    -> skip (not a dir)\n");
            continue;
        }
        if (is_categorized(name)) {
            write_report_line(rfd, "    -> skip (already categorized)\n");
            continue;
        }
        if (!dir_has_eboot(root, name)) {
            write_report_line(rfd, "    -> skip (no EBOOT.PBP)\n");
            continue;
        }
        write_report_line(rfd, "    has EBOOT.PBP\n");

        if (dir_has_favorite_marker(root, name)) {
            kind = AC_FAVORITE;
            snprintf(folder, sizeof(folder), "%s", ac_kind_folder(kind));
            write_report_line(rfd, "    favorite marker found\n");
        } else {
        write_report_line(rfd, "    reading PARAM.SFO...\n");
        /* parse PARAM.SFO */
        sfo_size = read_eboot_sfo(root, name, sfo, sizeof(sfo));
        if (sfo_size <= 0) {
            snprintf(report, sizeof(report), "SKIP %s (unreadable EBOOT)\n", name);
            write_report_line(rfd, report);
            continue;
        }
        snprintf(report, sizeof(report), "    SFO read ok, %d bytes; parsing...\n", sfo_size);
        write_report_line(rfd, report);
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

        write_report_line(rfd, "    classifying...\n");
        kind = classify_game(category, disc_id, title);
        build_kind_folder(kind, title, folder, sizeof(folder));
        }

        snprintf(report, sizeof(report), "    -> folder=\"%s\", making dirs...\n", folder);
        write_report_line(rfd, report);

        /* build category dir if missing (and its genre subdir, if any) */
        snprintf(old_path, sizeof(old_path), "%s/%s", root, ac_kind_folder(kind));
        sceIoMkdir(old_path, 0777);
        snprintf(old_path, sizeof(old_path), "%s/%s", root, folder);
        sceIoMkdir(old_path, 0777);

        write_report_line(rfd, "    dirs ready, renaming...\n");

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

        snprintf(report, sizeof(report), "    calling sceIoRename(%s -> %s)...\n",
                 old_path, new_path);
        write_report_line(rfd, report);

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
    write_report_line(rfd, "  (loop finished, no more dread entries)\n");
    sceIoDclose(dfd);
    snprintf(report, sizeof(report),
             "DEBUG organize_eboots(%s) raw_entries=%d moved=%d\n",
             root, raw_entries, moved);
    write_report_line(rfd, report);
    return moved;
}

/* ── organizer: /ISO rips ────────────────────────────────── */

static int classify_iso_by_id(const char *disc_id)
{
    /* full game id as seen in UMD_DATA.BIN: "UCUS-98618" */
    if (disc_id && *disc_id) {
        return (int)classify_game("", disc_id, "");
    }
    return (int)AC_UNKNOWN;
}

static int organize_iso(const char *root, SceUID rfd)
{
    SceUID dfd;
    SceIoDirent dir;
    int moved = 0;
    char report[256];

    dfd = sceIoDopen(root);
    if (dfd < 0) return 0;

    while (sceIoDread(dfd, &dir) > 0) {
        const char *name = dir.d_name;
        int is_iso, is_cso;
        unsigned char umd[256];
        char disc_id[32];
        enum ac_kind kind;
        const char *folder;
        char old_path[140], new_path[140];
        int n, suffix = 0;

        if (name[0] == '.') continue;
        if (entry_is_dir(&dir.d_stat)) continue;
        if (is_categorized(name)) continue;

        is_iso = has_suffix(name, ".iso");
        is_cso = has_suffix(name, ".cso");
        if (!is_iso && !is_cso) continue; /* only game rips */

        disc_id[0] = 0;
        kind = AC_UNKNOWN;

        if (has_favorite_sidecar(root, name)) {
            kind = AC_FAVORITE;
        } else if (is_cso) {
            cso_ctx_t cso;
            char path[140];
            snprintf(path, sizeof(path), "%s/%s", root, name);
            if (cso_open(&cso, path) == 0) {
                n = isocd_read_umd_data(cso_read_sectors, &cso,
                                        umd, sizeof(umd));
                if (n > 0) {
                    isocd_extract_game_id(umd, n, disc_id, sizeof(disc_id));
                    kind = (enum ac_kind)classify_iso_by_id(disc_id);
                }
                cso_close(&cso);
            }
        } else {
            iso_file_ctx fctx;
            char path[140];
            snprintf(path, sizeof(path), "%s/%s", root, name);
            fctx.fd = sceIoOpen(path, PSP_O_RDONLY, 0);
            if (fctx.fd >= 0) {
                n = isocd_read_umd_data(iso_sector_read, &fctx,
                                        umd, sizeof(umd));
                sceIoClose(fctx.fd);
                if (n > 0) {
                    isocd_extract_game_id(umd, n, disc_id, sizeof(disc_id));
                    kind = (enum ac_kind)classify_iso_by_id(disc_id);
                }
            }
        }

        if (kind == AC_UNKNOWN) {
            snprintf(report, sizeof(report),
                     "SKIP %s (unreadable image, id=[%s])\n", name, disc_id);
            write_report_line(rfd, report);
            continue;
        }
        folder = ac_kind_folder(kind);

        snprintf(old_path, sizeof(old_path), "%s/%s", root, folder);
        sceIoMkdir(old_path, 0777);

        snprintf(old_path, sizeof(old_path), "%s/%s", root, name);
        for (;;) {
            if (suffix == 0) {
                snprintf(new_path, sizeof(new_path), "%s/%s/%s",
                         root, folder, name);
            } else {
                /* keep extension: Name_2.iso */
                char *dot;
                char base[128], ext[8];
                snprintf(base, sizeof(base), "%s", name);
                dot = strrchr(base, '.');
                if (dot) {
                    snprintf(ext, sizeof(ext), "%s", dot);
                    *dot = 0;
                } else {
                    ext[0] = 0;
                }
                snprintf(new_path, sizeof(new_path), "%s/%s/%s_%d%s",
                         root, folder, base, suffix, ext);
            }
            if (sceIoGetstat(new_path, NULL) < 0) break;
            suffix++;
            if (suffix > 99) break;
        }
        if (suffix > 99) continue;

        if (sceIoRename(old_path, new_path) >= 0) {
            moved++;
            snprintf(report, sizeof(report),
                     "MOVE %s -> %s/  [UMD id=%s]\n", name, folder, disc_id);
        } else {
            snprintf(report, sizeof(report),
                     "FAIL %s (rename error, in use?)\n", name);
        }
        write_report_line(rfd, report);
    }
    sceIoDclose(dfd);
    return moved;
}

/* ── favorites promotion sweep ───────────────────────────── */

/* Move root/name -> root/CAT_00_Favorites/name (collision -> _2, _3...).
 * Shared by both the eboot-dir and iso-file promotion paths. */
static int move_into_favorites(const char *root, const char *name, SceUID rfd)
{
    char fav_dir[140], old_path[140], new_path[140], report[256];
    int suffix = 0;
    char *dot = NULL;
    char base[128], ext[8];

    snprintf(fav_dir, sizeof(fav_dir), "%s/%s", root, "CAT_00_Favorites");
    sceIoMkdir(fav_dir, 0777);

    snprintf(base, sizeof(base), "%s", name);
    dot = strrchr(base, '.');
    if (dot) {
        snprintf(ext, sizeof(ext), "%s", dot);
        *dot = 0;
    } else {
        ext[0] = 0;
    }

    snprintf(old_path, sizeof(old_path), "%s/%s", root, name);
    for (;;) {
        if (suffix == 0) {
            snprintf(new_path, sizeof(new_path), "%s/%s%s",
                     fav_dir, base, ext);
        } else {
            snprintf(new_path, sizeof(new_path), "%s/%s_%d%s",
                     fav_dir, base, suffix, ext);
        }
        if (sceIoGetstat(new_path, NULL) < 0) break;
        suffix++;
        if (suffix > 99) return 0;
    }

    if (sceIoRename(old_path, new_path) >= 0) {
        snprintf(report, sizeof(report), "FAVORITE %s -> CAT_00_Favorites/\n", name);
        write_report_line(rfd, report);
        return 1;
    }
    return 0;
}

/* Sweep already-sorted CAT_xx folders (everything but Favorites/
 * Uncategorized) for newly favorite-marked games and pull them into
 * CAT_00_Favorites. This is the one exception to "never touch a
 * categorized folder" — favoriting is a deliberate user action. */
static int promote_favorites(const char *root, SceUID rfd)
{
    SceUID cat_dfd;
    SceIoDirent cat_dir;
    int promoted = 0;
    char report[256];
    int i = 0;

    snprintf(report, sizeof(report), "DEBUG promote_favorites(%s) start\n", root);
    write_report_line(rfd, report);

    cat_dfd = sceIoDopen(root);
    if (cat_dfd < 0) {
        write_report_line(rfd, "DEBUG promote_favorites dopen FAILED\n");
        return 0;
    }

    while (sceIoDread(cat_dfd, &cat_dir) > 0) {
        const char *cat_name = cat_dir.d_name;
        SceUID dfd;
        SceIoDirent dir;
        char cat_path[140];

        i++;
        snprintf(report, sizeof(report), "  PROMOTE-ENTRY[%d] \"%s\"\n", i, cat_name);
        write_report_line(rfd, report);

        if (cat_name[0] == '.') continue;
        if (!entry_is_dir(&cat_dir.d_stat)) continue;
        if (!is_locked_category(cat_name)) continue; /* not a sorted folder */
        if (strcmp(cat_name, "CAT_99_Uncategorized") == 0) continue;

        snprintf(cat_path, sizeof(cat_path), "%s/%s", root, cat_name);
        dfd = sceIoDopen(cat_path);
        if (dfd < 0) continue;

        while (sceIoDread(dfd, &dir) > 0) {
            const char *name = dir.d_name;
            if (name[0] == '.') continue;

            if (entry_is_dir(&dir.d_stat)) {
                if (dir_has_favorite_marker(cat_path, name)) {
                    promoted += move_into_favorites(cat_path, name, rfd);
                    continue;
                }
                /* not an eboot dir itself? must be a CAT_<Genre> subfolder
                 * (e.g. CAT_01_PSP/CAT_Action) — look one level deeper */
                if (strncmp(name, "CAT_", 4) == 0 &&
                    !dir_has_eboot(cat_path, name)) {
                    SceUID gdfd;
                    SceIoDirent gdir;
                    char genre_path[160];
                    snprintf(genre_path, sizeof(genre_path), "%s/%s", cat_path, name);
                    gdfd = sceIoDopen(genre_path);
                    if (gdfd < 0) continue;
                    while (sceIoDread(gdfd, &gdir) > 0) {
                        const char *gname = gdir.d_name;
                        if (gname[0] == '.') continue;
                        if (!entry_is_dir(&gdir.d_stat)) continue;
                        if (!dir_has_favorite_marker(genre_path, gname)) continue;
                        promoted += move_into_favorites(genre_path, gname, rfd);
                    }
                    sceIoDclose(gdfd);
                }
                continue;
            }
            if (!has_favorite_sidecar(cat_path, name)) continue;
            promoted += move_into_favorites(cat_path, name, rfd);
        }
        sceIoDclose(dfd);
    }
    sceIoDclose(cat_dfd);
    snprintf(report, sizeof(report), "DEBUG promote_favorites(%s) done, promoted=%d\n",
             root, promoted);
    write_report_line(rfd, report);
    return promoted;
}

/* Extract the folder-name segment from ".../<folder>/EBOOT.PBP" (or
 * any trailing-filename path). If there's no earlier "/" before that
 * one (e.g. "AutoCat Favorites/EBOOT.PBP" with no device/dir prefix),
 * treat the start of the string as the folder name's start instead of
 * giving up — a caller passing a short, prefix-less path like that is
 * a legitimate case (see favtool.c), not malformed input.
 * Returns "" only for a path with no "/" at all, or NULL/empty. */
static void extract_parent_folder_name(const char *path, char *out, int outsize)
{
    const char *slash1, *slash2;
    size_t len;

    out[0] = 0;
    if (!path || !*path) return;

    slash1 = strrchr(path, '/');
    if (!slash1) return; /* no filename component */

    /* search for the previous slash before slash1 */
    slash2 = NULL;
    {
        const char *p = path;
        while (p < slash1) {
            if (*p == '/') slash2 = p;
            p++;
        }
    }

    len = (size_t)(slash1 - (slash2 ? slash2 + 1 : path));
    if (len >= (size_t)outsize) len = (size_t)outsize - 1;
    memcpy(out, slash2 ? slash2 + 1 : path, len);
    out[len] = 0;
}

/* ── entry ───────────────────────────────────────────────── */

int autocat_run_all(const char *self_path)
{
    int root_idx;
    SceUID rfd;
    char report[256];
    char self_folder_name[128];

    extract_parent_folder_name(self_path, self_folder_name, sizeof(self_folder_name));

    rfd = sceIoOpen(REPORT_PATH,
                    PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);

    for (root_idx = 0; root_idx < 1; root_idx++) {
        const char *base = game_roots[root_idx];
        char game_root[128], iso_root[128];

        snprintf(game_root, sizeof(game_root), "%s/PSP/GAME", base);
        snprintf(iso_root, sizeof(iso_root), "%s/ISO", base);

        /* Used to gate on sceIoGetstat(game_root/iso_root) here to
         * skip a device that isn't present, but on real hardware that
         * check itself was failing even though the paths definitely
         * exist (organize_eboots's own sceIoDopen on the identical
         * path succeeds). Drop the pre-check entirely — sceIoDopen
         * already tells us if a directory is missing, and
         * organize_eboots/organize_iso already handle that (dfd < 0
         * -> return 0) without needing this to gate anything upfront. */
        snprintf(report, sizeof(report),
                 "\n== AutoCat run on %s ==\n", base);
        write_report_line(rfd, report);

        promote_favorites(game_root, rfd);
        promote_favorites(iso_root, rfd);

        organize_eboots(game_root, rfd,
                        self_folder_name[0] ? self_folder_name : NULL);
        organize_iso(iso_root, rfd);
    }

    if (rfd >= 0) sceIoClose(rfd);
    return 0;
}