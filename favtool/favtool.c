/*
 * favtool — AutoCat Favorites & Sorter
 *
 * A tiny homebrew EBOOT (launched from the XMB like any game) that
 * lists every game AutoCat knows about, lets you toggle FAVORITE /
 * *.favorite marker files, and can run the actual categorization
 * pass (autocat_run_all) on demand via TRIANGLE.
 *
 * Deliberately NOT a vsh.txt plugin: a vsh.txt plugin runs inside
 * the VSH process itself, sharing its threads/memory with no
 * isolation — a bug there can take down the whole boot chain (we
 * hit this for real: freezes, a USB corruption error, and a
 * kernel-level crash across several attempts at making autocat.prx
 * work as a boot-time plugin). A normal homebrew EBOOT is its own
 * process — if this crashes, you're kicked back to the XMB, nothing
 * else is affected. Slower (you have to launch it, it's not
 * automatic at boot) but categorically safer.
 *
 * Uses pspDebugScreen text output (no GU init) — simple and robust.
 *
 * License: MIT
 */

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspdisplay.h>
#include <string.h>
#include <stdio.h>

#include "autocat.h"

/* Must match the folder name this app is actually packaged/installed
 * under (see Makefile's PSP_EBOOT_TITLE and the root Makefile's pack
 * target, and README's install instructions) — extract_parent_folder_name()
 * in autocat.c compares against this to avoid ever renaming/moving the
 * folder this very executable is running from. */
#define FAVTOOL_OWN_FOLDER_NAME "AutoCat Favorites"

PSP_MODULE_INFO("AutoCatFavorites", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(4096);

#define printf pspDebugScreenPrintf

#define MAX_ENTRIES 512
#define NAME_LEN    96

typedef struct {
    char display[NAME_LEN];  /* what we show in the list */
    char root[64];           /* e.g. ms0:/PSP/GAME/CAT_01_PSP */
    char name[NAME_LEN];     /* folder name or file name */
    int  is_dir;             /* 1 = eboot folder, 0 = iso/cso file */
    int  favorite;
} entry_t;

static entry_t entries[MAX_ENTRIES];
static int entry_count = 0;

/* Diagnostic counters shown on screen when the list comes up empty,
 * so we can see exactly where the scan is dropping entries instead
 * of guessing blind against real hardware. */
static int dbg_dopen_ok = 0;      /* did sceIoDopen(ms0:/PSP/GAME) succeed? */
static int dbg_raw_entries = 0;   /* total sceIoDread() hits, any type */
static int dbg_dirs_seen = 0;     /* entries where entry_is_dir() was true */
static int dbg_eboot_found = 0;   /* dirs where EBOOT.PBP was found */
static char dbg_sample_name[96] = "";  /* first raw entry name seen, for sanity */
static unsigned int dbg_sample_attr = 0;
static unsigned int dbg_sample_mode = 0;

/* Control test: does reading ms0: root (known non-empty — PSP/, ISO/,
 * SEPLUGINS/ etc definitely exist there) work at all? If this is also
 * 0, the bug is in sceIoDread generally, not specific to /PSP/GAME. */
static int dbg_root_dopen_ok = 0;
static int dbg_root_entries = 0;

static void debug_root_scan(void)
{
    SceUID dfd = sceIoDopen("ms0:");
    SceIoDirent dir;
    dbg_root_dopen_ok = (dfd >= 0);
    if (dfd < 0) return;
    while (sceIoDread(dfd, &dir) > 0) dbg_root_entries++;
    sceIoDclose(dfd);
}

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

/* ms0: only — see autocat.c for why ef0: is left out for now. */
static const char *game_roots[1] = { "ms0:" };

static int exit_request = 0;

int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1; (void)arg2; (void)common;
    exit_request = 1;
    return 0;
}

int callback_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread,
                                      0x11, 0xFA0, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
}

static int has_suffix(const char *name, const char *suffix)
{
    size_t nl = strlen(name), sl = strlen(suffix);
    if (nl < sl) return 0;
    return strcasecmp(name + nl - sl, suffix) == 0;
}

static int marker_exists(const entry_t *e)
{
    char path[192];
    if (e->is_dir)
        snprintf(path, sizeof(path), "%s/%s/FAVORITE", e->root, e->name);
    else
        snprintf(path, sizeof(path), "%s/%s.favorite", e->root, e->name);
    return sceIoGetstat(path, NULL) >= 0;
}

static void set_marker(const entry_t *e, int on)
{
    char path[192];
    if (e->is_dir)
        snprintf(path, sizeof(path), "%s/%s/FAVORITE", e->root, e->name);
    else
        snprintf(path, sizeof(path), "%s/%s.favorite", e->root, e->name);

    if (on) {
        SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT, 0777);
        if (fd >= 0) sceIoClose(fd);
    } else {
        sceIoRemove(path);
    }
}

/* Best-effort title: read TITLE out of EBOOT.PBP's PARAM.SFO if we can,
 * else just show the folder/file name. Keeps favtool independent from
 * autocat's sfo.c so it stays a standalone, minimal tool. */
static void read_eboot_title(const char *root, const char *name,
                              char *out, int outsize)
{
    char path[192];
    SceUID fd;
    unsigned char hdr[0x28];
    unsigned int offsets[8];
    unsigned int sfo_off, sfo_size;
    static unsigned char sfo[4096];
    unsigned int key_table, data_table, count, i;

    snprintf(out, outsize, "%s", name);

    snprintf(path, sizeof(path), "%s/%s/EBOOT.PBP", root, name);
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return;

    if (sceIoRead(fd, hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
        sceIoClose(fd);
        return;
    }
    memcpy(offsets, hdr + 8, sizeof(offsets));
    sfo_off = offsets[0];
    sfo_size = offsets[1] - offsets[0];
    if (sfo_size > sizeof(sfo)) { sceIoClose(fd); return; }

    sceIoLseek(fd, sfo_off, PSP_SEEK_SET);
    if (sceIoRead(fd, sfo, sfo_size) != (int)sfo_size) {
        sceIoClose(fd);
        return;
    }
    sceIoClose(fd);

    if (sfo_size < 20 || sfo[0] != 0x00 || sfo[1] != 'P' ||
        sfo[2] != 'S' || sfo[3] != 'F')
        return;

    key_table = sfo[8] | (sfo[9] << 8) | (sfo[10] << 16) | (sfo[11] << 24);
    data_table = sfo[12] | (sfo[13] << 8) | (sfo[14] << 16) | (sfo[15] << 24);
    count = sfo[16] | (sfo[17] << 8) | (sfo[18] << 16) | (sfo[19] << 24);

    for (i = 0; i < count; i++) {
        unsigned int p = 20 + i * 16;
        unsigned int key_off, fmt, data_off;
        const char *key;

        if (p + 16 > sfo_size) break;
        key_off = sfo[p] | (sfo[p + 1] << 8);
        fmt = sfo[p + 2] | (sfo[p + 3] << 8);
        data_off = sfo[p + 12] | (sfo[p + 13] << 8) |
                   (sfo[p + 14] << 16) | (sfo[p + 15] << 24);
        key = (const char *)sfo + key_table + key_off;

        if (strcmp(key, "TITLE") == 0 && fmt == 0x0204) {
            snprintf(out, outsize, "%s",
                     (const char *)sfo + data_table + data_off);
            return;
        }
    }
}

static void add_entry(const char *root, const char *name, int is_dir)
{
    entry_t *e;
    if (entry_count >= MAX_ENTRIES) return;
    e = &entries[entry_count++];
    snprintf(e->root, sizeof(e->root), "%s", root);
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->is_dir = is_dir;
    if (is_dir)
        read_eboot_title(root, name, e->display, sizeof(e->display));
    else
        snprintf(e->display, sizeof(e->display), "%s", name);
    e->favorite = marker_exists(e);
}

/* Scan one /PSP/GAME-style root: loose games AND anything already
 * sorted into a CAT_xx folder (recurse one level into those only). */
static void scan_eboot_root(const char *root)
{
    SceUID dfd = sceIoDopen(root);
    SceIoDirent dir;

    dbg_dopen_ok = (dfd >= 0);
    if (dfd < 0) return;

    while (sceIoDread(dfd, &dir) > 0) {
        const char *name = dir.d_name;
        char sub_path[192];
        char eboot_path[256];
        SceIoStat st;

        dbg_raw_entries++;
        if (dbg_sample_name[0] == '\0' && name[0] != '.') {
            snprintf(dbg_sample_name, sizeof(dbg_sample_name), "%s", name);
            dbg_sample_attr = dir.d_stat.st_attr;
            dbg_sample_mode = dir.d_stat.st_mode;
        }

        if (name[0] == '.') continue;
        if (!entry_is_dir(&dir.d_stat)) continue;
        dbg_dirs_seen++;

        snprintf(eboot_path, sizeof(eboot_path), "%s/%s/EBOOT.PBP", root, name);
        if (sceIoGetstat(eboot_path, &st) >= 0) {
            dbg_eboot_found++;
            add_entry(root, name, 1);
            continue;
        }

        if (strncmp(name, "CAT_", 4) == 0) {
            SceUID sdfd;
            SceIoDirent sdir;
            snprintf(sub_path, sizeof(sub_path), "%s/%s", root, name);
            sdfd = sceIoDopen(sub_path);
            if (sdfd < 0) continue;
            while (sceIoDread(sdfd, &sdir) > 0) {
                const char *sname = sdir.d_name;
                char seboot[256];
                if (sname[0] == '.') continue;
                if (!entry_is_dir(&sdir.d_stat)) continue;
                snprintf(seboot, sizeof(seboot), "%s/%s/EBOOT.PBP", sub_path, sname);
                if (sceIoGetstat(seboot, NULL) >= 0)
                    add_entry(sub_path, sname, 1);
            }
            sceIoDclose(sdfd);
        }
    }
    sceIoDclose(dfd);
}

/* Scan one /ISO-style root: loose rips AND anything sorted into CAT_xx. */
static void scan_iso_root(const char *root)
{
    SceUID dfd = sceIoDopen(root);
    SceIoDirent dir;
    if (dfd < 0) return;

    while (sceIoDread(dfd, &dir) > 0) {
        const char *name = dir.d_name;

        if (name[0] == '.') continue;

        if (entry_is_dir(&dir.d_stat)) {
            if (strncmp(name, "CAT_", 4) == 0) {
                SceUID sdfd;
                SceIoDirent sdir;
                char sub_path[192];
                snprintf(sub_path, sizeof(sub_path), "%s/%s", root, name);
                sdfd = sceIoDopen(sub_path);
                if (sdfd < 0) continue;
                while (sceIoDread(sdfd, &sdir) > 0) {
                    const char *sname = sdir.d_name;
                    if (sname[0] == '.') continue;
                    if (entry_is_dir(&sdir.d_stat)) continue;
                    if (has_suffix(sname, ".iso") || has_suffix(sname, ".cso"))
                        add_entry(sub_path, sname, 0);
                }
                sceIoDclose(sdfd);
            }
            continue;
        }

        if (has_suffix(name, ".iso") || has_suffix(name, ".cso"))
            add_entry(root, name, 0);
    }
    sceIoDclose(dfd);
}

static void scan_all(void)
{
    int i;
    entry_count = 0;
    for (i = 0; i < 1; i++) {
        char game_root[128], iso_root[128];
        snprintf(game_root, sizeof(game_root), "%s/PSP/GAME", game_roots[i]);
        snprintf(iso_root, sizeof(iso_root), "%s/ISO", game_roots[i]);
        scan_eboot_root(game_root);
        scan_iso_root(iso_root);
    }
}

#define VISIBLE_ROWS 24

int main(void)
{
    SceCtrlData pad, last_pad;
    int cursor = 0, top = 0;
    int i;

    pspDebugScreenInit();
    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    debug_root_scan();
    scan_all();
    memset(&last_pad, 0, sizeof(last_pad));

    while (!exit_request) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~last_pad.Buttons;
        last_pad = pad;

        if (entry_count > 0) {
            if (pressed & PSP_CTRL_DOWN) cursor = (cursor + 1) % entry_count;
            if (pressed & PSP_CTRL_UP) cursor = (cursor - 1 + entry_count) % entry_count;
            if (pressed & PSP_CTRL_CROSS) {
                entries[cursor].favorite = !entries[cursor].favorite;
                set_marker(&entries[cursor], entries[cursor].favorite);
            }
        }
        if (pressed & PSP_CTRL_START) break;
        if (pressed & PSP_CTRL_SELECT) scan_all();
        if (pressed & PSP_CTRL_SQUARE) {
            /* Diagnostic: is sceIoRename broken in general on this
             * CFW, or specifically for cross-directory moves? Create
             * a small test file, rename it within the same directory,
             * then try a cross-directory rename into a test subfolder.
             * Neither touches any real game. */
            char report[256];
            SceUID fd;
            int rc_same_dir, rc_cross_dir;

            pspDebugScreenSetXY(0, 0);
            printf("Running sceIoRename diagnostic (safe, uses test files only)...\n");
            sceDisplayWaitVblankStart();

            sceIoRemove("ms0:/seplugins/autocat_rename_test.tmp");
            sceIoRemove("ms0:/seplugins/autocat_rename_test2.tmp");
            sceIoRemove("ms0:/seplugins/autocat_rename_testdir/autocat_rename_test2.tmp");
            sceIoRmdir("ms0:/seplugins/autocat_rename_testdir");

            fd = sceIoOpen("ms0:/seplugins/autocat_rename_test.tmp",
                           PSP_O_WRONLY | PSP_O_CREAT, 0777);
            if (fd >= 0) sceIoClose(fd);

            rc_same_dir = sceIoRename("ms0:/seplugins/autocat_rename_test.tmp",
                                      "ms0:/seplugins/autocat_rename_test2.tmp");

            sceIoMkdir("ms0:/seplugins/autocat_rename_testdir", 0777);
            rc_cross_dir = sceIoRename("ms0:/seplugins/autocat_rename_test2.tmp",
                                       "ms0:/seplugins/autocat_rename_testdir/autocat_rename_test2.tmp");

            snprintf(report, sizeof(report),
                     "\nRENAME-TEST same_dir=0x%08x cross_dir=0x%08x\n",
                     (unsigned int)rc_same_dir, (unsigned int)rc_cross_dir);
            {
                SceUID rfd = sceIoOpen("ms0:/seplugins/autocat_report.txt",
                                       PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
                if (rfd >= 0) {
                    sceIoWrite(rfd, report, strlen(report));
                    sceIoClose(rfd);
                }
            }

            /* clean up test artifacts either way */
            sceIoRemove("ms0:/seplugins/autocat_rename_test.tmp");
            sceIoRemove("ms0:/seplugins/autocat_rename_test2.tmp");
            sceIoRemove("ms0:/seplugins/autocat_rename_testdir/autocat_rename_test2.tmp");
            sceIoRmdir("ms0:/seplugins/autocat_rename_testdir");

            pspDebugScreenSetXY(0, 0);
            printf("Diagnostic done.\n");
            printf("same_dir rename:  0x%08x  (%s)\n", (unsigned int)rc_same_dir,
                   rc_same_dir >= 0 ? "OK" : "FAILED");
            printf("cross_dir rename: 0x%08x  (%s)\n", (unsigned int)rc_cross_dir,
                   rc_cross_dir >= 0 ? "OK" : "FAILED");
            printf("\nAlso appended to ms0:/seplugins/autocat_report.txt\n");
            printf("Press X to continue.\n");
            sceDisplayWaitVblankStart();
            for (;;) {
                sceCtrlReadBufferPositive(&pad, 1);
                if (pad.Buttons & PSP_CTRL_CROSS) break;
                sceKernelDelayThread(16 * 1000);
            }
            memset(&last_pad, 0, sizeof(last_pad));
        }
        if (pressed & PSP_CTRL_TRIANGLE) {
            pspDebugScreenSetXY(0, 0);
            printf("Sorting... this runs the same classify/move logic as\n");
            printf("autocat.prx, just from a normal game process instead\n");
            printf("of a boot-time plugin. Please wait.\n");
            sceDisplayWaitVblankStart();
            /* Critical: tell autocat_run_all() our own folder name so
             * it never renames/moves the directory we're currently
             * executing from — doing that to a live process is what
             * caused a real crash/shutdown on real hardware.
             * sceKernelInitFileName() would be the "proper" way to
             * discover this at runtime, but it needs a kernel-lib link
             * this build doesn't currently carry; a compile-time
             * constant is simpler and just as correct as long as this
             * always ships under the folder name the Makefile/README
             * use ("AutoCat Favorites") — see FAVTOOL_OWN_FOLDER_NAME. */
            autocat_run_all(FAVTOOL_OWN_FOLDER_NAME "/EBOOT.PBP");
            scan_all();
            cursor = 0;
            top = 0;
            pspDebugScreenSetXY(0, 0);
            printf("Sort complete. See ms0:/seplugins/autocat_report.txt\n");
            printf("for a full move-by-move log. Press X to continue.\n\n");
            sceDisplayWaitVblankStart();
            for (;;) {
                sceCtrlReadBufferPositive(&pad, 1);
                if (pad.Buttons & PSP_CTRL_CROSS) break;
                sceKernelDelayThread(16 * 1000);
            }
            memset(&last_pad, 0, sizeof(last_pad));
        }

        if (cursor < top) top = cursor;
        if (cursor >= top + VISIBLE_ROWS) top = cursor - VISIBLE_ROWS + 1;

        pspDebugScreenSetXY(0, 0);
        printf("AutoCat Favorites  (%d games)\n", entry_count);
        printf("UP/DOWN move, X favorite, TRIANGLE sort, SQUARE rename test, SELECT rescan, START exit\n");
        printf("-----------------------------------------------------------------------\n");

        if (entry_count == 0) {
            printf("\nNo games found under /PSP/GAME or /ISO.\n");
            printf("\n-- debug: ms0: root (control test) --\n");
            printf("ms0: dopen: %s   ms0: raw entries: %d\n",
                   dbg_root_dopen_ok ? "OK" : "FAILED", dbg_root_entries);
            printf("\n-- debug: /PSP/GAME --\n");
            printf("ms0:/PSP/GAME dopen: %s\n", dbg_dopen_ok ? "OK" : "FAILED");
            printf("raw dread entries:   %d\n", dbg_raw_entries);
            printf("entries seen as dir: %d\n", dbg_dirs_seen);
            printf("dirs with EBOOT.PBP: %d\n", dbg_eboot_found);
            printf("first entry: \"%s\" attr=0x%08x mode=0x%08x\n",
                   dbg_sample_name, dbg_sample_attr, dbg_sample_mode);
        }

        for (i = 0; i < VISIBLE_ROWS && (top + i) < entry_count; i++) {
            int idx = top + i;
            printf("%s [%c] %.50s\n",
                   idx == cursor ? ">" : " ",
                   entries[idx].favorite ? '*' : ' ',
                   entries[idx].display);
        }

        sceDisplayWaitVblankStart();
        sceKernelDelayThread(16 * 1000);
    }

    sceKernelExitGame();
    return 0;
}
