/*
 * AutoCat for PSP — automatic game categorizer
 *
 * Scans ms0:/PSP/GAME (or ef0: on PSP Go), reads each game's
 * EBOOT.PBP PARAM.SFO, classifies it, and moves it into a
 * CAT_xx category folder so the XMB shows neat groups.
 * No VSH patching — works on any CFW. Idempotent and safe.
 *
 * License: MIT
 */

#include <pspkernel.h>
#include <pspmoduleinfo.h>
#include <pspthreadman.h>
#include <pspiofilemgr.h>
#include <string.h>

#include "autocat.h"

/* PSP_MODULE_KERNEL was tried to match GCLite's known-working
 * approach, but on real hardware it produced a kernel-level crash
 * ("blue screen") instead of just failing safely — a bug in kernel
 * mode can take down the whole OS instead of being contained to one
 * process. Back to PSP_MODULE_USER: a crash here can only kill this
 * module, never the console. Slower to debug (silent failure instead
 * of a crash with a stack trace), but nothing here is worth risking
 * the user's hardware over. */
PSP_MODULE_INFO("AutoCat", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(0);
PSP_NO_CREATE_MAIN_THREAD();

#define REPORT_PATH "ms0:/seplugins/autocat_report.txt"

/* Breadcrumb logger, independent of autocat.c's own report writer —
 * lets us see exactly how far module_start/the worker thread get even
 * if something downstream fails silently. */
static void log_line(const char *line)
{
    SceUID fd = sceIoOpen(REPORT_PATH,
                          PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd < 0) return;
    sceIoWrite(fd, line, strlen(line));
    sceIoClose(fd);
}

/* module_start runs synchronously on whoever is loading the module —
 * for a vsh.txt plugin that's the VSH's own plugin-loader thread. If we
 * do the whole scan/rename walk there, we're blocking VSH boot with
 * heavy sceIo traffic (and CSO zlib decompression) before the memory
 * stick driver is necessarily done settling; some CFWs also don't like
 * a vsh.txt module_start that takes a long time to return. Hand the
 * actual work off to our own thread and return immediately instead. */
static int autocat_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    log_line("\nBOOT: worker thread started, waiting for storage to settle...\n");
    /* give ms0:/ef0: a moment to finish mounting before we touch it */
    sceKernelDelayThread(3 * 1000 * 1000);
    log_line("BOOT: worker thread running autocat_run_all()\n");
    autocat_run_all(NULL); /* vsh.txt plugin isn't itself a /PSP/GAME entry */
    log_line("BOOT: worker thread done\n");
    sceKernelExitThread(0);
    return 0;
}

int module_start(SceSize args, void *argp)
{
    SceUID thid;
    (void)args;
    (void)argp;

    log_line("\nBOOT: module_start entered\n");

    thid = sceKernelCreateThread("autocat_worker", autocat_thread,
                                  0x18, 0x4000, 0, NULL);
    if (thid < 0) {
        log_line("BOOT: sceKernelCreateThread FAILED\n");
        return 0;
    }
    if (sceKernelStartThread(thid, 0, NULL) < 0) {
        log_line("BOOT: sceKernelStartThread FAILED\n");
    } else {
        log_line("BOOT: worker thread started ok\n");
    }

    return 0;
}

int module_stop(void)
{
    return 0;
}