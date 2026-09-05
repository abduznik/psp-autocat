/*
 * movedriver — tiny kernel-mode helper PRX
 *
 * Confirmed on real hardware: sceIoRename works fine within the same
 * directory but fails with 0x80010011 (FILE_ALREADY_EXISTS) for every
 * cross-directory move on this CFW's FAT driver, regardless of
 * whether the destination actually exists. That's exactly what
 * sorting a game into a CAT_xx subfolder needs.
 *
 * Following the same pattern real PSP homebrew tools use (e.g.
 * CMFileManager PSP's fs_driver.prx) for privileged filesystem
 * operations: this module runs sceIoRename with the calling thread's
 * K1 register and user level temporarily elevated to kernel/max
 * (pspSdkSetK1(0) + sctrlKernelSetUserLevel(8)), then immediately
 * restores both. That's the smallest possible privilege escalation —
 * just long enough for the one syscall — not a persistent hook and
 * not resident at boot; a caller loads this module on demand, calls
 * pspIoRename once, and can unload it again.
 *
 * License: MIT
 */

#include <pspsdk.h>
#include <pspkernel.h>
#include <systemctrl.h>

PSP_MODULE_INFO("movedriver", PSP_MODULE_KERNEL, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();

int pspIoRename(const char *oldname, const char *newname)
{
    u32 k1 = pspSdkSetK1(0);
    int level = sctrlKernelSetUserLevel(8);

    int ret = sceIoRename(oldname, newname);

    sctrlKernelSetUserLevel(level);
    pspSdkSetK1(k1);
    return ret;
}

int module_start(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    return 0;
}

int module_stop(void)
{
    return 0;
}
