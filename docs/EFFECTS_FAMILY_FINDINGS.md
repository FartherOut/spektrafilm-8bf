# Findings — the "Effects" family (vignette / bokeh / coma / CA / film gate / timing shift …)

Status: **investigation complete, 2026-10-15**. This document records what the
supplied runtime bundles actually contain, so the `.8bf` port scope is grounded in
evidence rather than guesses. The tree was reorganized on 2026-10-16 (everything
under `docs/spektrafilm-*` moved into the upstream checkouts, which now live under
`upstream/`); the paths below are the current ones.

## Question

The `hosts/ps8bf/res/resource_ids.h` header carries orphan control IDs for a lens /
"Effects" family (`IDC_EFX_VIGNETTE_TOGGLE`, `IDC_VIGNETTE_*`, `IDC_BOKEH_*`, …).
Do the runtime files added under `docs/` contain information about these features,
and are they real anywhere in the codebase?

## Direct answer

**Yes, the family is real — but it exists *only* as a compiled, GPU-only Vulkan
feature set in the shipping OFX 0.3.12 runtime.** There is no source for it in any
of the supplied folders, and **no CPU/engine implementation** anywhere in this repo.
It therefore cannot be "wired" into the `.8bf` the way the engine-backed controls
were; it would require a new CPU implementation.

## What each source folder is

| Folder | What it is | Effects family? |
|---|---|---|
| `upstream/spektrafilm/` | Upstream **Python reference implementation** (Andrea Volpato, `github.com/andreavolpato/spektrafilm`, GPLv3, v0.3.4). CPU "model" reference: `runtime/stages/{filming,printing,scanning}.py`, `model/{couplers,diffusion,grain,glare,density_curves,develop,color_filters}.py`, PySide GUI, LUT creator. | **No.** Only lens code is `utils/raw_file_processor.py`, which uses `lensfunpy` to **correct** (undistort / TCA / vignette-correct) RAW imports — the opposite of simulating them. |
| `upstream/spektrafilm-ofx/` | **OFX plugin source** (C++/Metal/Vulkan; v0.2.8 macOS, v0.1.9 Win/Linux). Carries the parameter surface (`src/SpektraParameters.h`) and the renderer math (`src/SpektraVulkanRenderer.cpp`) this port follows — including `rgbToRawMethod` and the Hanatos/Mallett response builders. | **No** — `effectsGroup\|bokehGroup\|filmGateGroup\|chromaticAberration` → 0 hits. |
| `upstream/vkdt/` | Hanatos' **vkdt** tree (`src/pipe/modules/filmsim/`), the origin of the Hanatos sensitivity model. | **No.** |

The investigation below was made against the **compiled OFX v0.3.12 bundles**
(`spektrafilm`, `_flow`, `_lens`, `_diffuse`) that shipped under `docs/`. Those
bundles are **no longer in the working tree**, so their string tables cannot be
re-inspected here; the inventory is retained from that snapshot. `update_config.json`
in the bundle reported `version 0.3.12`, `build 312`, `platform: vulkan`. The OFX
*source* that is in the tree is the older v0.2.x surface above, which does **not**
contain the family.

## The Effects family, as exposed by OFX 0.3.12

`effectsGroup` is a top-level group whose tooltip reads *"Film-gate, Bokeh,
Vignetting and other effects."* It contains (parameter-identifier counts from the
binary string table):

| Subsection | ~params | Notes |
|---|---|---|
| Lens Distortion (`lensDistortionGroup`) | 30 | presets: Subtle Barrel, Vintage Moustache, Telephoto Pincushion, Decentered Vintage, Anamorphic Wide, Anamorphic Mumps, Equidistant/Equisolid/Stereographic/Orthographic/Circular Fisheye |
| Vignette (`vignetteGroup`) | ~30 | shape roundness, aperture f/, natural falloff, mechanical vignetting, edge darkening, color shading |
| Bokeh (`bokehGroup`) | ~45 | center/edge defocus, blades, curvature, rotation, rim bias, cat-eye, swirl, Petzval controls, highlight response |
| Coma (`comaGroup`) | ~35 | edge coma, field onset/falloff, orientation, tangential spread, RGB scale, shared pupil |
| Chromatic Aberration (`chromaticAberrationGroup`) | ~40 | lateral radial/tangential R/B shifts, radial smear, axial color blur, RGB defocus, highlight/purple fringe |
| Film Gate (`filmGateGroup`) | 13 | enable, black outside, affect Distortion/Vignette/Bokeh/Coma, ratio, rounded corners, softness, padding |
| Gate Weave (`gateWeaveGroup`) | 14 | camera/printer/scanner stages, amplitudes, rotation, crop |
| Flicker & Breathing (`flickerBreathingGroup`) | 30 | color/spatial flicker, image breathing, drift/ripple mixes, stage weights |
| Timing Shift Box (`timingShiftGroup`) | 16 | enable/preset/offset/jitter/mix/highlight-emphasis/jitter-rate |
| Film Damage (`dustHairsGroup`, …) | 58+ | dust, dirt, hair, fibre, scratch, leader |
| Sprockets / Telecine (`sprocketsWritingGroup`, `telecineGroup`) | 10 | edge writing, telecine |

There is also a **Quick Access** group of mirror toggles
(`quickVignetteEnabled`, `quickBokehEnabled`, `quickComaEnabled`,
`quickChromaticAberrationEnabled`, `quickFilmGateEnabled`, `quickTimingShiftEnabled`, …),
plus per-subsection preset combos (Lens Distortion / Vignette / Bokeh / Coma / CA /
Film Gate / Flicker / Timing Shift / Film Damage Preset).

## Evidence

- **Shaders** (referenced by name in the `.ofx` binary and shipped as `.spv`):
  `SpektraBokeh{Field,FieldTier1..3,Pyramid,Copy}`,
  `SpektraComaField{Tier1..3}`, `SpektraDistortionPyramid`,
  `SpektraLens{Prepare,PrepareFlicker,PrepareBreathing,PrepareBreathingFlicker,Gather,GatherFlicker,Distortion,AxialBlurU,AxialBlurV}`,
  `SpektraTimingShift{BlockScan,BlockOffsets,AddOffsets}`.
- **Parameter strings / tooltips** embedded in the binary (e.g. *"Primary
  transverse Coma scales with the inverse square of this value."*, *"Lens focal
  length used by the physical natural-falloff model."*).
- **Presets**: `.spkpreset` files (e.g. `TimingShift_Subtle.spkpreset`), JSON with
  `"payload_encoding": "obfuscated-snapshot-hex"`, so values are **not** readable.

## What is *not* available

- **No source** for the Effects anywhere: not in `upstream/spektrafilm-ofx` (OFX v0.2.x;
  grep for `effectsGroup|bokehGroup|filmGateEnabled|chromaticAberration*` → 0 hits),
  not in `backup/`, not in the upstream Python reference, not in `spektra-core`.
- **No CPU implementation.** `spektra-core/sim_port` mirrors the Python reference's
  model scope (film / print / couplers / diffusion / grain / halation / glare /
  scanner) and has no optics/effects stages.
- **No GLSL source** — only compiled SPIR-V (disassemblable with
  `spirv-dis`/`spirv-cross`, but that is a reverse-engineering effort, not a port).
- The bundled **`manual.pdf` (v0.3.12) predates the Effects family** — its table of
  contents has no Effects section. The reference guide shipped in the source tree,
  `upstream/spektrafilm-ofx/documentation/spektrafilm_reference_guide.pdf`,
  likewise covers only the engine-backed surface.

## Implication for the `.8bf` / `spektra-core`

The orphan `IDC_EFX_*` / `IDC_VIGNETTE_*` / `IDC_BOKEH_*` identifiers in
`hosts/ps8bf/res/resource_ids.h` were forward-declared from a newer OFX surface, but
they are **not engine-backed**. Unlike film-format, auto-exposure, push/pull,
bleach bypass, leuco-cyan, diffusion detail, MTF50/USM, etc. — which map onto existing
fields in `sf_params_block` and only need UI wiring — the Effects family would require
**new CPU implementations** (or embedding a GPU pipeline in the classic FilterRecord
host, which is out of scope per `hosts/ps8bf/README.md` and
`PORT_PARITY_ROADMAP.md` Q1).

Decision: **keep them out of the default-render parity work.** The `.8bf` exposes the
engine-backed surface only; the Effects family stays an OFX/Vulkan feature.

## If a port is ever revisited

1. Disassemble the `.spv` shaders (`spirv-cross --output`) to recover the algorithms
   (Bokeh/Coma pupil models, distortion pyramid, timing-shift masks).
2. Recover the full parameter schema (names/labels/ranges/defaults/tooltips) from the
   binary string tables and the Quick Access mirrors.
3. Implement CPU counterparts as new `spektra-core` stages, then wire them into
   `sf_params_block` and the dialog — the same pattern as the other engine features.
