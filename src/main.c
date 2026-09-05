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

#include "autocat.h"

PSP_MODULE_INFO("AutoCat", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(0);
PSP_NO_CREATE_MAIN_THREAD();

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
    /* give ms0:/ef0: a moment to finish mounting before we touch it */
    sceKernelDelayThread(3 * 1000 * 1000);
    autocat_run_all();
    sceKernelExitThread(0);
    return 0;
}

int module_start(SceSize args, void *argp)
{
    SceUID thid;
    (void)args;
    (void)argp;

    thid = sceKernelCreateThread("autocat_worker", autocat_thread,
                                  0x18, 0x4000, 0, NULL);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, NULL);

    return 0;
}

int module_stop(void)
{
    return 0;
}