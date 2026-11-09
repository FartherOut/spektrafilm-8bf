# Building

Status: **current-state, 2026-11-09.** The shipping build is MSVC on Windows;
the Linux cross build is a validation route, never the artifact.

## Prerequisites (Windows, shipping)

- Visual Studio 2022 or 2026, any edition (Community is fine), with the
  "Desktop development with C++" workload.
- CMake >= 4.2 for the `"Visual Studio 18 2026"` generator; older CMake is
  fine for the VS 2022 generator or Ninja.
- Python 3 on PATH (the embedded pack resources and the PiPL are generated at
  configure time).
- The two build-required submodules initialised:
  `git submodule update --init`
  (`spektra-core/third_party/darktable-spektrafilm` = the data pack,
  `upstream/spektrafilm-ofx` = the archived Hanatos2025 bandpass; both pinned,
  see `SOURCES.md`).

## Shipping build (MSVC)

```powershell
cd hosts\ps8bf
.\build.ps1                                  # Release, newest installed VS
.\build.ps1 -Generator "Visual Studio 17 2022"   # pin VS 2022 (v143)
.\build.ps1 -Generator Ninja -Configuration Debug
```

`build.ps1` locates `vcvars64.bat` itself - a "Native Tools" prompt is not
required - and after building stages `dist\spektrafilm.8bf` plus `LICENSE`
and `THIRD-PARTY-NOTICES.md` (GPLv3 s5 and the pack's CC BY-SA attribution
ask that notices travel with the copy; the About dialog is the short form,
these files the long one).

CMake directly works too (`CMakePresets.json` carries `msvc`, `msvc2022` and
`mingw64` presets):

```
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

### What the build embeds

At configure/build time the scripts derive everything the DLL carries:

1. `tools/sync_pack.py` derives `spektra-core/data/pack` from the two pinned
   trees (stamp-based; it **fails** rather than falling back to the pristine
   pack - that fallback once built fine while silently changing how 20 film
   profiles render).
2. `tools/gen_assets.py --win` emits `asset_ids.h`, `pack_list.h` and
   `pack.rc` (every pack file as `RCDATA`, base resource id 3000; the spectra
   LUT re-encoded float16 -> float32).
3. `hosts/ps8bf/tools/gen_pipl.py` emits `res/pipl.bin` (PiPL resource id
   16000, layout verified against Adobe's `Cnvtpipl.exe` under Wine).

### Output invariants (enforced, not hoped for)

The staged DLL must be: CRT-static (`/MT`), importing **only**
`KERNEL32.dll`/`USER32.dll`/`GDI32.dll`, with the GPU stack
(d3d11/dxgi/d3dcompiler_47) `LoadLibrary`'d at runtime (decision D8).
`build.ps1` runs `dumpbin /dependents` and fails loudly on any UCRT/VC
runtime import (that means a mingw validation DLL or a `/MD` build got
staged), on any static GPU import, and on anything outside the allow-list.
Staging uses one deterministic path per generator - never a recursive glob
(a stale `build-mingw.sh` output was staged by exactly such a glob once).

### The build report and the warning baseline

`build.ps1` prints a full report: generator and VS version, cmake/cl/python
versions, pack provenance from `pack.stamp` (src and band content hashes,
file and profile counts), warning and error counts broken out **per code**,
artifact path/size/sha256, import list, log path. Two deliberate choices:

- **Nothing is suppressed or swallowed.** No `/wd<code>` anywhere
  (`CMakeLists.txt` says so out loud), no `-Quiet`, no captured stderr; the
  tools run on the console directly so MSBuild keeps its own colouring.
- **The warning baseline is exactly five `warning C4068`**
  (`-Wunknown-pragmas` on GCC/mingw): the vendored `grain.h`'s
  `#pragma STDC FP_CONTRACT` (plus `#pragma omp` / `#pragma GCC optimize` in
  `spektra_core.c`) in the five translation units that include it. The
  vendored code stays unmodified, so the notices stay in the report - the
  policy is to fix causes, never to silence. A **new code** in the report
  means new diagnostics and needs investigating; the counts are scraped from
  `build\msbuild.log` (stopping at the `Build succeeded` banner) rather than
  trusted from its summary, and deliberately not deduplicated - the five
  identical C4068s are five real per-translation-unit diagnostics.

The `sha256` line names *this file*, not this run: the linker stamps a
TimeDateStamp into the PE header, so two builds of identical sources hash
differently. Compare counts and sizes between runs; compare the hash between
two copies of one artifact (e.g. after a transfer to another machine).

## Validation build (Linux / CachyOS, mingw-w64)

```bash
pacman -S mingw-w64-gcc          # once
hosts/ps8bf/build-mingw.sh       # -> hosts/ps8bf/build/spektrafilm.8bf
```

Same sources, same x64 `FilterRecord` ABI, same embedded PiPL/RCDATA - but
the DLL imports the mingw runtime instead of being CRT-static, so it is a
compile-and-logic check, never the shipping artifact (the `dumpbin` guards
above are what keep the two apart). The Linux test harness
(`hosts/ps8bf/test/run.sh`, see `docs/TESTING.md`) is the real Linux gate.

## Known traps

- **Paths with backslashes**: `gen_assets.py` doubles native backslashes in
  the emitted `.rc` because `rc.exe` processes C-style escapes inside quoted
  filenames (`\t` in `F:\temp\...` becomes a TAB, `\b` in `\build` a
  backspace, error RC2135). Building from such a path is supported; hand
  editing generated `.rc` files is not (they are generated).
- **Tool output is ASCII**: build logs and script output stay 7-bit so they
  survive the OEM codepage of a Windows console without mojibake.
- **A fix "does not take"**: read `%TEMP%\spektrafilm.log` - the banner line
  carries version, build stamp, pid and the full module path the host loaded,
  which identifies a stale copy on the host's plug-in directory.
- **Fresh clone from a local path** (simulating a first checkout): git blocks
  file:// submodule clones by default; use
  `git -c protocol.file.allow=always clone ...`. Ordinary GitHub clones are
  unaffected.
- **Windows clones and line endings**: the repo's `.gitattributes` checks its
  own text files out with LF on every platform, but the two submodules are
  governed by their own repositories and neither carries a `.gitattributes`.
  A clone made with `core.autocrlf=true` therefore checks the submodule pack
  JSONs out with CRLF, and `tools/sync_pack.py` hashes raw bytes: the build
  report then prints pack stamp hashes that differ from the recorded ones.
  The render is byte-identical (the JSON parser does not care); only the
  printed hashes move. Clone with `core.autocrlf=false` or `input` to keep
  the hashes comparable.
