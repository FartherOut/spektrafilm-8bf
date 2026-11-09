# hosts/ps8bf — Windows `.8bf` adapter

Shipping host. A Photoshop-format 64-bit filter plug-in (`PluginMain` + a
binary PiPL + a `FilterRecord` ABI + a Dear ImGui/Direct3D 11 modal dialog) that
translates host selectors into the core's `SpektraAppBridge`-shaped C ABI
(`spektra-core`).

Scope per ROADMAP §2.1/§6: stills only, **space-preserving output** (the render
is written back in the document's input colour space instead of being forced to
sRGB), 16-bit primary / 8-bit secondary / 32-bit stretch, float maths + dither
at quantise, the whole selection processed as one full-frame tile.

## Status

> **v0.1.0 — feature complete (2026-11-04).** Signed off in Affinity Photo on Windows 11; the full
> film-simulation surface (ROADMAP §2.1) is implemented, the ImGui dialog + live preview are
> polished, and the plug-in ships as a single self-contained `.8bf`. Release notes: `CHANGELOG.md`,
> `docs/GUI_ROADMAP.md` §13.

Current-state documentation lives in `docs/` (index: `docs/README.md`):
`ARCHITECTURE.md` (layers and data flows), `BUILDING.md` and `TESTING.md`
(both routes, both gates), `PARAMETERS.md` (the parameter contract and
guardrails), `CONVENTIONS.md` (rules and release process). This README stays
the adapter-centric technical hub and validation log.

**Builds and links end-to-end on Linux/CachyOS via mingw-w64** (validation
build). The MSVC shipping build (static CRT, imports only
KERNEL32/USER32/GDI32) is in place and loads in Affinity Photo; engine
output is byte-identical to `tools/refrender` on the same input (see `test/`).

Verified on this machine (mingw-w64 GCC 16.2):
- The whole engine + shims + adapter compile and link to `spektrafilm.8bf`.
- The DLL exports `PluginMain`, embeds the binary PiPL and the ~14 MiB RCDATA
  data pack.
- `FilterRecord` static asserts (`pi/pifilter.h`) pass under the Windows x64
  ABI — all 121 offsets + `sizeof == 608` — against the real SDK header layout.
- `res/resource.rc` compiles with windres *and* llvm-rc; the embedded PiPL is
  byte-identical to `res/pipl.bin`.

## Layout

```
pi/pi_types.h     minimal PI scalar/handle types (self-contained, no Adobe SDK)
pi/pifilter.h     FilterRecord + Handle/Procs suites + FilterCaseInfo
                   (121 x64 offset static asserts, sizeof == 608)
src/plugin_main.c PluginMain entry, DllMain, selector dispatch, About box
src/params.*      persisted parameter block (host `parameters` Handle)
src/preset.*      preset codec: sf_params_block <-> portable JSON (no disk/Win32)
src/pix_convert.* 8/16/32-bit <-> float sample mapping (byte-linear, refrender)
src/filter_loop.* selector Start/Continue/Finish bodies over sp_chain
src/sf_gui.*      the sf_run_params_dialog seam + shared document-profile detection
src/sf_gui_imgui.cpp  Dear ImGui + Direct3D 11 parameter dialog (the only backend)
src/sf_d3d11.*    dynamically loaded D3D11 device/swap-chain + preview texture
src/sf_preview.*  preview render: whole-image fit + 1:1 crop (chart fallback)
src/sf_parallel.* Win32 thread runner (CreateThread) for the engine's stage/loop hooks
src/assets_win.c  sf_asset_lookup() over Win32 RCDATA resources
third_party/imgui vendored Dear ImGui v1.93.0 (core + win32 + dx11 backends)
res/resource.rc   PiPL + VERSIONINFO
res/pipl.bin      generated binary PiPL (210 bytes)
res/plugin.def    exports PluginMain
tools/gen_pipl.py emits res/pipl.bin (byte-identical to Adobe Cnvtpipl.exe)
CMakeLists.txt    shipping build (MSVC /MT, x64)
build.ps1         MSVC build + stage dist/spektrafilm.8bf (+ LICENSE, THIRD-PARTY-NOTICES.md)
build-mingw.sh    Linux/CachyOS cross build (validation)
build/gen/        generated: asset_ids.h, pack.rc, pack_list.h, spectra_lut_f32
```

## Building on CachyOS (Linux cross build, validation)

This produces a working DLL for ABI/logic validation. The *shipping* artifact
must still be produced with MSVC on Windows.

Required packages:

```sh
sudo pacman -S mingw-w64-gcc python
```

Then:

```sh
cd hosts/ps8bf
./build-mingw.sh          # -> build/spektrafilm.8bf
```

The script runs `tools/gen_assets.py --win`, regenerates `res/pipl.bin`, then
compiles the engine + shims + adapter and compiles/links both resource scripts,
and ends with a `== build report ==` (toolchain, source counts, warnings per
diagnostic code, pack provenance, artifact size and sha256, imports, entry point).
The resulting DLL imports the mingw UCRT (not the static CRT); that is fine for
validation and is the only intended difference from the shipping build.

Alternatively, drive the same project through CMake (it also works with the
mingw toolchain, which is how this was validated):

```sh
cmake -S . -B build/x -G "Unix Makefiles" \
      -DCMAKE_SYSTEM_NAME=Windows \
      -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
      -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres
cmake --build build/x
```

## Building on Windows (shipping, MSVC)

Prereqs: Visual Studio 2022 or 2026 (any edition — the free **Community**
edition is enough) with "Desktop development with C++", CMake **4.2+** for the
VS 2026 generator (note: VS 2026 18.0 bundles CMake 4.1.1, which does *not*
know that generator — install a current standalone CMake), Ninja (or the VS
generator), Python 3 on PATH. From a normal PowerShell:

```powershell
cd hosts\ps8bf
.\build.ps1                     # Release -> dist\spektrafilm.8bf
                                # (auto-detects the newest installed VS: 2026 > 2022)
.\build.ps1 -Generator "Visual Studio 17 2022"   # pin VS 2022 if both are installed
```

stages `dist\spektrafilm.8bf` with `LICENSE` and `THIRD-PARTY-NOTICES.md` beside
it, then prints a `== build report ==`: generator, CMake and `cl.exe` versions,
the embedded pack's provenance, the warning count broken out per diagnostic code,
the artifact's size and sha256, and the import table (expected: `KERNEL32.dll`,
`USER32.dll` and `GDI32.dll` only — no CRT DLLs, thanks to `/MT`). GDI32 is
ImGui's Win32 backend querying the display DPI through a GDI DC; the backend adds
no other imports of its own (Direct3D 11/DXGI/D3DCompile and the DWM chrome are
all resolved with `LoadLibrary`). The retired native
dialog's halation colour picker used COMDLG32; after retiring it, the shipping
build imports only KERNEL32/USER32/GDI32.

Nothing is filtered out of that console: the pack derivation, CMake, MSBuild,
`cl.exe` and `link.exe` each write their standard output in full, and MSBuild also
keeps the complete log at `build\msbuild.log`. No compiler warning is suppressed
either, so the count in the report is the real one (see the unknown-pragma note
under Validation status).

The real CRT supplies the printf/strtod family on Windows. `stdio_asset.c` *is*
linked so the engine's file reads resolve against the RCDATA pack. It exposes
`sf_asset_fopen`/`fread`/`fclose`/... (not CRT-named entry points, which would
collide with the static `/MT` CRT: `LNK2005`); the engine translation units have
the CRT names mapped onto them at compile time, while host TUs (the transcript
log, the GUI dialog) keep the real CRT stdio.

## PiPL

`res/pipl.bin` is the binary Plug-in Property List Photoshop/Affinity read to
discover the plug-in without executing it. It is generated by
`tools/gen_pipl.py`, which reproduces Adobe's `Cnvtpipl.exe` output
**byte-for-byte** (verified). Properties: Kind `8BFM`, Name "spektrafilm",
Category "Film Simulation", Version, `CodeWin64X86 = "PluginMain"`,
`SupportedModes = RGB`, and 7× `FilterCaseInfo` (inWhiteMat/outStraightData,
filtersLayerMasks + worksWithBlankData). The Category (`catg`) drives the host
menu: Affinity Photo always nests each `.8bf` under a submenu named after it,
with the item labelled by Name, so it must differ from the Name to avoid a
redundant "spektrafilm ▸ spektrafilm"; a descriptive category keeps a single
"spektrafilm" item under a named group (Pixel ▸ Filters ▸ Plugins ▸ Film
Simulation ▸ spektrafilm), the convention G'MIC uses. It is embedded by
`res/resource.rc` as:

```rc
16000   PiPL    "pipl.bin"
```

The bare `PiPL` token (a user-defined resource *type*) is exactly what
`Cnvtpipl.exe` emits and compiles under MS rc.exe, llvm-rc and windres.

> Modern Photoshop also accepts a JSON PiPL embedded as a `JSON`-typed resource
> (`JSON_PIPL JSON "PiPLs.json"`, see the current SDK samples). The binary PiPL
> is still supported by Photoshop and by Affinity Photo; adding the JSON
> resource is a possible future compatibility add-on.

## About the .8bf ABI

A `.8bf` plug-in and its host agree on a binary layout, not on source: the host
fills in a `FilterRecord` struct and calls the exported `PluginMain` with a
pointer to it, so our declaration of that struct has to match Adobe's field for
field, byte for byte, under `#pragma pack(push,4)`. Get one field's type wrong and
everything after it shifts — the plug-in reads image dimensions out of what is
really a function pointer.

This adapter needs **no Adobe downloads**: `pi/pi_types.h` + `pi/pifilter.h`
declare that layout ourselves, from Adobe's published SDK headers as the
reference, and the PiPL is generated locally by `tools/gen_pipl.py`. Only what a
stills filter uses is declared — the six selector calls, the parameter handle,
plane/rect data access, `HandleProcs` / `advanceState` / progress / abort, and the
`'fici'` PiPL constants; the PICA suites are deliberately absent. 121
`SF_OFF(field, n)` asserts pin `offsetof(FilterRecord, field)` to the published
offsets at compile time, so a layout mistake breaks the build instead of
corrupting pixels. The layout was cross-checked against the published
`pluginsdk/photoshopapi/photoshop/PIFilter.h`, which uses
`#pragma pack(push,4)` — matching our `pack(4)`.

Adobe's published headers, samples and `Cnvtpipl.exe` are mirrored in the
`AdobeDocs/photoshop-cpp-sdk` GitHub repo under a proprietary Adobe SDK
licence. They served only as a public reference for the ABI; **no Adobe SDK
files are vendored or distributed** in this GPL-3.0-or-later project, and none is
needed to build.

Older notes here called this a "clean-room re-derivation". *Clean-room* is the
term of art for an implementation written by someone kept away from the original,
which overstates it: what happened is the ordinary route to interoperability —
the documented layout was re-declared from its published description, and only
the parts a stills filter needs were written down. The claim that matters is the
same either way: no Adobe code in the tree, nothing Adobe-licensed to pass on.

The About selector deserves a note of its own: the plug-in has two About
surfaces — the modal behind the About button in its window, and the minimal
host-side box the `filterSelectorAbout` selector draws — and Affinity never
asks for the second. What each shows, and how to force it anyway:
`docs/ABOUT_BOXES.md`.

## Licence and notices

This project's own code is **GPL-3.0-or-later** ([`LICENSE`](../../LICENSE));
every file it writes carries that as an SPDX line. What the plug-in embeds or
derives from, and what each of those licences asks to have reproduced, is
collected in [`THIRD-PARTY-NOTICES.md`](../../THIRD-PARTY-NOTICES.md): the
darktable `spektra` stage the engine is ported from (GPL-3.0-or-later), the film
and paper data pack (CC BY-SA 4.0, © Andrea Volpato — whose attribution and
modification note the About dialog shows, because its licence names an app's
About screen as a place they belong), and Dear ImGui (MIT, vendored under
`third_party/imgui`). `build.ps1` stages `LICENSE` and `THIRD-PARTY-NOTICES.md`
into `dist\` next to the `.8bf`, so the notices travel with the binary.

## Parameter dialog

A single dialog sits behind the `sf_run_params_dialog` seam in `src/sf_gui.c`.
It edits the `sf_params_block` directly, so a filter invocation round-trips
through the same block the render consumes. The original Win32 template dialog
was retired in phase B6 (see
`docs/GUI_ROADMAP.md`); the only backend now is Dear ImGui + Direct3D 11.

`src/sf_gui_imgui.cpp` draws the whole dialog with the vendored Dear ImGui
v1.93.0 (`third_party/imgui`) through its stock `imgui_impl_win32` +
`imgui_impl_dx11` backends. It creates a native-framed top-level window (client
area fully ImGui-drawn) with its own modal message loop, and renders on demand
(when input arrives or state changes), so an idle dialog costs nothing.

- **Live preview** as an RGBA8 Direct3D 11 texture, produced by the existing
  `sf_preview` display transform (fit / 1:1 centre crop, with 1:1 panning).
- **Every numeric setting is a slider** with an editable buddy box (min/max from
  the OFX parameter range, reset value = the engine-neutral default); a middle-
  or right-click on the slider resets it to the default, and every control has a
  hover tooltip (including its default and range). With the cursor over a
  slider, the mouse wheel nudges the value (1% of the range per notch, Shift for
  0.1%) instead of scrolling the pane; the pane still scrolls when the wheel is
  over empty space. The preview waits for the gesture to end rather than for a
  pause in it: a slider held under the button renders nothing until the button
  is released, and a rolling wheel renders once the rolling stops.
- **Settings are grouped** into collapsible sections (Presets, Colour
  Management, Engine, Film, DIR Couplers, Print, Halation, Diffusion, Grain,
  Scanner, Camera Filters) mirroring the original dialog 1:1.
- **Theme.** A flat, modern dark/light style; dark is the default. Switch it with
  the *Theme* button in the header or `SPEKTRAFILM_GUI_THEME=dark|light`.
- **Keyboard.** Tab and the arrows move focus, Esc cancels. No key applies the
  settings: OK and Cancel are outside keyboard focus, so applying the dialog
  always takes a click on OK, on Cancel, or on the title-bar X. In a number
  field, Enter or Tab finishes that field only.
- **Windows 11 chrome.** Rounded frame corners and a theme-matched immersive
  title bar via `DwmSetWindowAttribute`. Per-monitor DPI changes are handled
  (`WM_DPICHANGED` rescales the style and the per-monitor font size between
  frames).
- **Import guard.** The GPU stack (`d3d11.dll`/`dxgi.dll`/`d3dcompiler_47.dll`)
  and `dwmapi.dll` are reached through `LoadLibrary`, so the shipping `.8bf`
  statically imports only KERNEL32/USER32/GDI32.

### Presets

The **Presets** group (first group in the settings pane) saves a whole look and
brings it back:

- **Save / Load / Delete** act on the name in the *Name* field; picking an entry
  from the *Preset* combo fills that field. Saved looks live in
  `%APPDATA%\spektrafilm\presets\<name>.json` and are found by `FindFirstFile`.
  The name is sanitised for the filesystem (letters, digits, space, `-`, `_`,
  `(`, `)`; reserved device names like `CON` get a `_` prefix), and both load
  and delete sanitise too, so `..\..` is a no-op rather than a traversal.
- **Copy / Paste** move a look through the clipboard as preset text, so it can
  be shared, pasted into a message, or kept in a document next to the edit.
  Copy publishes the *current* settings, not the selected file.
- **Defaults** restores `sf_params_init` (the factory look).

The JSON itself is produced by `src/preset.c` (`preset.h`), a pure codec with no
disk or Win32 in it, so the Linux harness round-trips it. It is a flat object:

```json
{
  "format": "spektrafilm-preset",
  "version": 1,
  "name": "Punchy Portra",
  "params": {
    "film": "kodak_portra_400",
    "paper": "kodak_2383",
    "exposure_ev": -0.65,
    "grain_particle_scale": [1.25, 1, 0.75]
  }
}
```

Three properties are worth knowing:

- **Exact.** A number is written in the shortest decimal form that reads back
  bit-exact, so loading a preset never nudges a value and the render stays
  byte-identical to what the dialog showed. Both directions are locale-proof —
  the separators are swapped around `strtod`/`snprintf` rather than by touching
  the process locale, which the host owns.
- **Portable.** `film`/`paper` are pack *stems*, not indices, so a preset
  survives the pack being reordered or extended; a film that has since left the
  pack keeps the default instead of producing an out-of-range index.
- **Tolerant in both directions.** Keys this build does not know are ignored
  (a preset from a later build still loads), and a value that cannot be
  represented — a string where a number goes, `1e400`, a fractional integer —
  keeps the default rather than inventing one. Only the structure is strict:
  foreign JSON, a missing `format`, an unknown `version` or a malformed object
  is refused outright, leaving the block untouched.

Fields that are *not* written, because they describe a document or a frame
rather than a look: the block header (`magic`/`version`), the document-derived
input matrix (`input_custom_*`), the host-metered `auto_exposure_ev` and the
trailing padding. A field added to `sf_params_block` must be added to the field
table in `preset.c` or presets keep the engine default for it (see the note in
`params.h`).

### Startup failure

If the Direct3D 11 device cannot be created (no usable hardware or WARP software
renderer), `sf_gui.c` shows an error message and cancels the filter rather than
running with no UI. There is no native fallback any more.

### Shared details

Things worth knowing:

- **Display names.** The film/paper combos show the profile's own `info.name`
  ("Kodak Portra 160"), not the on-disk stem (`kodak_portra_160`). The label
  arrays are generated into `pack_list.h` by `tools/gen_assets.py` and stay
  index-aligned with `SF_FILMS` / `SF_PAPERS`, so the persisted `*_index` values
  are unaffected.
- **DIR coupler controls.** `Amount` and the same/inter-layer `Inhibition`
  values scale the engine's coupler matrix directly (defaults 1.0, i.e. the
  previous behaviour). The spatial `Diffusion (um)` is the caller-side blur.
- **Film-look stages.** A second and third column expose the full Phase-2
  spatial surface, wired to the same caller-side stages as the darktable iop —
  in pipeline order: highlight **boost**, **camera diffusion**
  (Black Pro-Mist / Glimmerglass / Pro-Mist / CineBloom), **halation/scatter**
  (pre-film, linear), **production grain** (amount, clump blur, granularity,
  uniformity, sub-layer scale, density floor, dye-cloud size, recovery
  radius/amount), **print diffusion**, then **scanner lens blur / unsharp mask /
  viewing glare**. Every stage **defaults off**, so an unedited block renders
  byte-for-byte the same as the previous build; enable a stage with its
  checkbox (or a non-zero radius/glare) to add it. See `ROADMAP.md` Phase 2 and
  the `sp_chain.h` stage notes.
- **Live image preview.** The dialog runs from `filterSelectorContinue` (the
  `gmic-8bf` pattern), *after* the host has handed over the selection, so the
  `Preview` box shows the user's own image rendered through the same `sp_chain`
  as the final render. Two view modes (radio buttons):
  - **Whole image** (default) — supersampled: the frame is downsampled to up to
    2x the control size, rendered at that resolution with the pixel pitch
    scaled to match, and the *rendered result* is shrunk to the control, so
    halation sources and grain survive the shrink and the look closely matches
    the final render.
  - **1:1 crop** — the control-sized region at native pixel scale, centred on
    the pan point (drag to move); every preview pixel matches the full render
    at 100% zoom. The spatially exact view.

  Re-renders are debounced because each one builds a chain (LUTs): 120 ms after
  a change that arrives as a single edit (a committed number field, a dropdown,
  a reset), and about 0.7 s after the last mouse-wheel notch, so a rolling wheel
  renders once the rolling stops. A slider held under the mouse button defers
  the render until the button is released, however often the pointer pauses on
  the way. The 1:1 pan drag is debounced the same way and by the same rule:
  every frame of it re-arms the 120 ms wait, so the crop moves under the cursor
  at once and the pixels follow when the drag stops.
  When no pixels are available it falls back to the built-in reference scene
  (`sf_preview_render`).

### Diagnostics

Every `PluginMain` selector appends a line to `%TEMP%\spektrafilm.log` (path set
in `plugin_main.c`, written through `filter_loop.h` helpers). The first selector
in a process also writes a banner — product/version, a build stamp
(`SF_BUILD_STAMP`, `__DATE__ __TIME__` unless overridden), the process id and the
full path of the DLL the host loaded (`GetModuleFileName`). So merely opening the
filter's dialog creates the file, and a stale install is immediately visible
from the banner's path/stamp.

Each line records who was called and what the host passed (`filterRect` /
`filterRect32`, whether `inData`/`outData` were supplied and their row bytes,
depth/mode, and the rect/tile rendered). If a host rejects the filter with "host
supplied no pixel buffer", this log is the first thing to check: it shows which
rect family carried the selection, whether the host copied the source into
`outData` only, and whether the `PULL` fallback ran.

### Pixel handshake (Affinity Photo)

Photoshop pre-fills `inData`/`outData` before `filterSelectorContinue`. **Affinity
Photo does not** — it only fills the tiles a plug-in asks for through
`AdvanceStateProc`, which is why the known-good `gmic-8bf` requires
`advanceState != nullptr` and drives its whole read/write through it. The loop
supports both: if the host hands over buffers on Continue it uses them as before
(one full-selection tile); if it hands back neither buffer it falls back to the
pull model — gather the selection plane-by-plane in host-sized tiles, render the
whole frame once (the DIR-coupler blur seams if cut), then write it back plane by
plane. The mock harness exercises both and asserts byte-identical output
(`test/harness.c`, `test_pull_path`).

## Performance and threading

The engine's stages are embarrassingly parallel per pixel (expose / develop /
print / scan) and its blur passes are row- or block-independent, so the adapter
splits them across a small Win32 worker pool:

- `sp_chain` exposes a `sp_chain_set_parallel()` hook; the adapter installs a
  range splitter there so every element-wise batch runs as N contiguous ranges.
- `spektra_core` exposes `sf_set_loop_runner()` so the DIR-coupler blur
  (transpose, 1-D row passes, wide-blur down/up, channel pack/unpack) runs the
  same way. Rows/blocks write disjoint regions, so the result is **bit-identical**
  to serial.
- The pool is plain `CreateThread` (kernel32) — **no OpenMP**, which would add a
  `vcomp140.dll` import and break the KERNEL32/USER32/GDI32-only guard. The
  worker count comes from `GetSystemInfo`; `SPEKTRAFILM_THREADS=n` overrides it
  (`1` = serial). Small images stay serial (`1<<16` px floor) so trivial
  invocations don't pay thread setup.

Measured on this machine (1600×1200, 12 cores), byte-identical to serial:

| path                         | time   |
|------------------------------|--------|
| serial                       | 2.34 s |
| threaded (12 workers)        | 0.48 s |
| threaded (4 workers)         | 0.95 s |

That is ~4.9× on 12 cores; a 48 MP render that took ~45 s serial should land
near ~9 s at 12 cores (memory-bandwidth limited, so scaling is sublinear).

### Live preview

The dialog used to run from `filterSelectorParameters`, where the host has handed
over no image rects/pixels yet (Affinity only fills tiles via `AdvanceStateProc`),
so it could not show the user's image. It now runs from the render loop
(`filterSelectorContinue`) once the selection has been gathered: `filter_loop.c`
calls an optional dialog hook (installed by `plugin_main.c`, left NULL in the
Linux harness) between decoding the frame to float and rendering it. The hook is
only invoked for interactive invocations — a "Last Filter" replay skips
`filterSelectorParameters`, and the dialog stays out of the way so the stored
settings apply silently. See `sf_preview.c` for the preview render and the
`Parameter dialog` section above for the crop/scale details. The box shows the
whole image (fit) or a 1:1 centre crop, chosen with the *Whole image* / *1:1
crop* radio buttons.

Space-preserving output means the chain hands the preview its samples in the
document's own colour space. The preview control is a plain, unmanaged bitmap, so
`sf_preview_display()` converts document -> sRGB before quantising (linearise
with the document curve, to XYZ, a Bradford D50/D65 adaptation, then the sRGB
matrix + curve) — i.e. what a colour-managed viewer would show. When the document
is already sRGB with the sRGB curve this is a byte no-op.

## Spatial look stages (Phase 2)

`sp_chain` now renders the full darktable iop order, with the caller-side spatial
stages the engine intentionally omits:

```
boost -> camera diffusion -> halation/scatter   (pre-film, linear)
expose -> lograw -> develop_corr -> [DIR coupler blur] -> develop
grain (on the developed CMY density)
print_expose -> [print diffusion] -> print_develop -> scan
[scanner blur] -> [scanner unsharp mask] -> [viewing glare]
```

- **Default off.** Every stage's enable flag defaults to 0 in `sp_params.c`, so
  the default render is byte-identical to the previous slice — the harness's
  refrender parity test still passes unchanged. Turn a stage on from the dialog.
- **Wiring only.** The engine helpers (`sf_boost_highlights`, `sf_diffusion_filter`,
  `sf_halation`, the grain model, `sf_blur_plane3`, `sf_unsharp_mask3`,
  `sf_multiplicative_unsharp_mask3`, `sf_glare`) were already compiled into the
  port; the port adds the params, the `pixel_um`-based scaling and the ordering.
  Grain runs through the same production model as the reference (per-sub-layer
  particle draw, dye-cloud blur, clump blur, multiplicative unsharp recovery).
- **Threading.** The element-wise grain draw runs through the range splitter; the
  blurs run through `sf_loop_runner`. The all-stages-on render is asserted
  **byte-identical threaded vs serial** (`test/harness.c`, `test_phase2_stages`).
- **Grain scaling (reviewed 2026-10-16, kept as-is).** Grain size across film
  gauges derives from `pixel_um = format_mm·1000/long_edge_px` and
  `sp_grain_format_scale(mm) = (mm/35)^0.62`; the clump/final blur is applied in
  fixed pixels scaled by that curve. A review of the resulting small-gauge grain
  (Super 35 ≈ 2× the 35 mm RMS; 16 mm/Super 16 breaking into pixel speckle) and
  of the dormant OFX `grainMicroStructure` field concluded **no change is
  wanted**. No grain parameter, table entry, or stage was modified. Full
  rationale in `docs/GRAIN_SYSTEM_ROADMAP.md` §*Format scaling review*.
- **Film chemistry (wired).** Bleach bypass, leuco-cyan coupling, film push/pull
  *mode* and print bleach bypass are engine-backed fields
  (`sp_chain_params_t` → the `sp_chain.c` negative/print chemistry stages) and are
  exposed in the dialog's Film/Print chemistry sections. All are no-ops at their
  defaults, so the default render stays byte-identical. (Earlier revisions of this
  port called them OFX-only; that was before the engine grew the matching stages.)

## Locked-scope extras

The v1 scope (`ROADMAP.md` §2.1) names a few parameters the engine already
carries but the first dialog did not expose. These are now wired and persisted
in the params block (v19), each a no-op at its default so the default render
stays byte-identical to `tools/refrender`:

| group                | fields                                                | default (no-op)          |
|----------------------|-------------------------------------------------------|--------------------------|
| **DIR chroma**       | `gamma_samelayer[3]`, `gamma_inter_{r_gb,g_rb,b_rg}[2]` | 0 = the stock's own      |
| **Filtration**       | `m_filter_shift`, `y_filter_shift` (M/Y trim)         | 0 = neutral              |
| **Preflash**         | `preflash_exposure` + preflash M/Y shifts             | 0 = off                  |
| **Halation colour**  | per-channel return weights `[3]`                      | -1 = the film's own      |
| **Scanner point**    | black/white correction on/off + levels                | on, 0.01 / 0.98          |
| **Film chemistry**   | `film_push_pull_stops`, `film_gamma`                  | 0 / 1 = stock            |
| **Print chemistry**  | `print_gamma`                                         | 1 = paper                |
| **Film (extended)**  | `film_format`, `film_push_pull_mode`, `negative_bleach_bypass_amount`, `negative_leuco_cyan_coupling` | 0 / 0 / 0 / 1 = stock |
| **Auto exposure**    | `auto_exposure`, `auto_exposure_method`               | off                      |
| **Print (extended)** | `print_bleach_bypass_amount`, `print_push_pull_stops`, `print_shadow_shape`, `print_highlight_shape` | 0 = paper |
| **Diffusion detail** | camera + print core/halo/bloom intensity + size       | 0 = family profile       |
| **Scanner (extended)** | `scan_mtf50_lpmm`, `scan_usm_radius_um`             | 0 = pixel-based          |
| **RGB→raw method**   | `rgb_to_raw_method`                                    | 2 = Hanatos 2026         |

The DIR chroma initialiser seeds the stock's fitted coefficients first, so `0`
really means "the stock's own" for the entries you leave alone while still
overriding the ones you set. The halation weights follow the same idea (`-1`
keeps the film's own). The chemistry group maps onto the engine's curve morphs:
Standard push/pull shifts the film exposure by the stop count and scales the
film curve slope by `1.15^stops`, while the two gammas scale the per-stock
density-curve slopes. Harness coverage: `test_scope_extras`, `test_chem_stages`.

- **RGB→raw method** (`IDC_RGB_TO_RAW_METHOD`). Selects how the linear input RGB is
  turned into the spectral sensitivity the film exposes: **Hanatos 2026** (default
  — the erf4 adaptation window folded into the film sensitivities, the path every
  existing render used), **Hanatos 2025** (a baked per-(wavelength, channel)
  `bandpass_hanatos2025` array applied to the camera-filtered sensitivities, with no
  window and no renormalisation) and **Mallett 2019** (a 3×3 matrix on linear sRGB,
  `M[out][in] = Σ_wl sens[wl][out]·basis[wl][in]`, normalised by the mid-grey green
  response). The combo order matches the enum (`Hanatos 2025`, `Mallett 2019`,
  `Hanatos 2026`) and defaults to Hanatos 2026, so the default render is unchanged.
  Hanatos 2025 needs the per-profile bandpass, which the pack format predates:
  `tools/sync_pack.py` folds it in from the OFX archive while deriving the build
  pack (every build route runs the sync first), and any stock without one falls
  back to Hanatos 2026. Mallett 2019 is opt-in and still pending golden
  validation on Windows. Harness coverage: the "RGB→raw method" block in
  `test/run.sh`.

## Validation status

Pieces that were re-verified on Linux (`./build-mingw.sh`, then
`./hosts/ps8bf/test/run.sh`) as of the most recent update:

- [x] `build-mingw.sh` and `hosts/ps8bf/build.ps1` derive the pack (`sync_pack.py`)
      and generate assets, and link cleanly (`PluginMain` present, no tool errors).
- [x] The mingw build reports five diagnostics under `-Wall` and nothing else: the
      `-Wunknown-pragmas` notices for the pragma in the vendored `grain.h`, one per
      translation unit that includes it. Nothing is suppressed on any route, so
      those five are the real state of the build;
      `build-mingw.sh` prints every diagnostic it is given, not only the ones from a
      failed compile, and counts them in its build report.
- [x] `selftest` passes ("selftest: passed").
- [x] Default render is byte-identical to `tools/refrender` on the 64×64 probe.
- [x] Threaded path (4 workers + `SPEKTRAFILM_THREADS=1`) is byte-identical to
      serial on 260×260.
- [x] `rgb_to_raw_method = 1` (Mallett2019) matches `refrender` and differs from
      the default.
- [x] `rgb_to_raw_method = 0` (Hanatos2025) matches `refrender` and differs from
      the default.
- [x] Every one of the 31 profiles loads and renders film (`kodak_2383`) plus every
      one as a paper (`kodak_portra_400`) with no warnings/errors.
- [x] Full param matrix (108 combinations of input colour space, process mode,
      print timing, LUT steps, RGB→raw method; 54 combinations ×6 film+paper pairs
      with the locked-scope max param set) runs with no warnings/errors.
- [x] Render output is byte-deterministic across repeated runs.
- [x] `python3 tools/gen_mallett_basis.py --check` passes (basis matches Rust
      source); `tools/sync_pack.py --check` reports the derived pack current and
      `tools/merge_bandpass_hanatos2025.py --check` exits 0 (20 profiles carry the
      bandpass, 11 without are intentional).
- [x] `python3 tools/parity/parity.py` exits 0.
- [x] **Bug hunt / hardening pass (2026-10-18).** The pre-ship review fixed every bug found:
      two MSVC link blockers (unmangled C++ `g_sf_hinst` → `extern "C"`; the asset stdio shim
      colliding with the static CRT → renamed to `sf_asset_*` and mapped onto the CRT names for
      the engine TUs only), several ICC-tag bounds reads in `icc_profile.c`, a `sf_parameters`
      null-deref ordering bug in `plugin_main.c`, auto-exposure drift (metering now writes a
      dedicated `auto_exposure_ev` carved from tail padding, so `sizeof(sf_params_block)` stays
      840), two C4244 casts, and build-dependency staleness (the pack's `profiles/*.json` are now
      real build inputs). The harness is green (byte-identical to `tools/refrender`) and clean
      under ASan/UBSan; `build-mingw.sh` + `cmake --preset mingw64` are clean; the shipped import
      imports KERNEL32/USER32/GDI32 only (COMDLG32 was required only by the retired native dialog).

Validated on Windows (user reports, latest with full `build.ps1` logs on
VS 2026):

- [x] **Toolchain.** The default is Visual Studio 2026 (v145 / MSVC 14.5x,
  generator "Visual Studio 18 2026", CMake >= 4.2). VS 2022 (MSVC 19.44,
  Windows SDK 10.0.26100) built the artifacts validated in 2026-10 and stays
  available via `-Generator "Visual Studio 17 2022"` / the `msvc2022` preset.
- [x] **Clean-tree build from the release bundle (2026-11-07, latest run; full
  `build.ps1` report).** The source was `tools/package-for-windows.sh` output —
  475 files, verified to be exactly the tracked file list of the commit it was
  packaged from — extracted into an empty directory, so the tree held nothing
  but a fresh checkout: no build directory and no `spektra-core\data\pack`.
  `build.ps1` with no arguments then reported:
  - first configure derived the pack from the two bundled upstream trees —
    33 files, 20 bandpass profiles, `src pack e6ba3f78c114`,
    `bands f812b3f4e225`, **the same two hashes the Linux/MinGW route
    produces**. That is the cross-toolchain parity claim: the *pack* content is
    identical, so the derivation has no line-ending, path or float-formatting
    drift. The *artifact* hash cannot match across compilers and is not a parity
    signal — the binary embeds its own build date and time (the date the About
    popup shows), so `sha256` changes on every build by design.
  - `warnings 5 (warning C4068 x5)`, errors 0 — the documented baseline, all
    five from `#pragma STDC` in `grain.h`.
  - imports `USER32 GDI32 KERNEL32` and nothing else: no `D3D11`, `DXGI`,
    `D3DCOMPILER_47` or `COMDLG32`, and no CRT DLL because `/MT` staticises it.
    The `build.ps1` dumpbin guard checks these invariants automatically.
  - `dist\spektrafilm.8bf` (15.3 MiB,
    `sha256 72f12fb654c1de481359b8326790834b49ac533cfaf10d4fa61be038a8933394`)
    staged with `LICENSE` and `THIRD-PARTY-NOTICES.md`.
  - toolchain: VS 18.11.12328.437, MSVC 19.51.36260.0, CMake 4.3.1-msvc1,
    Windows SDK 10.0.26100, Python 3.13.14.
- [x] **Version resource read back from that artifact** — in Explorer's
  Details tab, or
  `(Get-Item .\dist\spektrafilm.8bf).VersionInfo | Format-List *`:
  `ProductName` `spektrafilm-8bf`, `FileDescription`
  `spektrafilm film simulation filter`, `CompanyName` `spektrafilm`,
  `FileVersion` and `ProductVersion` `0.1.0`, `OriginalFilename`
  `spektrafilm.8bf`, `LegalCopyright`
  `Engine GPL-3.0-or-later; data pack CC BY-SA 4.0`.
- [x] **In-host in Affinity on Windows 11 with that build:** the About popup
  keeps its fixed size, and Esc over it closes only the modal instead of
  cancelling the filter.
- [x] Discoverability – plug-in appears once, under Affinity Pixel → Filters →
  Plugins → Film Simulation → spektrafilm, and the dialog opens. No
  `%TEMP%\spektrafilm.log` is required for validation; the banner is
  informational only.
- [x] Capture behaviour verified in-host: no accidental double-processing,
  undo/redo/cancel works, selection tile handling works for the tested cases.
- [x] `RGB to RAW` combo has the three expected entries and all alter the image
  slightly; the default (Hanatos 2026) behaves as expected.
- [x] Input-space sensitivity for sRGB vs ProPhoto is acceptable; documented cat
  difference is within the expected small range.
- [x] Camera/profile boundary differences were exercised across stocks with
  and without archived bandpass.
- [x] GUI values/buttons have been sampled for sanity (granular GUI polish
  deferred to a later pass).
- [x] Performance (large frame/preview) is acceptable for the tested scenarios.
- [x] Packaging is not required for this validation.
- [x] Golden-capture, effect-stack, and extra optional checks are not required for
  release validation of the plug-in itself.
- [x] **Newest build (2026-10-18) works end-to-end in-host.** The bug-hunt build loads
  and renders correctly in Affinity on Windows 11; the ImGui parameter dialog (window,
  live preview, control round-trip, theme toggle, DWM chrome, DPI change) works
  end-to-end in-host.
- [x] **v0.1.0 GUI/preview polish (2026-11-04) verified in-host.** About modal (ISO-8601 date),
  true 1:1 crop + 1:1 pan tracking, supersampled whole-image preview, checkbox ticks, and the
  single spektrafilm menu entry (PiPL category "Film Simulation") all confirmed in Affinity on
  Windows 11.
- [x] **Keyboard and number fields (2026-11-08) verified in-host.** Typing into
  a number box no longer updates the preview: it stays still until the field is
  finished with Enter or focus moves elsewhere, then updates once, and Enter in
  a field does not apply the settings. Esc still dismisses the window, which is
  the intended behaviour. Not separately probed: Enter or Space on a
  deliberately keyboard-highlighted OK or Cancel — both buttons are given
  `ImGuiItemFlags_NoNav`, and Tab cannot leave the settings child window in the
  first place, so no key can reach either.
- [x] **Factory look (2026-11-08) verified in-host.** A first open of the
  settings pane shows Kodak Ektar 100 as the film and Kodak Ektacolor Edge as
  the paper.
- [x] **Slider and wheel preview timing (2026-11-08) verified in-host.**
  Against the `92240c9` build in Affinity on Windows 11: a slider held under the
  button no longer renders the preview however often the pointer pauses, and it
  updates once the button is let go; rolling the mouse wheel over a slider
  renders about 0.7 s after the rolling stops, and that settle window is the
  right length as it stands. The 1:1 crop drag renders when the drag stops
  rather than under the cursor - that is the debounce it already had, the
  gesture was not touched by this change - and it is accepted as it is: on a
  large image the live drag was never quicker than this. The dialog was found
  smoother to operate overall.

Still open (cannot be closed on Linux):

- [ ] Mallett2019/Hanatos2025 OFX golden-image parity (optional, not needed
      for this release). Requires a Windows GPU capture
      (`SpektraVulkanCopyHarness`) and a `tools/parity/` image-diff reporter.
      The CPU engine-side comparison against `tools/refrender` is already in
      the Linux suite.
- [ ] Printer-timing APD spectral calibration — still blocked on the licensed
      `st2065-2a-2020.csv` / `st2065-2b-2020.csv`.

None of the open items change the default render; the engine keeps the
filtered-enlarger (`printTiming = 0`) and Hanatos2026 RGB→raw defaults.

- **Still GPU-only (not ported):** the OFX *Effects* family — lens distortion,
  vignette, bokeh, coma, chromatic aberration, film gate, gate weave, flicker &
  breathing, timing shift and the like. These are Vulkan/compute shaders with no
  CPU engine implementation and no shader source in the tree (only compiled
  `.spv`); see `docs/EFFECTS_FAMILY_FINDINGS.md`.
- **Engine-backed surface is fully wired.** The last gap — the RGB→raw method
  selection — is closed (see *Locked-scope extras*); there is no remaining
  engine-backed parameter without a dialog control. The one UI caveat is
  filtration: the dialog exposes the M/Y trim plus the cyan `filterC` trim, and the
  engine has no separate C control.
- **Film-plane / ROI / tiling.** The selector still requests the whole selection
  as one tile (`ROADMAP.md` §6), so the spatial stages render over the full frame
  (no tiling seams). Preview crop/zoom/pan is a view preference only.
- **Keep defaults?** If you want the film-look stages on by default (matching the
  darktable iop, which ships halation and grain on), flip the corresponding
  defaults in `spektra-core/sim_port/sp_params.c` — the render path already
  handles it.
