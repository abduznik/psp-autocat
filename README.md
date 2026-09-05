# AutoCat — PSP Automatic Game Categorizer

Inspired by **Game Categories Lite** (Bubbletune / codestation), but automatic: where GCLite required you to *manually* create `CAT_` folders and drag games into them, **AutoCat does it for you**.

Scans **`/PSP/GAME`** (EBOOT.PBP games and homebrew) **and `/ISO`** (.iso/.cso UMD rips), reads real metadata, classifies, and physically renames each game into the right category folder. The XMB natively renders subfolders as category folders — no vsh patching is needed for the categories themselves to show up, and it works on any CFW (PRO, ME, ARK...). PRO/ME even merge same-named categories between `/PSP/GAME` and `/ISO` on the XMB.

**Run it from the "AutoCat Favorites" homebrew (TRIANGLE), not the `autocat.prx` boot-time plugin.** See [Installation](#installation) — the plugin path caused real, repeated crashes on real hardware during development and is not currently recommended.

## What gets categorized

| Source | How it's read |
|---|---|
| `/PSP/GAME/<game>/EBOOT.PBP` | `PARAM.SFO` (CATEGORY, DISC_ID, TITLE) |
| `/ISO/*.iso` | ISO9660 → `UMD_DATA.BIN` game id |
| `/ISO/*.cso` | CSO decompression (zlib) → game id |

All recognized PS1 eboots — including POP-FE / PSX2PSP conversions — land in the PS1 category. Covers the POP-FE `ME` category, PSN's `PG`, and the classic `SL*`/`SC*` disc prefixes.

## Category folders

| Folder | What goes in it |
|---|---|
| `CAT_00_Favorites` | Games you've marked as a favorite (see below) |
| `CAT_01_PSP` | Official PSP games (CATEGORY=MS/UG/MG, IDs UL*, UC*, NPU*/NPE*/NPH*/NPJ*) |
| `CAT_02_PS1` | PSone classics (CATEGORY=PG/ME, IDs SL*, SC*) |
| `CAT_03_Emulators` | Recognized by title (gPSP, Snes9xTYL, NesterJ, Daedalus, mGBA, CPS1/2PSP...) |
| `CAT_04_Homebrew` | Everything else |
| `CAT_99_Uncategorized` | Unreadable EBOOTs/ISOs — left in place, noted in the report |

Numeric prefix = XMB sort order. `CAT_` prefix = compatible with GCLite conventions (and with GCL's filter/sorting if you run both).

## Favorites

AutoCat is a fully standalone sorter — it does **not** need Game Categories Lite installed alongside it. It only *reuses* GCLite's `CAT_xx_Name` folder-naming convention, which the PSP's stock XMB already renders as category folders on its own.

To pin a game to `CAT_00_Favorites` so it always shows up at the top, regardless of what category it'd otherwise sort into:

- **`/PSP/GAME` eboots**: drop an empty file named `FAVORITE` inside the game's folder (e.g. `ms0:/PSP/GAME/God of War - Chains of Olympus/FAVORITE`).
- **`/ISO` rips**: create an empty sidecar file next to the rip named `<same name>.favorite` (e.g. `Persona 3.iso` -> `Persona 3.iso.favorite`).

On the next boot AutoCat pulls the marked game out of wherever it currently lives — including out of another `CAT_xx` folder — and moves it into `CAT_00_Favorites`. This is the one exception to the "never touch an already-categorized folder" rule, since favoriting is a deliberate action on your part.

There's no built-in "recently played" tracking — that would require patching the game-launch path (VSH syscall hooking), which is explicitly outside this project's "no VSH patching" design and carries real crash risk across different CFWs/firmware versions. Favorites is the safe, standalone way to keep a "continue playing" shortlist.

## Installation (recommended: homebrew, not a plugin)

1. Copy the `AutoCat Favorites` folder (containing `EBOOT.PBP`) to `ms0:/PSP/GAME/`
2. Launch **AutoCat Favorites** from the XMB like any other game
3. Press **TRIANGLE** to run the sort
4. It classifies and moves everything, then shows a summary; the full move-by-move log is also appended to `ms0:/seplugins/autocat_report.txt`
5. Press X, then START to exit back to the XMB — go to Game → Memory Stick and you'll see the `CAT_xx` folders

This is a normal homebrew game process: if anything ever goes wrong, it can only crash itself and drop you back to the XMB. Run it again any time you add new games.

### Boot-time plugin (`autocat.prx`) — not currently recommended

The repo also builds `autocat.prx`, a `vsh.txt` plugin meant to run the same sort automatically at boot. **Do not install this without understanding the risk**: during development, running the categorization logic as a `vsh.txt` plugin caused a freeze, a memory-stick filesystem error requiring a hard power-off, and a kernel-level crash on real PRO 6.60/6.61 hardware, across multiple attempts including a plain `PSP_MODULE_USER` build. The underlying cause was never fully root-caused — a `vsh.txt` plugin runs inside the VSH process itself with no isolation, so a bug there can take down the whole boot chain instead of just crashing one program. If you still want to experiment with it:

1. Copy `autocat.prx` to `ms0:/seplugins/`
2. Add to `ms0:/seplugins/vsh.txt`: `ms0:/seplugins/autocat.prx 1`
3. Reboot — if the PSP hangs, freezes, or won't boot, hold the power switch to force it off, then either remove the memory stick and delete/rename `autocat.prx` from another device, or boot into Recovery Menu (hold R while powering on) and disable it there
4. Your game files are never at risk either way — every incident during development left the actual games untouched; only the boot process itself was affected

## Safety

- **Idempotent**: already-categorized games (`CAT_*` / `XX_*` folders) are never touched — only loose games get sorted
- Never touches: hidden dirs, non-game dirs, DLC, `/ISO` subfolders it doesn't recognize, saves
- Name collisions get `_2`, `_3`... suffixes instead of clobbering (ISO extensions preserved: `Game_2.iso`)
- If an `EBOOT.PBP` or ISO/CSO can't be parsed it's left exactly where it is
- Removing `autocat.prx` from `vsh.txt` undoes nothing — your games stay in their new folders (which is the point)

## Note

If you change the classification rules later and want to re-run on already-sorted games, you'd move the games out of the folders manually — AutoCat deliberately refuses to touch organized categories.

### v1.2: fixed real-hardware no-op bug

On real PSP hardware, `module_start` for a `vsh.txt`-loaded plugin runs synchronously on the VSH's own plugin-loader thread. v1.1 did the entire directory scan/classify/rename pass (plus CSO zlib decompression) right there in `module_start`, before returning. That's a lot of blocking `sceIo*` traffic thrown at a boot-critical thread before the memory stick driver is necessarily done settling — on real hardware this either silently did nothing or got cut short, even though the classification logic itself (covered by the host unit tests) was correct. v1.2 spawns a dedicated worker thread from `module_start`, waits a few seconds for the storage to settle, and does the actual sorting there instead, returning from `module_start` immediately.

## Building

Docker (recommended):

```bash
docker run --rm -v "$PWD:/src" -w "/src" pspdev/pspdev make release
```

Host-side unit tests (no SDK needed — tests the SFO parser and classifier logic):

```bash
make test
```

## CI/CD

`.github/workflows/build.yml`:
- Every push/PR → Docker PRX build **+ host unit tests** (classification + SFO parsing)
- Tag `v*` or manual dispatch → zip package + GitHub Release

```bash
# Release v1.0
git tag v1.0 && git push origin v1.0
# or
gh workflow run build.yml -f tag=v1.0
```

## License

MIT