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

#include "autocat.h"

PSP_MODULE_INFO("AutoCat", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(0);
PSP_NO_CREATE_MAIN_THREAD();

int module_start(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    return autocat_run_all();
}

int module_stop(void)
{
    return 0;
}