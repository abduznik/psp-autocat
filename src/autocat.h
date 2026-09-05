#ifndef AUTOCAT_H
#define AUTOCAT_H

/* Run the auto-categorizer over the game folder.
 *
 * self_path: full path of the caller's own running EBOOT.PBP (e.g.
 * from sceKernelInitFileName()), or NULL if not applicable (the
 * vsh.txt plugin, which isn't itself a /PSP/GAME entry). When set,
 * the folder containing self_path is never scanned/moved — critical
 * for anything that links this code into a normal homebrew EBOOT,
 * since renaming the directory backing the currently-running
 * executable is what caused a real crash/shutdown on real hardware.
 *
 * Returns 0 on success (or nothing to do). */
int autocat_run_all(const char *self_path);

#endif /* AUTOCAT_H */