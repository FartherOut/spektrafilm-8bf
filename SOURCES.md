# Upstream sources

Where every external code/data source lives, how it is vendored, and what it
is used for. Two mechanisms: **git submodules** for what the build reads
directly (pinned to an exact commit, fetched with `git submodule update --init`),
and **`tools/fetch-sources.sh`** for upstream trees that are consulted when
porting instead of compiled (git-ignored, cloned on demand).

What each of those sources' licences requires of a distribution — the attribution
blocks, notices and full texts that must travel with the plug-in — is collected in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md). This file is about where things
come from; that one is about what we owe for them.

## Submodules (build inputs, pinned)

| Path | Upstream | Pinned at | Used for |
|---|---|---|---|
| `spektra-core/third_party/darktable-spektrafilm` | <https://github.com/darktable-org/darktable-spektrafilm> | `b15cfe1` | Shipping film packs: `packs/0.3.3` is the pristine source that `tools/sync_pack.py` derives the build input `spektra-core/data/pack` from (see below); the darktable `spektra` grain stage is the CPU baseline. |
| `upstream/spektrafilm-ofx` | <https://github.com/chaert-s/spektrafilm-ofx> | `86476afc` (main, 2026-07-06) | The OFX plug-in we port from: parameter surface, tooltips, shader reference. Its `Resources/data/profiles/archive` supplies the per-channel `bandpass_hanatos2025` that `tools/sync_pack.py` folds in while deriving the build pack — every build route runs the sync and stops if the archive is missing. |

Update a pin deliberately: `git submodule update --remote <path>`, build, run
the parity harness, then commit the moved pointer.

## The embedded data pack is derived, not stored

The film/paper pack the `.8bf` embeds is neither checked in nor hand-maintained:

```
darktable-spektrafilm submodule, packs/0.3.3   pristine, CC BY-SA 4.0
  + bandpass_hanatos2025 from the OFX profile archive
  = spektra-core/data/pack                      git-ignored build input
```

`tools/sync_pack.py` is the only writer of that directory, every build route runs
it before generating assets, and it fails when either upstream tree is missing.
Before it existed the build fell back to the pristine submodule pack whenever the
working copy was absent — which is exactly what a fresh clone sees — so a fresh
clone configured, built and passed tests while shipping a filter whose 20
bandpass-carrying film profiles rendered differently from the released one. The
stamp `spektra-core/data/pack.stamp` records the source pack, archive and tool
hashes it came from, so an unchanged rebuild copies nothing and leaves the
generated assets alone.

## Attribution, as upstream asks for it

The pack's licence (`SPEKTRAFILM_LICENSE.txt`, copied byte-for-byte into the
submodule as `LICENSE`) states what must travel with every copy and derivative —
"an app's About screen" is one of the places it names:

```
spektrafilm by Andrea Volpato
https://github.com/andreavolpato/spektrafilm
Licensed CC BY-SA 4.0
```

plus, for modified files, a note that they were modified and by whom. The pack we
embed is exactly that: 20 film profiles carry the `bandpass_hanatos2025` folded in
from the OFX archive. Both About surfaces (the ImGui dialog and the host's
plug-in About selector) therefore give the author, the URL, the licence and that
modification note.

Upstream's `CITATION.cff` also records Andrea Volpato's affiliation as **KTH Royal
Institute of Technology, Sweden**. That is bibliographic metadata for citing the
project in academic work — its `preferred-citation` entry omits the affiliation,
and the credit line the file asks plug-ins to show is only an acknowledgement of
`spektrafilm` — so it is noted here rather than taking a line in the dialog. The
acknowledgement it does ask for by name, "spectral film modelling powered by
spektrafilm", is the first line of the About dialog's acknowledgements.

## Reference-only trees (git-ignored, `tools/fetch-sources.sh`)

Consulted when porting rather than compiled; nothing in the build reads them,
so they are not pinned in git.

| Path | Upstream | What it is |
|---|---|---|
| `upstream/spektrafilm` | <https://github.com/andreavolpato/spektrafilm> | The original Python reference implementation (v0.3.4): `runtime/stages/*`, `model/*`, LUT creator. See `docs/EFFECTS_FAMILY_FINDINGS.md`. |
| `upstream/spektrafilm-rs` | <https://github.com/turbasvin/spektrafilm-rs> | A Rust port. `crates/spektrafilm-core/src/mallett.rs` is the source of truth for `tools/gen_mallett_basis.py`, which bakes the Mallett 2019 RGB→raw basis table into the engine as `spektra-core/third_party/darktable/spektra_mallett_basis.h`. The script is a maintenance-time generator, not a build step, so this tree does not need to be a submodule; `fetch-sources.sh` pins it to `v0.1.2` — the tag the committed header came from — so `gen_mallett_basis.py --check` keeps reporting something meaningful. |
| `upstream/vkdt` | <https://github.com/hanatos/vkdt> | Origin of the Hanatos sensitivity model (`src/pipe/modules/filmsim/`). |

## Vendored by file (not a submodule)

- `spektra-core/third_party/darktable/` — the darktable spektrafilm stage and
  support code, ported file-by-file into the engine (GPL-3.0-or-later, like
  darktable: its own notices read "either version 3 of the License, or (at
  your option) any later version").
- `hosts/ps8bf/third_party/imgui/` — Dear ImGui (MIT), vendored as the **complete
  v1.93.0 release tree**: 304 files copied in with the upstream `.git` removed, so
  there is no gitlink and no build-time fetch (decision D4, `docs/GUI_ROADMAP.md`).
  Upstream is <https://github.com/ocornut/imgui>, pinned to the `v1.93.0` tag —
  commit `c1aaac65e896a45f81f47a4d68f9959e0f86c859` (a lightweight tag, so the tag
  *is* that commit; 2026-10-14, `master` branch, not `docking`). `IMGUI_VERSION` in
  `imgui.h` reads `"1.93.0"`, and `diff -r` against a checkout of that commit
  leaves only the two patched files below: every other file is byte-identical.

  **Two files are patched**, each marked `[SPEKTRAFILM PATCH]` where it stands, and
  both for the same reason — keeping the static import table at four DLLs
  (decision D8, `docs/GUI_ROADMAP.md`):

  | File | Change | Why |
  |---|---|---|
  | `backends/imgui_impl_win32.cpp` | drops `#pragma comment(lib, "dwmapi")`; `ImGui_ImplWin32_EnableAlphaCompositing()` reduced to a no-op | no static `dwmapi.dll` import. Nothing calls that function, and the one DWM call this plug-in makes (caption colour) goes through `LoadLibrary("dwmapi.dll")` in `src/sf_gui_imgui.cpp` |
  | `backends/imgui_impl_dx11.cpp` | drops `#pragma comment(lib, "d3dcompiler")` | no static `d3dcompiler_47.dll` import: `D3DCompile` is redirected to `sf_d3d_compile_shim()`, which `LoadLibrary`s the DLL at runtime. The redirect is a per-source compile definition in `hosts/ps8bf/CMakeLists.txt`, mirrored in `build-mingw.sh`, so MSVC and MinGW build the same way |

  Nothing else in the tree is modified. MIT permits modifying the code; its
  obligation is the notice, which `LICENSE.txt` keeps unmodified.

  The build compiles six translation units — `imgui.cpp`, `imgui_draw.cpp`,
  `imgui_tables.cpp`, `imgui_widgets.cpp`, `backends/imgui_impl_win32.cpp`,
  `backends/imgui_impl_dx11.cpp` — and the preprocessor opens fourteen files in
  this tree in total: those six plus the eight `imgui`/`imconfig`/`imstb` headers
  they include. `LICENSE.txt` is the only other file that matters; the remaining
  289 (51 more files under `backends/`, `examples/`, `docs/`, `misc/`,
  `imgui_demo.cpp`, ImGui's own repo config) are read by nothing in the build.
  The tree is kept whole deliberately: upgrading is copying the next release over
  the folder, reading the `git diff` — which is also how the two patches above are
  re-found and re-applied — and a vendor that matches an upstream release
  file-for-file is easier to audit than a hand-picked list.

  One trap when re-vendoring: ImGui's own `.gitignore` arrives with the tree and
  lists `gradle/`, so `examples/example_android_opengl3/android/gradle/`
  `libs.versions.toml`, which upstream does track, is silently skipped by
  `git add -A` and needs `git add -f`.
- `hosts/ps8bf/pi/` — this project's own declaration of the `.8bf` ABI layout,
  written from Adobe's published headers as the reference; no Adobe SDK files are
  vendored (see `hosts/ps8bf/INSTALL.md` and the ABI section of
  `hosts/ps8bf/README.md`).

All upstreams are GPL-3.0-compatible, so the combined work stays
GPL-3.0-or-later (see `LICENSE`).

Every file this project writes carries `SPDX-License-Identifier: GPL-3.0-or-later`
on its first line (a `#` comment in scripts, `/* */` in C and `.rc`) — new files
should too. Vendored trees keep whatever notices they arrived with, unmodified:
darktable's files state their own grant, Dear ImGui's `LICENSE.txt` is MIT, and
the data pack's `LICENSE` is the CC BY-SA 4.0 text the author asks not to be
changed. Where a file of ours contains code copied verbatim from darktable
(`spektra-core/shims/dt_stubs.c`) the header says which functions and that they
remain © the darktable developers.
