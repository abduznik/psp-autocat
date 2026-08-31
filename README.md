# AutoCat — PSP Automatic Game Categorizer

Inspired by **Game Categories Lite** (Bubbletune / codestation), but automatic: where GCLite required you to *manually* create `CAT_` folders and drag games into them, **AutoCat does it for you** at boot.

Scans **`/PSP/GAME`** (EBOOT.PBP games and homebrew) **and `/ISO`** (.iso/.cso UMD rips), reads real metadata, classifies, and physically renames each game into the right category folder. The XMB natively renders subfolders as category folders, so **no vsh patching is needed — works on any CFW** (PRO, ME, ARK...). PRO/ME even merge same-named categories between `/PSP/GAME` and `/ISO` on the XMB.

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
| `CAT_01_PSP` | Official PSP games (CATEGORY=MS/UG/MG, IDs UL*, UC*, NPU*/NPE*/NPH*) |
| `CAT_02_PS1` | PSone classics (CATEGORY=PG/ME, IDs SL*, SC*) |
| `CAT_03_Emulators` | Recognized by title (gPSP, Snes9xTYL, NesterJ, Daedalus, mGBA, CPS1/2PSP...) |
| `CAT_04_Homebrew` | Everything else |
| `CAT_99_Uncategorized` | Unreadable EBOOTs/ISOs — left in place, noted in the report |

Numeric prefix = XMB sort order. `CAT_` prefix = compatible with GCLite conventions (and with GCL's filter/sorting if you run both).

## Installation

1. Copy `autocat.prx` to `ms0:/seplugins/`
2. Add to `ms0:/seplugins/vsh.txt`:

   ```
   ms0:/seplugins/autocat.prx 1
   ```

3. Reboot the PSP (or reload VSH plugins in the recovery menu)
4. AutoCat runs at boot, sorts everything, and appends a report to `ms0:/seplugins/autocat_report.txt`
5. Go to Game → Memory Stick — you'll see the `CAT_xx` folders

## Safety

- **Idempotent**: already-categorized games (`CAT_*` / `XX_*` folders) are never touched — only loose games get sorted
- Never touches: hidden dirs, non-game dirs, DLC, `/ISO` subfolders it doesn't recognize, saves
- Name collisions get `_2`, `_3`... suffixes instead of clobbering (ISO extensions preserved: `Game_2.iso`)
- If an `EBOOT.PBP` or ISO/CSO can't be parsed it's left exactly where it is
- Removing `autocat.prx` from `vsh.txt` undoes nothing — your games stay in their new folders (which is the point)

## Note

If you change the classification rules later and want to re-run on already-sorted games, you'd move the games out of the folders manually — AutoCat deliberately refuses to touch organized categories.

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