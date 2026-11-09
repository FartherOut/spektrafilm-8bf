# Architecture

Status: **current-state, 2026-11-09.** Describes the tree as it ships at
v0.1.0. Historical design records live in the dated roadmaps in this folder.

spektrafilm is a Photoshop-compatible `.8bf` stills filter for Windows x64
(primary host: Affinity Photo) that renders spectral film simulations. Three
layers, each independently testable:

```
hosts/ps8bf/          the .8bf adapter (Win32, ImGui+D3D11 dialog)
spektra-core/sim_port/  sp_chain: parameter struct + stage orchestration
spektra-core/third_party/darktable/  the vendored engine (spektra_core.c,
                        spektra_sim.c) behind shims/ (glib/json stubs, asset stdio)
```

`spektra-core/third_party/darktable-spektrafilm/` (submodule) is the data pack,
not code. `upstream/spektrafilm-ofx` (submodule) supplies the archived OFX
bandpass merged into that pack. Both are required by the build; see
`docs/BUILDING.md`.

## The .8bf contract

The host fills a `FilterRecord` struct and calls the exported `PluginMain`
with a selector (`hosts/ps8bf/src/plugin_main.c`): About (0), Parameters (1),
Prepare (2), Start (3), Continue (4), Finish (5). The layout is declared
locally in `pi/pi_types.h` + `pi/pifilter.h` (no Adobe SDK needed) and pinned
by 121 `SF_OFF` offset asserts. The PiPL resource (`res/pipl.bin`, generated)
tells the host what the plug-in is without executing it. Every selector call
appends to `%TEMP%\spektrafilm.log`, banner first, with the module path the
host actually loaded - the first thing to read when a fix "does not take".

`filterSelectorParameters` only ensures a valid settings block exists in the
host's parameters Handle. The dialog does not run there: a preview of the
user's image needs pixels, which the host only hands over during the
Start/Continue loop.

## Render path (final pixels)

`filter_loop.c` owns Start/Continue/Finish. The whole selection is requested
as **one tile** - the DIR-coupler blur is a full-frame spatial pass, and
tiling it without radius padding would seam. The sequence in Continue:

1. pull the tile (`planeProc`/`pixelData`, 32-bit coordinates when offered),
2. decode to interleaved float RGB (`pix_convert.c`),
3. measure auto exposure on the still-encoded frame when enabled, storing the
   EV into `auto_exposure_ev` (recomputed every invocation; the chain is then
   rebuilt with it),
4. run the dialog hook (below) exactly once, before rendering,
5. render through `sp_chain_render` with the host's `progressProc`/`abortProc`
   bridged between stages (an abort maps to `userCanceledErr`),
6. encode back and scatter into the host's output planes; alpha and extra
   planes ride through as a memcpy.

The dialog hook (`sf_filter_set_dialog_hook`) is the seam that makes preview
and render share one code path: `plugin_main.c` installs `sf_dialog_bridge`,
which opens the ImGui window over the decoded frame and, on OK, writes the
edited block back into the host's Handle (growing it if the host stored a
smaller older one). The Linux mock harness leaves the hook NULL and drives the
exact same bodies - that is why `filter_loop.c` stays free of Win32.

## Preview path

`sf_preview.c` renders the preview through the **same `sp_chain`** as the
final render, which is what makes "preview matches final" structural rather
than aspirational. Three entries: `sf_preview_render_image()` (true 1:1 crop
of the user's frame at the pane's pixel size, pannable),
`sf_preview_render_fit()` (whole frame at reduced resolution, with the micron
pixel pitch scaled by the same factor so grain and blur stay physically
scaled), and `sf_preview_render()` (built-in reference scene, the fallback
when no pixels exist yet). The chain writes space-preserving output;
`sf_preview_display()` converts document space to sRGB (XYZ, Bradford CAT to
D65) for the unmanaged preview - identity for the sRGB default.

The dialog (`sf_gui_imgui.cpp`) pumps its own modal loop (`WaitMessage`-based,
inside the hook that runs inside Continue). Preview re-renders are timed per
input family: a slider held under the mouse button defers the render until the
button is released, a wheel notch arms a 700 ms settle window so a rolling
wheel renders once the rolling stops, and every other change takes the 120 ms
debounce; a pane resize re-renders immediately. The GPU stack
(d3d11/dxgi/d3dcompiler_47) is `LoadLibrary`'d at runtime (decision D8) so the
shipping import table stays KERNEL32/USER32/
GDI32; `build.ps1` fails the build if a static GPU import appears.

## Parameter surface

One flat block (`params.h`, magic `'SFP1'`, version 21) is the single source
of truth shared by the GUI, the host-persisted Handle, presets and the
clipboard. `sf_params_validate()` rejects anything out of range with a
distinct code (see `docs/PARAMETERS.md`); `sf_params_to_chain()` maps it to
`sp_chain_params_t`, the engine's own struct, whose keys are shared with the
OFX side via `sp_params.c`. Presets (`preset.c`) are hand-editable JSON
written field-by-field through an offset table, in the shortest decimal form
that reads back bit-exact, always dot-separated regardless of locale.

## Data pack pipeline

The pack the plug-in embeds is **derived, never hand-edited**:

```
submodule pack (darktable-spektrafilm) ─┐
                                        ├─ tools/sync_pack.py ─> spektra-core/data/pack
OFX archive bandpass_hanatos2025 ───────┘        (git-ignored; sole writer)
                                                       │
                              tools/gen_assets.py --win ─> asset_ids.h + pack.rc + pack_list.h
                                                       │
                              rc.exe (via CMake) ─> RCDATA resources in the .8bf
                                                       │
                    runtime: assets_win.c resolves pack paths -> SF_ASSET_RES
                    (base id 3000); stdio_asset.c maps the engine's file reads
                    onto those resources. No files on disk next to the DLL.
```

`sync_pack.py` is stamp-based (content hashes of the source pack, the archived
bandpasses and the scripts themselves in `data/pack.stamp`), fails rather than
falling back to the pristine pack, and supports `--check`. `gen_assets.py`
re-encodes the spectral LUT from float16 to float32 so the engine loads the
embedded copy directly. `tools/gen_pipl.py` emits `res/pipl.bin`, verified
byte-for-byte against Adobe's `Cnvtpipl.exe` under Wine.

## Threading

`sf_parallel.c` splits row ranges across worker threads between stages. The
guarantee is byte-identity with the serial path (the harness compares them on
a 260x260 frame with `SPEKTRAFILM_THREADS=4`); below ~64k pixels the runner
stays serial. `SPEKTRAFILM_THREADS=1` forces serial for debugging.

## Load-bearing invariants

- **Preview == render**: both go through `sp_chain` with the same parameters;
  a preview-only approximation is a bug, not a shortcut.
- **Threaded == serial, byte-identical** (`test/run.sh` gate).
- **Mock host == `tools/refrender`**, byte-identical (the locked parity
  target; `refrender` is the reference renderer the whole port is checked
  against).
- **Whole-frame single tile**: any future tiling must pad by the blur radius
  first, or the DIR blur seams.
- **Import allow-list**: KERNEL32/USER32/GDI32 only, CRT static (`/MT`), GPU
  stack dynamic. `build.ps1` enforces all three with `dumpbin`.
- **No warning suppressions anywhere**; the report baseline is exactly five
  `C4068`/`-Wunknown-pragmas` notices from the vendored `grain.h` pragmas.
- **Pack determinism**: `sync_pack.py` is the only writer of `data/pack`;
  `data/pack.stamp` records the content hashes of the sources and of the
  derivation scripts, so "is the pack current?" is a check, not a belief.

## Where things live

| Concern | File |
|---|---|
| Selector dispatch, log, host-side About | `hosts/ps8bf/src/plugin_main.c` |
| Tile pull/push, auto exposure, dialog hook | `hosts/ps8bf/src/filter_loop.c` |
| Parameter block + validator | `hosts/ps8bf/src/params.c` |
| Preset codec (pure, harness-tested) | `hosts/ps8bf/src/preset.c` |
| Dialog, preview wiring, presets UI, About modal | `hosts/ps8bf/src/sf_gui_imgui.cpp` |
| Preview engine paths + display transform | `hosts/ps8bf/src/sf_preview.c` |
| D3D11 device, dynamic loading | `hosts/ps8bf/src/sf_d3d11.cpp` |
| Document ICC detection | `hosts/ps8bf/src/sf_gui.c`, `icc_profile.c` |
| Stage orchestration, chain clamps | `spektra-core/sim_port/sp_chain.c` |
| Engine (vendored, unmodified) | `spektra-core/third_party/darktable/` |
| Linux mock host + property tests | `hosts/ps8bf/test/harness.c` |
