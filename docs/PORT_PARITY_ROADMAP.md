# Port parity roadmap — .8bf vs upstream

> **Historical inventory - largely closed.** Written to scope the gaps that
> remained before "plugin completion"; by v0.1.0 the engine-backed surface
> is fully wired (every engine-backed parameter has a dialog control). What
> this document lists as blockers is therefore mostly implemented; what
> genuinely remains open is two items: the APD printer-timing spectral
> calibration (blocked on the licensed `st2065-2a/2b-2020.csv`) and optional
> OFX golden-image parity (needs a Windows GPU capture) - plus the
> GPU-only *Effects* family, recorded in `docs/EFFECTS_FAMILY_FINDINGS.md`.
> Current state: `hosts/ps8bf/README.md` §*Validation status* and
> §*Locked-scope extras*. The body is kept unmodified as the record of how
> the scope was reasoned about at the time.

Companion to `HALATION_SYSTEM_ROADMAP.md` (which covers halation/scatter in detail)
and `GRAIN_SYSTEM_ROADMAP.md`. This document inventories the features exposed by
`upstream/spektrafilm-ofx/src/SpektraParameters.h` (and the host/UI layers around it)
that the .8bf still lacks, so the work still ahead of "plugin completion" can be
scoped explicitly. Status tags:

- **Implemented** — exposed on the persisted block, engine, and UI.
- **Partial** — some surface or path exists, but not the full source feature.
- **Missing** — not ported yet.
- **Out of SDR scope** — intentionally excluded by the .8bf's SDR-only,
  still-image contract (per `hosts/ps8bf/README.md`).

The source is a full NLE-grade OFX with HDR output, GPU tiling, and diagnostic
render modes. Those are **Out of SDR scope** and listed only for completeness;
the SDR still-image subset is the actual port target.

> **Effects family (vignette / bokeh / coma / chromatic aberration / film gate /
> gate weave / timing shift / flicker + breathing / film damage / telecine):** a
> newer OFX (v0.3.12, Vulkan) exposes these, but they have **no source and no CPU
> engine path** — they are GPU-only and out of scope for this port. Evidence and
> the full inventory are in `docs/EFFECTS_FAMILY_FINDINGS.md`.

## Scope decisions (user-confirmed)

- Disregard importing all 26 working color spaces - keep the few useful for
  still-image work (already 3; input space management handled upstream).
- ~~Disregard RGB-to-Raw method selector beyond default Hanatos 2026 path.~~
  **Revised 2026-10-16:** all three methods are now implemented and exposed
  (Hanatos 2026 default, Hanatos 2025, Mallett 2019) — see *RGB→raw method*.
- Disregard enlarger scale/offset (low value inside an image editor).
- Grain synthesis is GPU-only; skip for now (see Q1 below).
- To port: ProcessMode paths beyond print/scan, PrintTimingMode,
  printer lights, and Camera UV/IR filters (see each section's status).

### User questions

**Q1 - GPU support in this 8bf?** No, realistically not. This adapter is a
CPU .8bf built on a C engine (spektra-core). Modern Photoshop does have
GPU-aware plug-in approaches (ImageEngine / OCIO / Smart Render, or AE-style),
but the classic FilterRecord ABI used here runs the filter on the CPU. GPU
acceleration would require a different plug-in path (e.g. an After Effects /
Premiere SDK plug-in, or rewriting the filter as a host GPU-side shader program),
which is outside this port's scope. SDR CPU only.

**Q2 - "Output conversion decision"?** The OFX exposes an outputColorSpace plus
HDR tone-mapping / output-role paths that convert the result into a chosen
target gamut/transfer on the way out. The `.8bf` is **space-preserving**: the
engine is told to write its output in the *same* primaries + white point as the
resolved input space (the document profile when auto-detect is on, else the
manual `inputColorSpace` menu), so the pixels are handed back to Photoshop in the
document's own space and are not re-interpreted. (Photoshop does **not**
colour-manage filter I/O — it neither converts the plug-in's output nor changes
the document profile — so the plug-in owns this step.) This supersedes the
earlier "no output conversion / Photoshop does colour management" wording, which
was wrong. The default sRGB case is byte-identical to the old sRGB-primaries
output. Arbitrary user-chosen output gamuts / HDR output roles remain out of SDR
scope.

## 1. Process mode / output conversion (source `ProcessMode`, `OutputRole`, `RenderOutputMode`, `Hdr*`)

| Source field | Status | Notes |
|---|---|---|
| `process` PrintSimulation / ScanNegative / ProcessNegative | Implemented | `process_mode` on the block; mode 2 (ProcessNegative) maps to develop-only in the engine. |
| `scanNegativeInvert` | Implemented | `scan_negative_invert` on the block; applied at scan. |
| `rgbToRawMethod` (Hanatos 2026/2025, Mallett 2019) | Implemented | `rgb_to_raw_method` on the block + engine branches; combo `{Hanatos 2025, Mallett 2019, Hanatos 2026}`, default Hanatos 2026 keeps the render byte-identical (see *RGB→raw method* below). |
| `printTiming` (FilteredEnlarger vs APD printer) | Partial (trim approx, APD table unavailable) | Engine toggle enables per-channel trim; APD calibrated neutrality requires licensed ST 2065-2 table. |
| `outputColorSpace` + ICC round-trip | Space-preserving | the engine outputs in the resolved input primaries/white (auto-detected document profile or the manual menu); no user-selectable output gamut. |
| `outputRole` (SDR/HDR/RCM) | Out of SDR scope | .8bf is SDR-only by design. |
| `hdrPreset/hdrTransfer/hdrPeakNits/.../hdrToneMapping` | Out of SDR scope | |
| `renderOutput` (diagnostic film-density/log-raw previews) | Out of SDR scope | |
| `colorAdaptation*` (input/output chroma compression) | Out of SDR scope (HDR tooling) | |
| `gpuRenderTiling` | Out of SDR scope / N/A | .8bf processes the whole selection as one tile. |

## 2. Gamut / color management (source `ColorSpace`, `inputColorSpace`)

| Source field | Status | Notes |
|---|---|---|
| `inputColorSpace` (26 working spaces) | Disregarded beyond existing few | keep the existing still-image set. |
| ICC auto profile detection | Implemented (basic) | `input_profile_auto` + `input_custom_rgb_to_xyz` path; the dialog shows the detected profile name under the checkbox. |
| custom document primaries | Implemented | also drives the space-preserving output (same primaries/white on the way out). |

## 3. Camera / optical pre-filters (source `cameraUvFilter*`, `cameraIrFilter*`)

| Source field | Status | Notes |
|---|---|---|
| `cameraUvFilterEnabled`, `cameraUvCutNm` | Implemented (spectral) | Engine applies applyCameraBandPass-style spectral renormalisation at sf_sim_build time. |
| `cameraIrFilterEnabled`, `cameraIrCutNm` | Implemented (spectral) | Engine applies ~15 nm IR roll-off with channel renormalisation at sf_sim_build time. |

## 4. Exposure / film chemistry (source `filmExposureEv .. filmHighlightShape`)

| Source field | Status | Notes |
|---|---|---|
| `filmExposureEv`, `printExposureEv` | Implemented | |
| `autoExposure` (+ method) | Implemented | |
| `filmPushPullStops/Mode`, `filmGamma` | Implemented | |
| `printPushPullStops`, `printGamma` | Implemented | |
| `negativeBleachBypassAmount`, `negativeLeucoCyanCoupling` | Implemented | |
| `printBleachBypassAmount` | Implemented | |
| `printShadowShape`, `printHighlightShape` | Implemented | |
| `filterC/M/Y`, `preflash*` | Implemented | |
| `enlargerScale` | Disregarded | low value in image editor |
| `enlargerOffsetXPercent/YPercent` | Disregarded | |
| `printerLightsR/G/B`, `printerLightsGang`, `printerLightCalibration` | Implemented (non-spectral trim) | Host + engine apply the per-channel trim and gang/calibrate at `print_timing == 1`; the APD spectral calibration offset stays zero pending the licensed ST 2065-2 table. |
| `dirCouplers*` spatial/params | Implemented | Amount + same/inter inhibition + spatial diffusion + per-channel chroma gammas. |

## 5. Film formats / pixel size (source `FilmFormat`)

| Source field | Status | Notes |
|---|---|---|
| `filmFormat` + gauge-derived `pixel_um` | Implemented | `IDC_FILM_FORMAT`. |
| `grainSynthesis*` (synthesis model params) | Skipped (GPU-only) | GPU-only synthesis; out of scope. |

## 6. Grain surface (source grain fields)

| Source field | Status | Notes |
|---|---|---|
| grain model/sublayers/count | Implemented | |
| per-channel particle scale/layer | Implemented | |
| per-channel uniformity/density-min | Implemented | |
| final blur um / dye-cloud um | Implemented | |
| grain micro structure scale/sigma | Implemented (scale) / stored-only (sigma, by decision) | The OFX sigma field is dormant at its own default (`sigmaNm > 50·pixel_um` gate); the port keeps `grain_micro_structure_scale` as a dye-cloud blur multiplier. See `GRAIN_SYSTEM_ROADMAP.md` §*Format scaling review*. Grain format scaling reviewed 2026-10-16 and kept as-is. |
| grain animate/seed | Partial | Seed ported; `grainAnimate` not (single-image host, no temporal). |
| grain amount/saturation | Implemented | |

## 7. Halation / scatter (source halation fields)

`HALATION_SYSTEM_ROADMAP.md` is authoritative. Summary:

| Feature | Status |
|---|---|
| halation on/off, scatter/halation amount+scale | Implemented |
| per-channel halation strength | Implemented |
| per-channel first sigma | Implemented (phase 1) |
| first-sigma ceiling (phase 2) | Implemented |
| halation profiles combo | Implemented |
| bounce count/decay curve | Missing (deferred; source hard-coded) |
| camera diffusion | Implemented |
| print diffusion | Implemented |
| larger-highlight tests / large-image perf tests | Missing (phase 3) |

## 8. Scanner (source scanner fields)

| Source field | Status | Notes |
|---|---|---|
| scannerEnabled / white/black correction / levels | Implemented | |
| glarePercent / glareRoughness / glareBlur | Implemented | |
| scannerMtf50LpMm / scannerUnsharpRadiusUm / amount | Implemented | |
| camera optical blur at scan time | Partial | `scan_blur` used; physical lens model simplified. |

## 9. Host / platform features

| Source capability | Status | Notes |
|---|---|---|
| GPU render tiling | Out of SDR scope | Engine runs whole-frame. |
| 32-bit float primary mode | Partial | Sample mapping + path exists; UI/support maybe limited. |
| HDR output path | Out of SDR scope | |
| Real-time incremental / live NLE integration | Out of SDR scope | .8bf is a panel filter. |
| Profile store / film scanning pipeline | Missing | The .8bf embeds the data pack; no external profile scanning. |

## Status of the earlier "quick wins" (updated 2026-10-15)

Most of the items this inventory originally listed as "missing" have since been
closed by the `.8bf` engine (see the status columns above):

- `inputColorSpace` full 26-space list — **disregarded** (scope decision: keep the
  still-image set).
- `rgbToRawMethod` selector — **done** (`rgb_to_raw_method`; see *RGB→raw method*
  below).
- `process` modes beyond print/scan + `scanNegativeInvert` — **done**
  (`process_mode`, `scan_negative_invert`, exposed in the dialog).
- Printer lights — **done** (R/G/B trim + gang + calibrate); `enlargerScale`/offsets
  remain **disregarded**.
- Camera UV/IR filters — **done** (spectral band-pass inside `sf_sim_build`).
- `printTiming` APD density path — **partial**, blocked on the licensed ST 2065-2
  table.

### .8bf dialog coverage (2026-10-15)

The `.8bf` dialog now exposes every engine-backed parameter the earlier layouts
left out: film format (`IDC_FILM_FORMAT`) + push/pull mode, negative bleach bypass
+ leuco cyan, auto exposure + meter, print push/pull + shadow/highlight shape +
print bleach bypass, the camera and print diffusion core/halo/bloom detail, and
scanner MTF50 + unsharp radius — plus a halation return-colour picker that maps a
colour onto the per-channel weights. `sf_params_to_chain` now projects the four
chemistry fields it previously dropped, and the duplicated `sf_params_init` in
`filter_loop.c` is gone. All new controls are no-ops at their defaults, so the
default render stays byte-identical. As of 2026-10-16 the RGB→raw method selector
is also wired, so every engine-backed parameter now has a dialog control.

## Roadmap action items derived from this inventory (updated 2026-10-15)

- Add `Print/enlarger` parameter group to `sf_params_block` and dialog. → **done**
  (printer lights / film format / printer process groups).
- Extend input-color-space combo (or populate from `sp_color_space_count`). → **disregarded**.
- Camera filter (UV/IR) stage port. → **done**.
- Printer process / ProcessNegative / scan invert. → **done**.
- rgb-to-raw selector (engine-side dispatch). → **done** (`rgb_to_raw_method`).
- Halation larger-highlight perf tests (already listed in Phase 3). → **open**.

## RGB→raw method

Status: **implemented, 2026-10-16.**

Source: `upstream/spektrafilm-ofx/src/SpektraVulkanRenderer.cpp`
(`makeHanatosRawResponse` ~L642, `makeMallettRawMatrix` ~L535) and
`SpektraParameters.h` `RgbToRawMethod { Hanatos2025 = 0, Mallett2019 = 1,
Hanatos2026 = 2 }`, default `Hanatos2026`.

The engine field is `rgb_to_raw_method` (`sp_chain_params_t`), surfaced as
`IDC_RGB_TO_RAW_METHOD`. We mirror the OFX **enum order** in the combo labels —
`{ "Hanatos 2025", "Mallett 2019", "Hanatos 2026" }` — so the combo index equals
the stored value, unlike the OFX combo which is ordered `{ "Hanatos 2026",
"Hanatos 2025", "Mallett 2019" }` and maps index → enum. Default index/value 2
(Hanatos 2026).

- **Hanatos 2026** (default): the erf4 adaptation window folded into the film
  sensitivities (`sf_sim_build`, guarded by `adaptation_bandwidth`) plus the
  optional degree-4 surface, using the profile's
  `hanatos2025_adaptation_window_params` and the `reference_illuminant`. (The
  engine header labels this "hanatos2025 sensitivity adaptation" — the *name* is
  stale; the *math* is Hanatos2026.) Unchanged from before.
- **Hanatos 2025**: a baked per-(wavelength, channel) `bandpass_hanatos2025`
  array (81×3) applied to the camera-filtered sensitivities with **no** window and
  **no** renormalisation. The array only exists in the OFX **archive** profiles
  (`upstream/spektrafilm-ofx/Resources/data/profiles/archive/*.json`),
  not in the darktable pack. `tools/sync_pack.py` copies it in while deriving the
  git-ignored build pack `spektra-core/data/pack` — it is the only writer of that
  directory, and every build route runs it before `gen_assets.py` embeds the pack.
  Profiles with no
  archived bandpass (print papers, B&W) fall back to Hanatos 2026, matching the
  archive.
- **Mallett 2019**: a 3×3 matrix
  `M[out][in] = Σ_wl sens[wl][out]·basis[wl][in]` on **linear sRGB**, divided by
  the mid-grey green response. The basis (`SF_MALLETT2019_BASIS[81][3]`) is
  generated by `tools/gen_mallett_basis.py` into
  `spektra-core/third_party/darktable/spektra_mallett_basis.h`, and the matrix is
  built at chain load. The normaliser uses the **un-bandpassed** sensitivity.
  Opt-in and **pending golden validation** against a Windows OFX capture; the
  input-space matrix uses the engine's CAT16 to D65 (the OFX reference uses CAT02 —
  ~1e-4 difference for non-sRGB input spaces, identical for sRGB index 0).

Because these are **per-profile** fields (like the existing window/surface
coefficients), no `pack_format` bump is required: the loader accepts additional
optional profile members, and the Hanatos 2026 default render is unchanged — the
harness pins it byte-identical to `tools/refrender` and checks each method against
`refrender` fed the same `sp_params` key.

## Detailed blocker notes from the source tree

### PrintTiming (APD printer-light responsivity)
- **Source**: `shaders/SpektraFilm.metal` (`spektra_apd_printer_timing_exposure_scale`),
  `shaders/vulkan`, `SpektraMetalRenderer.mm` (line ~1983). Uses an APD printer
  light exposure scale based on `academyPrinterDensityData` and
  `printerLightCalibration`.
- **What `.8bf` would need**: package the academy printer-density table for each
  film/paper pair into the pack (see `tools/generate_profile_curves.py`
  `academy_printer_density_available()`), and teach `sf_sim_print_expose` to apply
  per-channel calibration when `printTiming == ApdPrinterDensity`.
- **Engine impact**: large; possibly requires spectral energy constants.

### Camera UV/IR filters
- **Source**: `SpektraMetalRenderer.mm applyCameraBandPass` (about L1520):
  `smoothErfEdge(wl, cutNm, transition)` with UV transition 8 nm and IR 15 nm,
  combined as `(uv)*(ir)`, and renormalizing each channel's sensitivity by
  `(numerator/denominator)`. Applied to the film sensitivity curve.
- **What `.8bf` would need**: spectral sensitivity modification of the film
  profile at chain creation based on cut wavelengths.
- **Engine impact**: would require `sf_sim_build` to accept a filtered
  sensitivity curve, or re-run sim build with `filter_params`.

### Printer lights
- **Source**: `spektra_printer_light_exposure_scale` (lines around 1980),
  shader uses printer lights + gang + calibration and opacity exchange.
- **What `.8bf` would need**: printer-light trim values per channel applied to
  print exposure scale; `printerLightsGang` to link edits.
- **Engine impact**: per-channel exposure delta against each film.
