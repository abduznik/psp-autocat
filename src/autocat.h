#ifndef AUTOCAT_H
#define AUTOCAT_H

/* Optional fast cross-directory rename hook. Plain sceIoRename fails
 * with 0x80010011 for cross-directory moves on some CFWs (confirmed
 * on real hardware) — if the caller has a privileged rename available
 * (see favtool's movedriver.prx loader), register it here and every
 * move will try it first, falling back to copy+delete only if it's
 * NULL or itself fails. Same signature/semantics as sceIoRename:
 * return >= 0 on success. Pass NULL to go back to copy+delete only. */
void autocat_set_fast_rename(int (*fast_rename)(const char *, const char *));

/* Progress callback, invoked periodically while copying a file (the
 * slow path — cross-directory moves with no fast_rename available).
 * name is the current source filename (not a full path), bytes_done/
 * bytes_total describe progress through that one file. Called with
 * bytes_total == 0 right before starting a new file (so the UI can
 * reset/print the name once) and periodically as bytes accumulate.
 * Return 0 to continue, nonzero to abort the current copy — the
 * partially-copied destination file is deleted and the original is
 * left untouched, then the whole sort stops (autocat_run_all returns
 * early) so a later run can pick up where this one left off. Pass
 * NULL to disable (no progress reporting, can't be cancelled mid-copy). */
void autocat_set_progress_callback(int (*cb)(const char *name,
                                             unsigned int bytes_done,
                                             unsigned int bytes_total));

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