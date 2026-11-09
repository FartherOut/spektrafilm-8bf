# Spektrafilm → Affinity Photo (`.8bf`): Roadmap

**Status:** ✅ **v0.1.0 — feature complete.** Threaded `.8bf` shipping and signed off in Affinity Photo on Windows 11; the full film-simulation surface (§2.1) is implemented, the ImGui dialog + live preview are polished, GPL-3.0-or-later, VS 2026 build. Research report below.
**Date:** 2026-10-10 (updated 2026-11-06 — post-review hardening round)

> This document is the research report and plan of record; it is kept as the
> reasoning trail. Current-state documentation is in `docs/` (index:
> `docs/README.md`).

> **Update 2026-11-09 — DIR coupler edge overshoot: investigated, kept, to be
> raised upstream.** A user report: with halation, grain and every other spatial
> stage off, the factory Ektacolor look shows a whitish rim on the light side of
> dark→light boundaries. Measured with `tools/refrender` on a synthetic step
> edge and on a purpose-built test chart rendered through all 22 stocks in the
> pack, in the GUI and in the engine: the overshoot comes **entirely** from the
> DIR coupler diffusion stage — with `couplers:false` it is exactly zero, the
> rest of the chain is strictly per-pixel, and the print section can only
> rescale the rim, never remove it. GUI and engine agree within 1–2 levels on
> every stock, so this is not a porting bug: it is the model's adjacency effect
> (Crawford–Hunter), inherited from the reference (upstream default
> `dirCouplersDiffusionUm = 20.0`). Colour negatives all carry it (±20 %
> between stocks); reversal films and Tri-X-reversal show none, Double-X is
> present but ~40 % weaker. What is questionable is the **magnitude** — ~+55 %
> local overshoot at a full-contrast step, where a real adjacency effect is a
> few percent. **To explore:** raise it with the OFX authors / andrea and
> compare notes on the intended strength; no port-side change, parity holds.

> **Update 2026-11-08 — keyboard: no key applies the dialog.** The line below
> ("keyboard navigation (Enter accepts / Esc cancels)") and the same wording in
> `docs/GUI_ROADMAP.md` are superseded: Enter no longer accepts, and OK and
> Cancel are excluded from keyboard focus so no key can start the render or
> dismiss the window. Esc still cancels. Number boxes commit when the field is
> finished with, not per keystroke. Current rule: `docs/CONVENTIONS.md` §*GUI*.

> **Update 2026-11-06 — post-review round: input guardrails, display locale,
> fresh-clone verification.** A Windows review round after the v0.1.0 sign-off.
> **Display:** every GUI number and tooltip uses European comma decimals.
> **Guardrails:** the four blur-scale multipliers gained a validator range
> (reject code 43) that preset-file and clipboard loads refuse, so an
> out-of-range value reaches the engine by no route (the GUI text box clamps
> before it writes); the halation ceiling no longer rewrites the per-channel
> first-bounce radii — preview, `OK` and the chain's clamp fold the block
> non-destructively instead; the recursive-Gaussian pass count saturates at
> `SF_GAUSS_MAX_IIR_PASSES` as a totality bound; and the settings pane warns
> *before* an expensive preview ("Wide halation/diffusion makes the preview
> slow to update"), taking diffusion's widest Gaussian from the engine
> (`sf_diffusion_widest_sigma_um()`) rather than a copy of the family tables.
> **Build log:** the pack tools print plain ASCII, so the Windows console
> shows no codepage mojibake. **Verified on Windows 11 + Affinity from a
> fresh-clone tree with VS 2026:** first configure derives the pack with the
> same content hash as the Linux machine, `warnings 5 (warning C4068 x5)`,
> errors 0, imports KERNEL32/USER32/GDI32. Details: `CHANGELOG.md`.

> **Update 2026-11-04 — v0.1.0: modernization + GUI/preview polish, feature complete.** A final
> pass took the plug-in from "works" to "shipped". **Build:** the MSVC shipping build now defaults
> to **Visual Studio 2026 Community** (generator *Visual Studio 18 2026*, toolset v145, standalone
> CMake ≥ 4.2), with VS 2022 as fallback. **Licensing/hygiene:** adopted **GPL-3.0-or-later** with
> the full license text; dropped every claim that the Adobe Photoshop SDK is required (the `.8bf`
> ABI is declared by hand in `pi/`); stopped tracking build junk; print **auto-exposure
> now defaults OFF**. **Vendoring:** the trees the build reads are real git submodules
> (`upstream/spektrafilm-ofx` @86476af, `spektra-core/third_party/darktable-spektrafilm`
> @b15cfe1); reference-only upstreams (`spektrafilm`, `vkdt`, `spektrafilm-rs`) are cloned
> into `upstream/` by `tools/fetch-sources.sh`. **Docs:** added `INSTALL.md` (Windows build +
> install, naming the Affinity v3 plug-in folder `%USERPROFILE%\.affinity\Affinity\3.0\Plugins`).
> **GUI/preview (all Windows-verified in Affinity):** the header shows name + version only;
> the **About** box is a real modal (Esc over it no longer cancels the plug-in window) reporting
> the build date as **ISO 8601**; the
> **preview matches the final render** — a true native-scale 1:1 crop plus a ≤2× supersampled
> whole-image view; fixed the 1:1 crop rendering almost entirely black (a blit destination pointer
> that advanced per row instead of per pixel) and made the **1:1 pan drag track the cursor exactly
> 1:1**; checkbox selected-state keeps its blue tick (no solid fill, hover stable); the **host menu**
> shows Spektrafilm once (PiPL `catg` = "Film Simulation", item `name` = "Spektrafilm" — Affinity
> always nests each `.8bf` under a category submenu). **Result: v0.1.0, feature complete for the
> locked §2.1 scope.** Only optional OFX-GPU golden-image parity, the licensed APD spectral table,
> and the deliberately out-of-scope GPU-only features remain (§8 Phase 4; `hosts/ps8bf/README.md`
> §*Still open*). Release notes: `CHANGELOG.md`, `docs/GUI_ROADMAP.md` §13.

> **Update 2026-10-18 — bug hunt / hardening pass complete (Windows-verified).** The pre-ship
> review fixed every bug found. Two MSVC link blockers: an unmangled C++ reference to `g_sf_hinst`
> (`extern "C"` in `sf_gui_imgui.cpp`) and the asset stdio shim colliding with the static CRT
> (renamed `fopen/fread/...` → `sf_asset_*`, mapped back onto the CRT names for engine TUs only —
> which also fixed the host transcript-log hijack). Correctness/safety: several out-of-bounds
> ICC-tag reads in `icc_profile.c`; a `sf_parameters` null-deref ordering bug in `plugin_main.c`;
> auto-exposure drift (metering now writes a dedicated `auto_exposure_ev` carved from tail padding,
> so `sizeof(sf_params_block)` stays 840); two C4244 casts. Build-dependency staleness: the pack's
> `profiles/*.json` are now real build inputs (CMake `GLOB CONFIGURE_DEPENDS` + Makefile
> wildcard). Verified: `test/run.sh` green (byte-identical to `tools/refrender`) and clean under
> ASan/UBSan; `build-mingw.sh` + `cmake --preset mingw64` clean; the shipped import list is
> KERNEL32/USER32/GDI32 (COMDLG32 was required only by the native dialog, retired in B6).
> Confirmed working in-host in Affinity on Windows 11.
> Details in `docs/GUI_ROADMAP.md` §12.

> **Update 2026-10-16 — preview is colour-correct; grain scaling reviewed and kept as-is.** The
> live preview now runs a **display transform** (`sf_preview_display`) so the preview bitmap is
> colour-correct: document-space chain output → sRGB (linearise with the document curve → to XYZ →
> Bradford D50/D65 adaptation → sRGB matrix + curve). It is a byte no-op for an sRGB/sRGB document,
> so the default render stays identical, and it lives entirely in the preview path (`sf_preview.c`),
> not the engine. Separately, the **grain format scaling** was analysed end-to-end (`pixel_um`,
> `sp_grain_format_scale`, the fixed-pixel clump blur, and the dormant `grainMicroStructure` field)
> and **deliberately left unchanged** — the current scaling is accepted as good as-is; no grain code
> was altered. Details in `docs/GRAIN_SYSTEM_ROADMAP.md` §*Format scaling review*.

> **Update 2026-10-16 — parameter-dialog GUI roadmap added.** The Win32 parameter dialog flickers
> black while scrolling (no `WS_CLIPCHILDREN`, a full-client `WM_ERASEBKGND`, and a blanket
> `RedrawWindow(RDW_ERASE|RDW_ALLCHILDREN)` per scroll step). Plan: a small Win32 flicker fix first,
> then migration of the whole dialog (preview + settings) to **Dear ImGui** using its stock/official
> Windows renderer (`imgui_impl_win32` + `imgui_impl_dx11`, Direct3D 11 with a WARP fallback), with the
> existing dialog kept as a selectable fallback. Engine output stays byte-identical. **The interim Win32
> flicker fix is implemented** (WS_CLIPCHILDREN, clip-aware invalidation, batched no-redraw pane moves,
> no-op-move guard); it builds clean and the harness is green, pending Affinity verification. Details in
> `docs/GUI_ROADMAP.md`.
>
> **Update 2026-10-17 — ImGui foundation (B0–B3) implemented.** ImGui v1.93.0 is vendored under
> `hosts/ps8bf/third_party/imgui`. Both build paths (`build-mingw.sh`, `cmake --preset mingw64`) compile
> the C++ TUs and keep the shipping import table at kernel32/user32/gdi32 (COMDLG32 was dropped in B6,
> when the native dialog went): the GPU stack
> (`d3d11`/`dxgi`/`d3dcompiler_47`) is `LoadLibrary`'d, and `dwmapi`/`imm32`/`shell32` are avoided (the
> B4 DWM chrome is also `LoadLibrary`'d). The ImGui dialog owns its window + modal loop and shows a live
> preview as an RGBA8 D3D11 texture (fit / 1:1 crop + pan) through the existing `sf_preview` transform.
> **B3 ports the full `dialog.c` control set** into collapsible ImGui groups (label | slider | editable
> buddy box; middle/right-click resets to the engine default; tooltips on every control), with the
> document-profile detection factored into a shared `sf_gui.c` helper used by both backends.
>
> **Update 2026-10-17 — ImGui polish + default flip (B4–B5) implemented.** B4 adds a flat, modern
> dark/light theme (dark default; `SPEKTRAFILM_GUI_THEME=dark|light` or the header *Theme* button), an
> accent OK button, keyboard navigation (Enter accepts / Esc cancels), a theme-matched clear colour,
> per-monitor DPI handling (`WM_DPICHANGED` rebuilds the font/style between frames) and the Windows 11
> DWM chrome (rounded corners + immersive title bar). B5 flipped the default: the ImGui backend is now
> the only backend (B6, 2026-10-19, retired the native dialog and its `SPEKTRAFILM_GUI=native`/
> `-DSF_GUI_BACKEND=native` switch; a failed D3D11/WARP init reports an error and cancels).
> `test/run.sh` stays byte-identical. **Verified in Affinity on Windows 11 (2026-10-18)**; the bug hunt / hardening pass is
> also complete. Details in `docs/GUI_ROADMAP.md`.

> **Update 2026-10-15 — roadmap docs reconciled.** The status tables in
> `docs/OFX_FEATURES.md` and `docs/PORT_PARITY_ROADMAP.md` were brought in line with the
> code: process modes + scan invert, printer lights, camera UV/IR, film format,
> auto exposure, bleach bypass (neg + print), leuco-cyan, shadow/highlight shape,
> print push/pull, diffusion detail, scanner MTF50/USM radius and the full grain
> surface are all implemented (params block **v19** + dialog). The only
> engine-backed item still open is the **RGB→raw method selector**, now in
> progress. Paths were updated for the reorganised `upstream/` tree
> (`spektrafilm/` = Python reference, `spektrafilm-ofx/` = OFX project).
**Update 2026-10-14 late — UI navigation foundation promoted to two-row tabs.**
The host now switches the same fixed control map through two rows of 11 owner-drawn section
tabs (`IDC_TAB_*`, no comctl32 dependency), stored in
`hosts/ps8bf/src/dialog.c`; each section carries matching field labels plus red, disabled stub
controls (`IDC_STUB_LABEL_*`) for unsupported fields and `WM_CTLCOLORSTATIC` coloring. `sf_section_show()` remains the single page-switch path; the unsupported controls remain visually present but non-backed.

<|end|> A late-2026 pass started closing the last granular scope items
| that were previously stubs. The first implementation (consumer-facing controls
| plus their params-block wiring) lands **scanner glare roughness/blur** as real
| per-image fields (the engine used to pin them at 0.7 / 0.5 px; they are now
| real `sp_chain_params_t` / params-block v13 members, exercised end-to-end).
| The remaining granular controls (halation full set, grain micro-structure, and
| diffusion surface changes) are queued for the next pass, as recommended below.

> **Update 2026-10-14 — code surface substantially closed.** Experimental film
> Push/Pull (model 1: log-raw warp + density gain) is now implemented behind
> `filmPushPullMode`; true ICC document/working-profile auto-detect is now
> implemented (`sf_icc_parse_profile`, channel-curv/para handling) and plumbs
> a derived matrix/transfer into the chain; auto exposure + meter now works
> (centre-weighted / median, preview-shaped, applied as exposure EV). RGB-to-raw
> remains Hanatos 2026 as agreed. The remaining work is parity/goldens, the
> data-pack decision, Phase 2 per-channel reconciliation and Phase 4 packaging.
**Author:** research pass over `upstream/` + the wider upstream ecosystem

> **Update 2026-10-12 — PhotoCraft/WASM path dropped.** The project targets
> **Affinity/Photoshop (`.8bf`) only**. The PhotoCraft WebAssembly test host and
> its tooling were removed: `hosts/wasm/`, `tools/pc-host/`,
> `tools/test_wasm_parity.py`, the wasm-only shims (`numconv_mini.*`,
> `vsnprintf_mini.c`), and the wasm asset flavour of `tools/gen_assets.py`. The
> shared `pc_json.h` moved to `spektra-core/shims/portability/`. Sections below
> that describe the WASM adapter are kept only as **historical rationale**; the
> paragraphs marked *(dropped)* are no longer part of the plan.

---

## 1. TL;DR

You want the **spektrafilm** film-simulation engine usable as a **Photoshop-format `.8bf` filter plugin** in **Affinity Photo** (Windows), kept **trivially updatable** from the pinned `upstream/spektrafilm-ofx` submodule (see `SOURCES.md`).

Two facts drive everything:

1. **Affinity Photo only loads Photoshop-compatible 64-bit filter plugins (`.8bf`)**, configured via *Preferences → Photoshop Plug-ins* with *"allow unknown plugins"*. It runs the plugin's own dialog. This is the real target.
2. **The OFX project is GPU-only** (Metal on macOS, Vulkan on Windows/Linux) and ships **no CPU renderer**. You chose CPU-only, which is the right call for a single-image editor — but it means the CPU engine must be **sourced/ported from somewhere**, because the OFX renderer cannot simply be relinked for CPU.

The good news: **CPU implementations of this exact algorithm already exist** and can seed the work instead of writing a spectral pipeline from scratch (see §4).

**Recommended path:** build **one portable C/C++ CPU core** (seeded from the darktable C engine), expose it through the **existing `SpektraAppBridge.h` C ABI**, and write **one thin host adapter** — a Windows `.8bf` adapter for Affinity. Drive the whole thing with a **git submodule + one-command build script**, using the OFX repo as the **data + parameter + spec** source and a **parity harness** to catch algorithm drift.

The one thing that is *not* achievable: a CPU port that automatically inherits every algorithmic tweak of the OFX GPU shaders with zero maintenance. §5 explains the tradeoff and how to manage it.

---

## 2. Goal & fixed constraints (from your answers)

| Decision | Value |
|---|---|
| Shipping target | **Affinity Photo desktop**, Photoshop `.8bf` filter plugin |
| Test target | **Affinity Photo** (`pspiHost` / IrfanView / XnView as a Windows surrogate) |
| Build OS | **Windows only** (MSVC) |
| Render backend | **Portable CPU core** (GPU not required) |
| Update model | **Git submodule + thin adapter / one-command rebuild** |
| Scope | **OFX parity for the film-simulation surface** (HDR/delivery, printer lights, LUT export, film plane, non-Production grain are **out** — see §2.1) |
| License | **GPL-3.0-or-later** / upstream spektrafilm is GPLv3; personal use only |

Non-goals: macOS/Linux builds, GPU acceleration, Adobe Photoshop as a shipping host, Canva web (it cannot host native plugins — "Canva Affinity" resolves to Affinity Photo by Canva).

### 2.1 Scope decision (locked 2026-10-10)

Decided after reviewing the full OFX surface (`docs/OFX_FEATURES.md`) and a Q&A on
per-feature needs for stills. The plugin is used as **one step in a raw/DNG
development flow**: develop raw in a wide-gamut Affinity working space (ProPhoto or
Display P3, 16-bit) → apply the filter → convert to sRGB/Adobe RGB at export *in
Affinity*.

**In scope (implemented):**
- Process modes: Print simulation / Scan negative / Process negative, Invert Negative Scan.
- Input colour space: **auto-detect from the Affinity document profile**, with the full
  26-space OFX menu as a manual override (for hosts that expose no ICC).
- Output: **none in the plugin** — process in the document working space; Affinity handles
  sRGB / Adobe RGB / Display P3 delivery (Display P3 is wide-gamut *SDR*, not HDR).
- Film: stock, film format, push/pull (Standard; Experimental disabled), exposure EV,
  film gamma, **auto exposure + meter**, RGB→raw (Hanatos 2026 / 2025 / Mallett 2019 —
  all three are available from the OFX data pack; 2026 is the OFX default),
  **negative bleach bypass**, **leuco-cyan coupling**.
- Print: paper, print timing (Filtered Enlarger), print push/pull, **print bleach
  bypass**, exposure EV, print gamma, **shadow/highlight shape**, **C/M/Y filtration**,
  preflash + M/Y shifts.
- DIR couplers: full group incl. tails, same/inter-layer inhibition, **per-channel gamma
  couplings**, calibrate-to-stock.
- Grain: **Preview + Production models** (Synthesis stays out) — the full grain
  parameter group (model selector, sublayers + sub-layer count, particle area, per-channel
  and per-layer particle scale, per-channel density min, per-channel uniformity, final
  blur, dye clouds, micro structure, saturation), plus an explicit **Seed** control
  (default: the stock's own deterministic pattern; a locked seed reseeds it).
- Halation: full group incl. **per-channel Strength RGB**.
- Diffusion: camera + print stages, full group (core/halo/bloom intensity + size).
- Scanner: engine arrives free via the darktable seed — controls present but hideable.

**Present in the parameter surface but disabled for v1:**
- HDR output (PQ/HLG, nits, tone-mapping), RCM/ACES scene output, output-colour-space menus,
- Film Plane (enlarger scale/offset), LUT export, GPU render tiling,
- Grain Preview / Synthesis models and Animate.
- Manage-group bookkeeping (preset save/load/copy-paste, defaults persistence): **done in Phase 4**
  — see `hosts/ps8bf/src/preset.c` and the dialog's Presets group (§8, Phase 4).

**Omitted entirely:** printer lights (only meaningful for movie printing runs); gate-weave /
film-breathe never existed in OFX (they're a Dehancer-style feature).

**Resulting parity target:** *OFX minus the HDR/delivery surface, printer lights, film
plane, LUT export, and Synthesis grain* — i.e. the complete film-simulation surface,
not the video/delivery surface.

---

## 3. What the pieces actually are (research findings)

### 3.1 The OFX project (`upstream/spektrafilm-ofx`, upstream `chaert-s/spektrafilm-ofx`)

- A native C++17 **port of `andreavolpato/spektrafilm`** — GPU only: `SpektraMetalRenderer.mm` (~319 KB) on macOS, `SpektraVulkanRenderer.cpp` (~338 KB) on Windows/Linux.
- **Already exposes a clean C ABI** in `src/SpektraAppBridge.h`:
  - descriptor-driven parameters (`SpektraAppGroupDescriptor`, `SpektraAppParamDescriptor`, kinds, min/max, defaults, visibility tiers Flow/Pro/Dev),
  - film/paper enumeration, option sets,
  - `SpektraAppRenderParams` (the full ~200-field parameter struct),
  - `SpektraRendererCreate/Destroy/Render`.
  This is the **ideal host seam** and should be reused verbatim as our core's ABI.
- **Parameter surface is large** (`src/SpektraParameters.h`): process modes, RGB→raw methods (Hanatos 2026/2025, Mallett 2019), ~26 color spaces, HDR output (PQ/HLG, nits), print timing (filtered enlarger vs APD printer density), camera UV/IR filters, push/pull, bleach bypass, leuco cyan, enlarger filters C/M/Y, enlarger scale/offset, preflash, printer lights, **DIR couplers** (14 params), **grain** (3 models: Preview/Production/Synthesis, ~40 params), **halation** (scatter + halation + highlight boost), **camera & print diffusion** (4 filter families), **scanner** (MTF, unsharp, glare) and **scanner color correction**, **color adaptation** flags.
- **Build-time data pipeline**: `tools/generate_profile_curves.py` (+ `tools/ofx_stock_lists.py`) reads `Resources/data` and emits generated C++ tables and `.f32` LUTs (`SpektraGeneratedProfileCurves.cpp`, `SpektraHanatos2025Spectra.f32`, `SpektraOutputGamutCompression.f32`). Python deps: `numpy`, `scipy`, `colour-science`. CMake even ships **parity/perf/evaluation harnesses** (macOS Metal + a `SpektraVulkanCopyHarness` on Windows) — reuse these as the parity oracle.
- **Data**: `Resources/data` ≈ 22 MB — profile JSON (~204 KB each; 5.7 MB current + 5.7 MB archive), filter database (168 KB), LUTs (9.8 MB incl. Hanatos `irradiance_xy_tc.npy` 5.7 MB + `.lut` 4.1 MB).
- Note: upstream expects to live at `OFX/SpektraFilm` inside the spektrafilm repo and use `../../.venv`. The local copy lives at `upstream/spektrafilm-ofx` (next to the Python reference at `upstream/spektrafilm`).

### 3.2 Affinity Photo's plugin support (the shipping host)

- **Photoshop-compatible 64-bit plugins only**, filter type. You point Affinity at a folder in *Preferences → Photoshop Plug-ins*, enable the unknown-plugins option, restart, and it appears under the **Filters** menu.
- Affinity **runs the plugin's own modal dialog** — there is no host-provided parameter UI. So the plugin must ship its own UI.
- Affinity implements Adobe's published `.8bf` ABI loosely. Practical implication: build a **single, self-contained 64-bit DLL** with **no external dependencies**, no reliance on Adobe's old ADM dialog stack, and no reliance on optional suites.

### 3.3 PhotoCraft's plugin system *(dropped — historical rationale only)*

- Pure-Rust clean-room editor; plugins are **WebAssembly**, executed by `wasmi`, surfaced under **Filter › Plug-ins**.
- **WASM plugin ABI v1**: exports `pc_abi_version`, `pc_manifest`, `pc_alloc`, `pc_filter`; **no imports** (no host libc, no WASI); **f32 interleaved samples**; params passed as **JSON**; called **per band**.
- Limits to design around: memory **512 MiB**, fuel **50 M per call + 4,000/sample** for `pc_filter`, wall clock **60 s**.
- Consequence: PhotoCraft can validate the **core logic** (correctness, params, film/paper selection) but is a poor place for full-size renders. Plan to test on **small crops / thumbnails**. It cannot validate the `.8bf` packaging at all.

### 3.4 Existing CPU implementations (the accelerators)

| Project | Language | Coverage | Notes |
|---|---|---|---|
| **darktable `spektrafilm`** (merged, 5.6/5.8 dev) | **C** + OpenCL | Film/paper, exposure, push/pull, development chemistry, **DIR couplers**, camera & print **diffusion** (4 families), **grain** (emulsion + texture), **halation** (scatter + halation + highlight boost), **scanner** (pre/post compression, sharpness, glare, black/white point), gamut compression, spectral-upsampling method selection | CPU + GPU; uses **PCHIP 3D LUTs** baked from spectra + spatial passes. Split: `src/common/spektra_core.{c,h}` (~47 KB + 15 KB, spatial + inline color), `data/kernels/grain.h` (17 KB), `data/kernels/spektrafilm.cl` (59 KB), `src/iop/spektrafilm.c` (~324 KB, includes module/UI/pipe glue). Uses a **data pack** (`pack.json`) generated from the spektrafilm Python package. GPL-3.0. |
| **`turbasvin/spektrafilm-rs`** | **Rust** | Claims the **full chain** incl. halation, DIR, grain (bit-exact numpy MT19937), glare, scanner; **bit-identical to the Python reference** at f64 | Has a **CPU f64 backend** (`ComputeBackend` trait; rayon + BLAS) and a wgpu GPU path. Rust → both a Windows `cdylib` (loadable as `.8bf`) and `wasm32`. Blazing-fast f32 GPU path. GPL-3.0. |
| **`rafaelcgs10/spektrafilm-art-darktable`** | C (ART) | ART port with a **pinned, reproducible data pack** (`pack.json` via an exporter flake) | Confirms the data-pack + LUT approach is portable. |

There are also `vkdt` (Vulkan) and an ART LUT-bridge, but the three above are the ones directly usable for a CPU `.8bf`.

**Why this matters:** you do not need to translate 8,000 lines of Metal into C. The color chain can be **baked into 3D LUTs** at build time (the OFX repo already emits LUTs and has `lutSafeParams`; the darktable module applies PCHIP-3D LUTs per pixel), and only the **spatially-varying passes** (halation, diffusion, DIR, grain, scanner blur/unsharp, glare) need real code. That's what makes a CPU plugin tractable.

---

## 4. The central tension (read this before choosing)

Your requirements collide on one axis:

- **Full behavioral parity** with the OFX shaders, **and**
- a **portable CPU core**, **and**
- **near-zero-maintenance updates** when `upstream/spektrafilm-ofx` changes.

You can have any two, but not all three, because the OFX behavior lives in **GPU shaders** that contain no CPU code path.

### Options

**Option A — Reuse the OFX renderer verbatim via software Vulkan (SwiftShader/Mesa lavapipe).**
- ✅ Literal parity; updating the submodule *is* the update; reuses `SpektraVulkanRenderer.cpp` unchanged.
- ❌ Not CPU (a bundled Vulkan ICD, 10–30 MB+); no WASM target (kills PhotoCraft testing); in-process `.8bf` + a software compute ICD is fragile and slow; large surface area.
- **Verdict:** the only route to *automatic* full parity, but it contradicts your CPU/dual-target choices. Keep as a fallback. A dedicated feasibility study (2026-10-12) confirmed the OFX render core is host-agnostic C++ that could be compiled into the `.8bf` for a ~20–100× GPU speedup, at the cost of a hard Vulkan-driver dependency and losing byte-parity with `tools/refrender`. Superseded by the threaded CPU engine (4.9× on 12 cores, byte-identical); the study is retired, its conclusion stands: viable fallback, not the plan.

**Option B — Seed a portable C core from darktable's `spektra_core` + wire it to the OFX data/params.** ✅ **recommended**
- ✅ C, CPU-only, MSVC- and wasm-friendly, ~90% of the parameter surface already implemented, PCHIP-LUT color chain keeps it fast; GPL-3.0 compatible.
- ❌ Algorithm lives in a *different* repo than your update source; the OFX-only features in scope (§2.1) must be ported — bleach bypass, leuco-cyan, shadow/highlight shape, C filter, auto exposure + meter, per-channel DIR/halation/diffusion/grain controls — and drift is possible.
- **Verdict:** fastest to a working, correct, fast CPU plugin; the update story is "data + params auto-refresh, physics port is maintained and guarded by a parity harness."

**Option C — Self-port the OFX shaders (Metal/Vulkan) to CPU.**
- ✅ Truest to the OFX source; updating the submodule gives you the diff to re-port.
- ❌ Enormous (≈8 k lines Metal / 7.9 k lines GLSL + 8 k lines host code), ongoing, easy to get subtly wrong. Not realistic for "minimal hassle."
- **Verdict:** not recommended as the primary strategy; use OFX as the *spec*, not as a translation source.

**Option D — Rust core from `spektrafilm-rs`.**
- ✅ Most complete + bit-exact; one language compiles to a Windows `cdylib` (`.8bf`) **and** `wasm32`; strong parity tooling.
- ❌ Rust toolchain + `cdylib` for an in-process `.8bf` has its own ABI/PiPL plumbing; ray-rayon/BLAS must be disabled for both MSVC-in-host and wasm; f64 CPU is ~14 s/16 MP (needs a preview/progress path).
- **Verdict:** strong alternative if you prefer one language for both hosts and want reference-grade parity.

### Decision matrix

| | Parity | CPU | WASM test | Update ease | Effort to first working plugin |
|---|---|---|---|---|---|
| A · Software Vulkan + OFX | ★★★ | ✗ | ✗ | ★★★ | Medium (but wrong shape) |
| **B · darktable C core + OFX data** | ★★ | ✓ | ✓ | ★★ | **Low–Medium** |
| C · Self-port OFX shaders | ★★★ | ✓ | ✓ | ★ | Very high |
| D · Rust `spektrafilm-rs` core | ★★★ | ✓ | ✓ | ★★ | Medium–High |

**Recommendation: Option B**, with `spektrafilm-rs` (D) kept as the **reference oracle** for parity testing and as the fallback core if B's coverage proves insufficient. Reject A/C for this use case.

---

## 5. Recommended architecture

```
spektrafilm/                              (your project)
├─ upstream/                    upstream checkouts, real upstream names
│   ├─ spektrafilm-ofx/         git submodule → chaert-s/spektrafilm-ofx (SPEC + DATA + PARAMS)
│   │                             Resources/data, tools/generate_profile_curves.py,
│   │                             src/SpektraParameters.h, src/SpektraAppBridge.h
│   ├─ spektrafilm/             reference-only: Python reference (fetch-sources.sh)
│   ├─ spektrafilm-rs/          reference-only: Rust port (fetch-sources.sh, @v0.1.2)
│   └─ vkdt/                    reference-only: Hanatos filmsim (fetch-sources.sh)
│
├─ spektra-core/                          portable CPU engine (new; seeded from darktable spektra_core)
│   ├─ include/spektra_core_abi.h         = SpektraAppBridge.h (kept byte-compatible)
│   ├─ src/color_chain.*                  RGB→raw upsample, film exposure, develop, print, scan (PCHIP-3D LUTs + curves)
│   ├─ src/spatial.*                      halation, scatter, DIR couplers, diffusion, grain, scanner blur/unsharp, glare
│   ├─ src/params.*                       RenderParams ↔ OFX descriptors; defaults, options, film/paper lists
│   ├─ data/                              generated tables/pack baked at build time
│   └─ third_party/darktable/             extracted darktable C engine, untouched
│                                             (spektra_sim.*, spektra_core.*, grain.h, …)
│   └─ third_party/darktable-spektrafilm/ data-pack repo (git submodule, b15cfe1)
│
├─ hosts/
│   └─ ps8bf/                             Windows .8bf adapter: PiPL + PluginMain + FilterRecord + Win32 dialog → core
│
├─ tools/
│   ├─ build.ps1                          ONE command: sync submodule → gen data → build core → .8bf → tests
│   ├─ gen_data.py                        wraps OFX generate_profile_curves.py → core data pack
│   └─ parity/                            compare core output vs OFX harness output / spektrafilm-rs oracle
│
└─ ROADMAP.md
```

**Design rules**
- **One core, one ABI.** The core exports exactly the `SpektraAppBridge`-shaped C functions. The adapter is a thin translator (host call → `SpektraRendererRender`).
- **Self-contained adapter.** The `.8bf` never touches any other host's ABI; PiPL + FilterRecord are the only host seam.
- **All heavy tables baked at build time** (LUTs, profiles, curves). No runtime file I/O — simplest for `.8bf` (single self-contained DLL).
- **Whole-frame render with ROI support.** The core already models a render window; on CPU, render the requested tile/ROI, keeping full-frame-only passes (auto-exposure, frame peak) computed once per invocation.
- **Input colour space is resolved by the host adapter** — document ICC profile when the host exposes one (Affinity), else the manual 26-space override (for hosts that expose no ICC) — and passed to the core as an explicit input-space id. The core has no implicit display-dependent default (§2.1, §6).

### The update workflow (how "drop in a new source" works)

1. `git submodule update --remote upstream/spektrafilm-ofx` (or drop a new zip over the folder — the script supports both).
2. `tools/build.ps1` runs:
   - ensure Python venv (`numpy scipy colour-science`),
   - regenerate data pack from `upstream/spektrafilm-ofx/Resources/data` (via the OFX generator),
   - re-import the OFX parameter/descriptor table (parse `SpektraParameters.h` + `SpektraAppBridge.*`) so new params/films/papers flow in automatically,
   - compile `spektra-core`, then the `.8bf` (MSVC),
   - run parity tests.
3. If the OFX *shaders* changed, the parity harness flags drift and you port just the diff into `spektra-core/src/spatial.*` / `color_chain.*`.

**Honest caveat:** data, film/paper lists, parameter names, defaults and option sets update **automatically**; algorithm changes update **only after you port the diff** (guided by the parity harness). This is the price of a CPU core, and it is the same price paid by darktable and spektrafilm-rs.

---

## 6. The `.8bf` host adapter (Affinity, Windows)

**Format facts**
- A `.8bf` is a **64-bit Windows DLL** with a **`PiPL` resource** declaring it as a **filter** plugin and naming the entry point. The host calls `PluginMain(selector, filterRecord)` (`SPBasicSuite*` for PICA).
- Filter protocol selectors: `filterSelectorAbout`, `filterSelectorParameters`, `filterSelectorPrepare`, `filterSelectorStart`, `filterSelectorContinue`, `filterSelectorFinish`. Images are declared per-plane (`inData`, `outData`, `inLoPlane`…`inHiPlane`) and the plugin processes **tiles/rows**, calling **`AdvanceState()`** to get the next block.
- Data model: RGB (and optionally alpha), **8- or 16-bit** channels; support multiple planes. Must not assume a single contiguous full-frame buffer.
- **No Adobe Photoshop SDK is required:** the required ABI is reimplemented in
  `pi/pi_types.h` + `pi/pifilter.h` and the PiPL is generated locally by
  `tools/gen_pipl.py`. Build with MSVC.

The MSVC build loads in **Affinity Photo**; because Affinity does not pre-fill the `filterSelectorContinue` buffers, the adapter drives pixel I/O through `AdvanceStateProc` (the model the known-good `gmic-8bf` uses): when the host offers neither `inData` nor `outData`, it gathers the selection plane-by-plane, renders the whole frame once, and scatters it back. Every selector also appends to `%TEMP%\spektrafilm.log` a build/version banner plus the loaded DLL path (identifies a stale install). A **Linux mock-host parity harness** (`test/harness.c` + `run.sh`) drives the exact selector bodies and confirms output is **byte-identical to `tools/refrender`** on the same input, including the 8/16-bit adapters, the DIR-coupler blur, the modern 32-bit-coordinate / single-buffer host shapes, and the **AdvanceState pull model**. **[DONE 2026-10-12] Threaded render** — the adapter installs a `CreateThread` pool on the engine's stage/loop hooks (`sp_chain_set_parallel` + `sf_set_loop_runner`); element-wise stages and the blur's rows/blocks split into contiguous ranges, **byte-identical to serial**, ~4.9× on 12 cores (1600×1200: 2.34 s → 0.48 s). No OpenMP (would import `vcomp140.dll`); `SPEKTRAFILM_THREADS` overrides the worker count. **[DONE 2026-10-13] Live image preview** — the dialog moved from `filterSelectorParameters` into `filterSelectorContinue` (an optional hook from `filter_loop.c`, so the harness stays byte-identical) and previews the user's own image through the same `sp_chain`, box-scaled to the control: the default *Whole image* view (supersampled whole-frame render, downscaled after rendering) or a true native-scale 1:1 view of the pane-sized region; "Last Filter" replays skip the dialog. Still open: the **spatial look stages** (grain, halation/scatter, camera/print diffusion) wired into `sp_chain` in the reference order, exposed in the dialog, and default off (default output byte-identical; all-stages-on asserted byte-identical threaded vs serial). See §8 Phase 2.

**Adapter design**
- **UI:** ship our own **Win32 modal dialog** driven by the OFX descriptors (groups → tabs, kinds → controls, min/max, film/paper combo boxes, process/scanner sections; out-of-scope groups rendered greyed-out per §2.1). Avoid Adobe ADM (deprecated/Windows-awkward) and avoid optional suites (the spetric test host doesn't provide `SuitePea`). The dialog runs from `filterSelectorContinue` (the `gmic-8bf` pattern) so the **live preview** can render the user's actual image on control change (whole-image fit or a 1:1 crop; it needs gathered pixels, which `filterSelectorParameters` does not have). Full render on OK.
- **Threading/safety:** the host may call on a non-UI thread and may load multiple plugin instances; keep the core reentrant, no global mutable state, `AdvanceState`-safe.
- **Tiling/large images:** honor the requested rect; render ROI; keep the coordinate origin stable so spatially-varying passes (halation/diffusion/grain/glare) are consistent across tiles (the OFX design already worries about this; darktable's `roi_x/roi_y`-anchored wide blur is the model).
- **Bit depth:** 16-bit is the sweet spot for Affinity; support 8-bit for compatibility; keep 32-bit/float as a stretch. Do the math in float internally and dither only at quantize.
- **Colour management (input space):** when the host exposes the document's ICC profile (Affinity via PICA color/image services), map its primaries + transfer curve onto the nearest OFX input-space entry and pass that id to the core; otherwise rely on the manual 26-space override (hosts that expose no ICC). Per §2.1 there is **no plugin-side output conversion** — the result stays in the document working space.
- **Self-contained:** statically link the CRT and the core; imports limited to `kernel32`/`user32`/`gdi32`.

**Testing without Photoshop/Affinity**
- **`spetric/Photoshop-Plugin-Host`** (`pspiHost.dll`, MIT): `pspiPlugInLoad` → `pspiPlugInExecute(hWnd)` loads and runs an `.8bf` (`capPspi` console tester, `NiGulp` GUI). Requires the Adobe SDK to build. Documented limits: RGB/BGR/RGBA/grayscale (+external alpha), **no** SuitePea, no channel ports, no descriptorParameters, no padding, no ICC.
  → Keep the plugin within those limits so it loads here.
- Alternatives that load `.8bf`: **IrfanView**, **XnView**, **GIMP via PSPI**. Any of these gives you a pixel-level regression surface on Windows.
- Final sign-off must be in **Affinity Photo** itself (you'll need access at least once).

---

## 7. The PhotoCraft (WASM) adapter *(dropped)*

Dropped 2026-10-12: the project targets Affinity/Photoshop (`.8bf`) only. The
WASM adapter, its host (`hosts/wasm/`) and its driver (`tools/pc-host/`) have
been removed from the tree, along with the wasm-only shims. The paragraphs
below are kept only so the earlier decisions remain legible.

- Compile `spektra-core` to **`wasm32`** (freestanding; supply `libm` for exp/log/pow/sin; use an in-code deterministic PRNG for grain).
- Export the v1 ABI: `pc_abi_version`, `pc_manifest` (JSON: name, params schema, etc.), `pc_alloc`, `pc_filter`. **No imports.** Embed the data pack in the module.
- Params: flatten `SpektraAppRenderParams` into the JSON schema so PhotoCraft's UI can drive film/paper/exposure/etc.
- **Budget planning:** fuel is `50M + 4000 × samples`. A 3 MP×3ch image is ~9 M samples → ~36 G instructions, far beyond 60 s in an interpreter. **Therefore the WASM path is for small crops/thumbnails and parameter-logic validation**, not full renders. Ship a "test mode" that renders ≤ ~0.5–1 MP.
- What this validates: core correctness, color chain, film/paper data load, parameter mapping. What it cannot validate: PiPL/`.8bf` packaging, Affinity dialog behavior, tiling, bit-depth handling.

---

## 8. Phased plan (target: locked v1 scope — §2.1)

Estimates assume one developer, part-time, familiar with C/C++.

### Phase 0 — Scaffolding & harness (foundation)
- **[DONE 2026-10-10] Extraction spike** → `spektra-core/`: darktable `spektra_sim.{c,h}` + `spektra_core.{c,h}` + `grain.h` run standalone (Linux/gcc), full spectral chain + a spatial filter verified deterministic. See `spektra-core/README.md`.
- Restructure to the layout in §5; add `upstream/spektrafilm-ofx` as a git submodule (support folder-drop too).
- ~~[DONE 2026-10-10] "hello WASM" de-risk~~ — **removed 2026-10-12.** `hosts/wasm/` + `tools/pc-host/` were deleted when the WASM path was dropped; the shared `pc_json.h` now lives in `spektra-core/shims/portability/`.
- **[DONE 2026-10-12] Threaded render path.** `sp_chain_set_parallel()` + `sf_set_loop_runner()` let the host split the element-wise stages and the blur's rows/blocks across a `CreateThread` pool (no OpenMP → keeps the KERNEL32/USER32/GDI32-only import guard). Bit-identical to serial; ~4.9× on 12 cores. See `hosts/ps8bf/README.md` §*Performance and threading*.
- `tools/build.ps1` skeleton: venv check → data generation → build stub core → build stub `.8bf` → run tests.
- Stand up the **parity harness**: a fixed set of test images + parameter files rendered by (a) an OFX harness on a Windows GPU machine, (b) the new core; emit diff metrics.
- **Exit:** one command produces an (empty) `.8bf` that appears in the spetric host.
- **Risk:** low (spike de-risked the seed; the remaining unknowns are JSON-portability and data choice).

### Phase 1 — Color chain (the non-spatial 90%)
- Baseline: the chain is **already extracted and running** (`spektra_sim`). Remaining work in this phase:
  - Replace glib/json-glib with an embedded JSON parser (spike build uses system libs; the MSVC `.8bf` must not).
  - Map the OFX descriptor surface → `sf_sim_params_t` + profile/development-time params (`development_min` must be plumbed, not constant).
  - **Input colour space mapping:** current core consumes linear RGB in caller-chosen primaries (`input_rgb_to_xyz` + CAT16 into the table illuminant) — exactly the auto-detect/manual-override model in §2.1; the adapter decides the primaries.
- Implement: input decode per the **resolved input colour space** (auto-detected document profile, else the manual menu — not the video-oriented ARRI LogC4 default), RGB→raw spectral upsample, film exposure, develop (curve interpolation), print (filtered-enlarger timing), scan, gamut compression inside the document working space. **No plugin-side display transfer/EOTF** — output stays in the working space and Affinity does delivery (§2.1).
- **Data-source decision (resolved 2026-10-16):** darktable pack 0.3.3 works today
  but carries only the **Hanatos** irradiation table and is CC BY-SA 4.0, so the
  OFX-only method data is layered on rather than regenerating the pack: Hanatos
  2025's `bandpass_hanatos2025` is merged from the OFX archive into the working pack
  (`tools/merge_bandpass_hanatos2025.py`) and Mallett 2019's basis is generated into
  the engine (`tools/gen_mallett_basis.py`). The pack stays in darktable format and
  the update flow stays one-command.
- Wire the descriptor/param table so film/paper/process/RGB-method/exposure work.
- Get **bit-close** parity on the bare chain (target: ≤1/255 max diff, like spektrafilm-rs).
- **Exit:** `.8bf` renders a full image in Affinity-compatible hosts, matching the OFX reference within tolerance.
- **Risk:** low-medium (LUT/half-float precision; data-method parity).

### Phase 2 — Spatial effects
- Port/validate: halation (scatter core+tail, back-reflection, highlight boost, protect, **per-channel strength**), DIR couplers (same/interlayer inhibition, diffusion tail, **per-channel gamma couplings**), camera & print diffusion (4 families, **core/halo/bloom intensity+size**), grain (**Production model**, MT19937-compatible, **Seed**), scanner (MTF blur, unsharp, glare).
- These are **already extracted** (`spektra_core.c` + `grain.h`); the work is wiring the spatial stages (darktable's module-level spatial calls + the grain render loop in `spektrafilm.c`) into `sp_chain`, with `pixel_um` scaling and full-frame (ROI-origin 0) coordinates.
- **[DONE 2026-10-13] Wired into `sp_chain`** in the reference order: boost → camera diffusion → halation/scatter (pre-film, linear) → … develop … → production grain (per-sub-layer particle draw, dye-cloud blur, clump blur, multiplicative unsharp recovery) → print diffusion → … scan … → scanner lens blur → scanner unsharp mask → viewing glare. New `sp_chain_params_t`/`sp_params.c` keys, persisted in the params block (v4) and exposed in the dialog's *Film look* / *Grain* / *Print diffusion* / *Scanner* groups. **Every stage defaults off**, so the default render stays byte-identical to the Phase-1 slice and all existing parity tests pass; the harness adds `test_phase2_stages` (each stage changes the render; all-stages-on is byte-identical threaded vs serial).
- **Still open:** per-channel halation strength/σ and diffusion group intensity/size have no darktable slider (the pack supplies them); scanner `blur`/`usm` are pixels in darktable but MTF/µm in OFX; the diffusion-family enum order differs between the two. Reconcile against OFX golden images.
- **Exit:** full-fidelity renders match OFX within tolerance across the test matrix.
- **Risk:** medium–high (grain stochastics + tile stability).
- **[DONE 2026-10-14] Full grain surface wired** (see `docs/GRAIN_SYSTEM_ROADMAP.md`): Preview/Production model selector, `grainSublayersEnabled` + `grainSubLayerCount` clamp, per-channel (`grainParticleScaleR/G/B`, `grainDensityMinR/G/B`, `grainUniformityR/G/B`) and per-layer (`grainParticleScaleLayer0..2`) overrides, and absolute-µm `grainFinalBlurUm`/`grainBlurDyeCloudsUm`. All overrides use **relative** semantics (1.0 = the stock's own figure) or negative "use the stock" sentinels, so every field is a no-op at its default and the default render stays byte-identical to `tools/refrender`; the params block is now **v15** and the harness adds `test_grain_surface` (per-field mutation changes the render; explicit defaults reproduce the stock byte-for-byte). Only the GPU **Grain Synthesis** family (`grainSynthesis*`, `grainSynthesisLayered`, `grainAnimate`, `grainMicroStructureSigmaNm`) remains a red stub.

### Phase 3 — Advanced parameters & parity-close (per §2.1 scope)
- Port the OFX-only features in scope: **bleach bypass (neg + print)**, **leuco-cyan coupling**, **shadow/highlight shape**, **C (cyan) filter**, **auto exposure + meter**, **DIR per-channel gamma couplings**, **halation per-channel strength**, **grain micro-structure, saturation, Seed**.
- **[DONE 2026-10-13] Wired every item the engine already supports** — live in the dialog, persisted in the params block (now **v5**), and covered by the harness:
  **filtration** (M/Y enlarger trim — the engine carries `m_filter_shift`/`y_filter_shift`),
  **preflash** (+ preflash M/Y trims), **DIR per-channel gamma couplings**,
  **per-channel halation strength**, and the **scanner black/white point**
  (correction flags + levels). New dialog groups: *Filtration / Preflash*,
  *DIR Couplers (Chroma)*, *Halation (Chroma)*, and the scanner black/white rows.
  DIR chroma seeds the stock's fitted coefficients then overrides the entries set
  (`0` = the stock's own); halation keeps `-1` = stock; all entries are no-ops at
  their defaults, so the default render stays byte-identical
  (harness `test_scope_extras`).
- **[DONE 2026-10-13] Film/print curve chemistry (params block v6).** Wired the
  engine-supported morph terms: **film push/pull** (Standard — shifts the film
  exposure by the stop count and scales the film curve slope by `1.15^stops`,
  exactly darktable's model), **film gamma** (`filmGamma`) and **print gamma**
  (`printGamma`), each scaling the per-stock density-curve slope. New *Film /
  Print (chemistry)* dialog group; all three are identity at `0/1/1`, so the
  default render stays byte-identical (harness `test_chem_stages`).
- **Closed since (params block v19, 2026-10-15):** bleach bypass (neg + print),
  leuco-cyan, film format, auto exposure + meter, grain
  micro-structure/saturation/Seed (Preview + Production models), shadow/highlight
  shape, print push/pull, cyan `filterC` trim, process modes + scan invert,
  printer lights, and camera UV/IR are all implemented and exposed in the dialog.
- **Closed since (params block v20, 2026-10-16):** the `rgbToRawMethod` selector —
  `rgb_to_raw_method` plus all three engine branches. Hanatos 2025 uses the
  per-profile `bandpass_hanatos2025` (present in the OFX archive profiles, merged
  into the pack by `tools/merge_bandpass_hanatos2025.py`); Mallett 2019 builds its
  3×3 matrix from the generated `SF_MALLETT2019_BASIS` at load and is opt-in
  pending golden validation. Both are additive per-profile fields, **not** a
  `pack_format` bump, and the default (Hanatos 2026) render stays byte-identical.

- **Note (2026-10-13, closed 2026-10-15):** bleach bypass and leuco-cyan have
  **no darktable engine counterpart** (`sf_sim_params_t` carried no such terms),
  so they needed new engine terms rather than pure wiring — those terms now exist
  and are exercised end-to-end.
- Complete option sets + Flavor tiers (Flow/Pro/Dev) → dialog tabs; keep out-of-scope controls present-but-disabled (§2.1).
- **Exit:** scope-complete parity for the implemented surface; parity harness green; disabled stubs verified.
- **Risk:** medium (bleach bypass + leuco cyan have no darktable reference — validate against OFX golden images).

### Phase 4 — Polish & ship
- **[DONE]** Progress/cancel: a per-chain progress hook (`sp_chain_set_progress`) reports stage fractions and honours the host's abort (`userCanceledErr`).
- **[DONE]** Live preview (real image, fit / 1:1 centre crop) and dialog controls for the spatial stages.
- **[DONE 2026-10-16]** Preview **display transform**: the preview bitmap is colour-correct for
  any document space (document curve/primaries → Bradford white adaptation → sRGB), a byte no-op
  at the sRGB default (`sf_preview_display`, `hosts/ps8bf/src/sf_preview.c`).
- **[DONE 2026-10-18] Bug hunt / hardening pass.** Every bug found in the pre-ship review is
  fixed: two MSVC link blockers (unmangled C++ `g_sf_hinst`; asset stdio shim vs the static CRT),
  several ICC-tag bounds reads (`icc_profile.c`), a `sf_parameters` null-deref order
  (`plugin_main.c`), auto-exposure drift (dedicated `auto_exposure_ev`; struct size unchanged at
  840), two C4244 casts, and build-dependency staleness for the pack's `profiles/*.json`. The
  Linux harness is green (byte-identical) and clean under ASan/UBSan; confirmed working in-host in
  Affinity on Windows 11. See `docs/GUI_ROADMAP.md` §12.
- **[DONE 2026-10-19] Presets.** A `Presets` group at the top of the dialog saves, loads, deletes,
  copies and pastes a whole look. The codec is `hosts/ps8bf/src/preset.c` — pure, with no disk or
  Win32 in it, so the Linux harness round-trips it bit-exactly (loading a preset never nudges a
  value, so the render stays byte-identical to what the dialog showed). It is flat JSON with
  film/paper stored as pack *stems* (a preset survives the pack being reordered), numbers in the
  shortest round-tripping decimal form, both directions locale-proof, unknown keys ignored and
  unrepresentable values refused. Files live in `%APPDATA%\Spektrafilm\presets\*.json`; Copy/Paste
  move a look through the clipboard as text. The import table is unchanged (KERNEL32/USER32/GDI32).
  See `hosts/ps8bf/README.md` §Presets.
- **[DONE 2026-10-19] About box.** `filterSelectorAbout` reports the real surface — film/paper
  counts from `pack_list.h`, the stage list, the preset hint — instead of the stale "Phase 1
  vertical slice" text. USER32 `MessageBoxA` only, so the import table is unaffected.
- **[DONE 2026-11-04] Dialog UX polish.** Esc/Enter focus and the About-modal interaction, DPI
  handling, scroll behaviour, checkbox tick rendering, the 1:1 crop filling the pane and 1:1 pan
  tracking, and the preview matching the final render (true 1:1 + supersampled whole image) — all
  verified in Affinity on Windows 11.
- **[DONE 2026-11-04] Packaging.** Single self-contained `.8bf` (imports KERNEL32/USER32/GDI32);
  `INSTALL.md` install guide; GPL-3.0-or-later license + notices.
- **Exit — met 2026-11-04:** signed-off use in Affinity Photo. **Released as v0.1.0, feature
  complete.** Remaining items are optional/out-of-scope only (OFX-GPU golden-image parity, the
  licensed APD spectral table, GPU-only grain-synthesis and Effects families) — see the §8 Phase 4
  notes and `hosts/ps8bf/README.md` §*Still open*.

---

## 9. Testing strategy

| Layer | Tool | Validates |
|---|---|---|
| Core unit/parity | `tools/parity` (CPU core vs OFX harness output, vs `spektrafilm-rs` oracle) | numeric correctness, drift detection |
| `.8bf` load/run | **spetric host** / `capPspi` / `NiGulp`, IrfanView, XnView, GIMP-PSPI | PiPL, dialog, tile/bit-depth handling, no external deps |
| Final | **Affinity Photo** (one-time access) | real host behavior, Filters menu, 16-bit, selection/mask |

Generate the reference set once with the OFX `SpektraVulkanCopyHarness` (Windows GPU) and store golden images + param files in `tools/parity/reference/`.

---

## 10. Licensing

- All candidate sources are **GPL-3.0** (OFX, spektrafilm Python, darktable module, spektrafilm-rs). Reuse is compatible.
- Personal use means GPL obligations are moot in practice, but keep the license/notice files in the build so the tree stays clean if it's ever shared.
- Adobe's `.8bf` ABI headers are public; ours are re-declared in `pi/` from that published layout, with the `FilterRecord` offsets pinned by compile-time asserts. No Adobe SDK source or headers are in the tree, so there is nothing Adobe-licensed to redistribute.

---

## 11. Risks & open questions

1. **Core source choice (B vs D).** If darktable's engine proves hard to extract cleanly (darktable type/memory coupling), switch the seed to `spektrafilm-rs` (D), which is already standalone and bit-exact. Decide after a 1–2 day extraction spike.
2. **Affinity 8bf quirks.** Affinity's SDK adherence is imperfect; test early with a trivial "invert" `.8bf` to validate PiPL/dialog/tiling before investing in the full adapter.
3. **Dialog scope.** Even with §2.1's cuts, the implemented surface is ~130 controls; the dialog is a real UI project. Mitigate with the OFX Flavor tiers (Flow first) and collapsible groups; disabled stubs render as greyed-out controls.
4. **CPU performance.** LUT-baked color chain is fast; the Production grain pass and wide diffusion blurs are the cost centers (grain synthesis is out of scope). Provide preview-vs-full quality and a progress bar; target interactive preview at reduced resolution.
5. **Update drift.** Managed by the parity harness; document a short "porting an OFX shader change" checklist.
6. **Toolchain.** MSVC toolchain set up (VS 2022/2026 Community is enough). No Adobe SDK acquisition is needed — the ABI headers are re-derived in `hosts/ps8bf/pi/`.
7. **Affinity ICC exposure.** Whether Affinity exposes the document's ICC profile to filter plugins (via PICA color/image services) is unverified — some test hosts have no ICC. If Affinity doesn't expose it, the manual 26-space override becomes the primary input-space path. De-risk early (Phase 0/1) with a profile probe.

---

## Grain and halation full-feature roadmaps

Detailed supporting roadmaps now live under `docs/`:

- `docs/GRAIN_SYSTEM_ROADMAP.md` -- production-grain parity, per-channel support,
  grain model selector, sublayers, micro-structure, seed, and verification gates.
- `docs/HALATION_SYSTEM_ROADMAP.md` -- source halation feature matrix,
  per-channel sigma, bounce controls, numeric stability, and parity tests.
- `docs/GUI_ROADMAP.md` -- parameter-dialog scroll-flicker fix and the migration
  to a Dear ImGui UI (ImGui's stock Direct3D 11 renderer); the native dialog was
  retired in B6.

The shipping path currently remains limited to the production grain stage and the
engine-backed halation scatter/back-reflection path. GPU-only grain synthesis and
delivery/output transform controls remain red-stubbed for this CPU `.8bf` target.


## 12. Immediate next steps (suggested)

1. **Seed decided — DONE (2026-10-10).** The extraction spike succeeded:
   `spektra-core/` builds standalone on Linux/gcc and renders the full spectral
   colour chain (`spektra_sim` expose→develop→print→scan) plus a spatial filter
   (`sf_unsharp_mask3`), deterministically. Only coupling was glib/json-glib for
   pack/profile JSON — swappable for an embedded parser (MSVC/wasm32 needs it).
   One shim bug found & fixed along the way (`dt_iop_image_copy` takes a float
   count, not bytes — see `spektra-core/README.md`). No pivot needed; drop
   `spektrafilm-rs`.
2. **Restructure the repo — DONE (2026-10-10).** `git init` (main), §5 layout in place
   (`hosts/ps8bf`, `tools/`, `tools/parity`), two submodules
   registered: `upstream/spektrafilm-ofx` → `chaert-s/spektrafilm-ofx` (@86476af) and
   `spektra-core/third_party/darktable-spektrafilm` (pack, @b15cfe1). Both
   byte-verified identical to the original folder drop (moved to `backup/`).
   Baseline commit `7ca55df`. Local git identity set to "Spektrafilm Dev
   <dev@spektrafilm.local>" — adjust to taste (`git config user.name/email`).
   Folder-drop still supported: replace `upstream/spektrafilm-ofx/` contents directly.
3. **Write `tools/build.ps1`** (venv → data gen → core → stub `.8bf`).
4. **Build a "hello `.8bf`"** (invert filter, PiPL, Win32 dialog with one slider) and load it in the spetric host / IrfanView to de-risk the packaging.
5. ~~"hello WASM" — DONE (2026-10-10)~~ — **removed 2026-10-12** along with the
   WASM path (`hosts/wasm/`, `tools/pc-host/`).
6. **Stand up the parity harness** and capture golden images from the OFX Vulkan harness.

Once 4 and 6 are green, the remaining work is filling in `spektra-core` phase by phase.

---

## Appendix A — Key references

- OFX (source of truth): `https://github.com/chaert-s/spektrafilm-ofx`
- Algorithm origin: `https://github.com/andreavolpato/spektrafilm`
- CPU seed (C): `darktable` — `src/common/spektra_core.{c,h}`, `data/kernels/grain.h`, `data/kernels/spektrafilm.cl`, `src/iop/spektrafilm.c`; manual: `https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/spektrafilm/`
- CPU seed (Rust, bit-exact): `https://github.com/turbasvin/spektrafilm-rs`
- ART port + data pack: `https://github.com/rafaelcgs10/spektrafilm-art-darktable`
- `.8bf` test host: `https://github.com/spetric/Photoshop-Plugin-Host`
- `gmic-8bf` (a real, modern 8bf filter, useful as a build reference): `https://github.com/0xC0000054/gmic-8bf`
- Photoshop C++ recipes (PiPL, entry points, ImGui dialog): `https://sonictk.github.io/ps_cpp_recipes/`
- Affinity plugin help: `https://www.affinity.studio/help/filters-plugins/`

## Appendix B — Local file map (this checkout)

The `upstream` checkout now holds **two** trees: the Python reference
(`upstream/spektrafilm/`, v0.3.4) and the OFX project
(`upstream/spektrafilm-ofx/`, v0.3.12).

- C ABI seam: `upstream/spektrafilm-ofx/src/SpektraAppBridge.h`
- Renderer interface: `.../src/SpektraRenderer.h`
- Full parameter struct + enums: `.../src/SpektraParameters.h`
- OFX plugin describe/render: `.../src/SpektraFilmPlugin.cpp` (render window handling ~line 4807)
- GPU renderers: `.../src/SpektraMetalRenderer.mm`, `.../src/SpektraVulkanRenderer.cpp`
- Data generation: `.../tools/generate_profile_curves.py`, `.../tools/ofx_stock_lists.py`
- Build: `.../CMakeLists.txt` (data gen, three flavors, Windows Vulkan + harnesses)
- Data: `.../Resources/data` (~22 MB: profiles, filters, LUTs)
- Shaders: `.../shaders/SpektraFilm.metal`, `.../shaders/vulkan/*.comp`
