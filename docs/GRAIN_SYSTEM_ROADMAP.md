# Grain system roadmap

> **Historical roadmap - scope shipped.** The production-emulsion grain
> surface described here is implemented and released in v0.1.0 (the
> "implemented" markers below are the final state); GPU Boolean/FBG grain
> synthesis remains a feature roadmap. Current state: `docs/ARCHITECTURE.md`.
> The body is kept unmodified as the port record.

## Scope

This roadmap tracks the port of Spektra film's grain surface from `upstream/spektrafilm-ofx` into the `.8bf` CPU/portable path. The shipping path is intentionally limited to the production-emulsion grain stage that fits the CPU tile renderer. GPU Boolean/FBG grain synthesis remains a feature roadmap, not a `v1` render path.

## Source reference

Primary references:

- `upstream/spektrafilm-ofx/src/SpektraParameters.h`
- `upstream/spektrafilm-ofx/src/SpektraFilmPlugin.cpp`
- `upstream/spektrafilm-ofx/src/SpektraTooltips.h`
- `spektra-core/third_party/darktable/spektra_core.c`
- `spektra-core/third_party/darktable/spektra_core.h`
- `spektra-core/sim_port/sp_chain.c`

OFX hue stock, paper format, density matrix, grain layer, and sub-layer parameters come from the `upstream/spektrafilm-ofx` parameter descriptor. The port uses the darktable `spektra` grain stage as the CPU implementation baseline.

## Feature matrix

| Feature | OFX/source parameter | Host field | Core field | Status | Notes |
|---|---|---|---|---|---|
| Grain on/off | `grainEnabled` | `grain_on` | `sp_chain_params_t.grain_on` | Implemented | Production path available. |
| Production amount | `grainAmount` | `grain_amount` / `amount` | `c->grain_amount` | Implemented | Direct scale on developed density grain. |
| Grain model | `grainModel` | `grain_model` | `grain_model` | Implemented (Preview/Production) | Synthesis is not offered; it is a red stub. Preview is a coarsest-layer approximation (the engine exports no total-curve single-layer raw sampler). |
| Grain saturation | `grainSaturation` | `grain_saturation` | `c->grain_saturation` | Implemented | Interpolates luminance and color-equivalent grain. |
| Sublayers enabled | `grainSublayersEnabled` | `grain_sublayers_enabled` | `grain_sublayers_enabled` | Implemented | Off renders a single coarsest layer. |
| Sublayer count | `grainSubLayerCount` | `grain_sublayer_count` | `grain_sublayer_count` | Implemented | Clamps *down* only; the stock fits curves for its own layer count. |
| Particle area scale | `grainParticleAreaUm2` | `grain_granularity` | `c->params.grain_rms_scale` | Implemented | Exposed as "Granularity": scales the stock's rms particle count. |
| Particle scale R/G/B | `grainParticleScaleR/G/B` | `grain_particle_scale[3]` | `grain_particle_scale[3]` | Implemented | Relative: 1.0 = the stock's own figure. |
| Particle scale layer 0/1/2 | `grainParticleScaleLayer0...2` | `grain_particle_scale_layer[3]` | `grain_particle_scale_layer[3]` | Implemented | Relative: 1.0 = the stock's own figure. |
| Density minimum R/G/B | `grainDensityMinR/G/B` | `grain_density_min_rgb[3]` | `grain_density_min_rgb[3]` | Implemented | Relative multiplier on the stock's per-channel floor. |
| Uniformity R/G/B | `grainUniformityR/G/B` | `grain_uniformity_rgb[3]` | `grain_uniformity_rgb[3]` | Implemented | Relative multiplier on the stock's per-channel uniformity. |
| Clump blur (absolute) | `grainFinalBlurUm` | `grain_final_blur_um` | `grain_final_blur_um` | Implemented | `> 0` = absolute um; `< 0` = stock-scaled clump blur. |
| Dye cloud blur (absolute) | `grainBlurDyeCloudsUm` | `grain_dye_cloud_um` | `grain_dye_cloud_um` | Implemented | `> 0` = absolute um; `< 0` = stock-scaled dye-cloud blur (scalar `grain_dye_cloud` still scales it). |
| Micro structure scale | `grainMicroStructureScale` | `grain_micro_structure_scale` | `c->grain_micro_structure_scale` | Implemented | Multiplier on the dye-cloud blur (1.0 = stock). Deliberately *not* the OFX cell-size meaning — see §*Format scaling review*. |
| Micro structure sigma | `grainMicroStructureSigmaNm` | `grain_micro_structure_sigma_nm` | stored only | Red stub (by decision) | The OFX field is dormant at its own defaults (see §*Format scaling review*); left stored-only on purpose. |
| Grain seed | `grainSeed` | `grain_seed` | `grain_seed` | Implemented | 0 keeps the stock pattern. |
| Grain animation | `grainAnimate` | absent | unsupported | Red stub | Not part of still-frame mode. |
| Simple Boolean grain synthesis | `grainSynthesis*` | all absent | unsupported | Red stub | GPU/shader Monte-Carlo path; CPU route not ported. |
| Layered synthesis | `grainSynthesisLayered` | absent | unsupported | Red stub | Requires synthesis engine first. |

## Current implementation model

The `.8bf` uses the production production-grain implementation:

1. density / layer structure from the film sim pack;
2. per-sub-layer particle geometry;
3. deterministic clumping blur;
4. dye-cloud blur; and
5. final density correction using the multiplicative unsharp mask.

The OFX grain synthesis model is not emulated in this CPU engine. It may require a separate tiled CPU Monte-Carlo path or deliberate out-of-scope status.

## Format scaling review (2026-10-16) — reviewed, kept as-is

A port-wide review of grain scaling across film gauges was carried out and the
outcome is **no code change**: the current scaling is accepted as-is. The findings
are recorded here so the reasoning is not re-derived later.

**How the grain scale is derived.** `pixel_um = format_mm · 1000 / long_edge_px`
(`sp_pixel_um_for_format`) drives the physical grain size, and the clump/format
dial is `sp_grain_format_scale(mm) = (mm/35)^0.62`. The gauge table
(`sp_chain.c` `SP_FILM_FORMATS`) has `35mm = 35.0` (the still full-frame gauge)
and `Super 35 = 24.89` (the cine aperture); real cine 35 mm is ≈ 21.9 mm.

**What was measured.** On flat grey at 1024 and 3072 px:
- Super 35 renders ≈ **2× the grain RMS** of 35 mm (expected from the smaller
  gauge → smaller `pixel_um`);
- 16 mm / Super 16 grain is **uncorrelated pixel speckle** (lag-1 autocorrelation
  ≈ −0.07 / 0.01), whereas 35 mm and above is **clumped** (ac1 ≈ 0.44–0.82).

**Root cause (for the record).** The clump / final blur is applied in *fixed
pixels* rather than physical µm: `SP_GRAIN_BLUR_BASE = 0.89` scaled by
`(mm/35)^0.62` (`sp_chain.c`), whereas the OFX path uses
`grainFinalBlurUm = 7.17 µm → / pixelSizeUm`. So the blur shrinks — rather than
stays physically constant — as the gauge gets smaller, which is what makes the
small gauges break into pixel speckle.

**Decision.** Left unchanged. Three candidate fixes were considered and all were
declined as unnecessary for the shipping surface:

1. **Physical clump blur for gauge selections** (gate `Custom` so the default
   render stays byte-identical) — declined.
2. **Format-table fix** (cine 35 mm ≈ 21.9 mm, or split still-vs-cine) —
   declined.
3. **Implement the `grainMicroStructure` stub** — declined, and separately
   unnecessary: the OFX field is gated on
   `sigmaNm·0.001 / pixel_um > 0.05` i.e. `sigmaNm > 50·pixel_um` (nm). At its
   own default `sigmaNm = 30` this needs `pixel_um < 0.6 µm` (a 35 mm frame
   longer than ≈58,000 px), so it is dormant at the default and every realistic
   scan resolution. NOTE: `grainMicroStructureScale`'s OFX meaning is the field
   *cell pitch in µm*, not a strength; the port keeps it as a dye-cloud blur
   multiplier (1.0 = stock) and this is intentional.

No grain parameter, table entry, or stage was modified by this review.

## Roadmap

### Phase 0 - Free-file production fidelity, no behavior drift

- Keep all new grain parameters no-op at their stock defaults.
- Add one test per newly parsed grain option with:
  - default parity to the unchanged reference render; and
  - deliberate non-default mutation changing output.
- Do not alter engine defaults without a reference oracle.

### Phase 1 - Exact per-channel and per-layer scalar parity

- Split `grainDensityMin` into R/G/B rather than a single scalar.
- Split `grainUniformity` into R/G/B.
- Split `grainParticleScale` into R/G/B.
- Split `grainParticleScaleLayer0..2`.
- Add `grainSubLayerCount` rather than collapsing it.
- Add `grainFinalBlurUm` independently of the stock_scaled blur.
- Add `grainBlurDyeCloudsUm` control already present, but verify it is actually active.
- Add `directional/granular` tests for each of the fields.

Exit criteria:

- Every darktable production grain parameter present in host struct maps one-for-one to an OFX parameter, or is explicitly classified as unsupported.
- No changes at defaults.

**Status: implemented.** The per-channel/per-layer overrides are applied in `sp_grain_stage` (`sp_chain.c`) from the `sf_grain_layers_t` copy (`gunif[]`, `gdmin[]`, `layers.layer_dmin[][]`, `layers.n`, `layer_npart[][]`), with the copy of `sp_chain_params_t` scaled at `sp_chain_create`. Semantics are *relative* (1.0 = the stock's own figure), which is what keeps the default build byte-identical to the pack's own resolution. The absolute `grain_final_blur_um` / `grain_dye_cloud_um` overrides replace the stock-scaled values when positive.

### Phase 2 - Grain model selector

- Add stock versus user-selected grain model choice.
- Preserve Preview and Production output paths.
- Wire `grainModel`.
- If Synthesis is selected:
  - implement a CPU simplified Boolean-field approximation outside GPU path, or keep disabled.
- Decide whether Synthesis remains red-stub only, since it may be performance-magnitude CPU.

**Status: implemented for Preview and Production.** `grain_model` selects Production (layered dye-cloud) or Preview. Preview is a coarsest-layer approximation (`nsub = 1`) because the CPU port exposes no total-curve single-layer raw sampler; it is documented as an approximation. Grain Synthesis remains a red stub (see Phase 3).

### Phase 3 - Synthetic grain option (deferred / possibly unsupported)

- Implement in a separate tiled job if approved.
- Add controls for all `grainSynthesis*` fields.
- Quantize/predictable grain parity only if golden comparison is available.
- Keep CPU memory/time bounds explicit.

**Status: deferred (remains a red stub).** The shader synthesis path is ~1000 lines of GPU Monte-Carlo with no CPU reference, so a faithful port is not practical within this project's constraints. All controls are listed in the grain red-stub label.

### Phase 4 - Sublayers and anisotropy

- Full layer-separated clumping.
- Multi-layer mixing.
- per-layer particle geometry and depth scaling.
- Seed derivative per layer.
- Repeatability tests.

**Status: implemented.** `grain_sublayers_enabled` selects the layered versus single-layer model; `grain_sublayer_count` clamps the stock's layer count; `grain_particle_scale_layer[3]` scales per-layer particle geometry; `grain_density_min_rgb[3]` scales the per-channel/per-layer density floor. Seed derivatives stay deterministic per layer (no CPU RNG state), so renders are repeatable.

### Phase 5 - Integration with film format and scan glare

- Ensure film gauge governs grain particle scale.
- Ensure pixel pitch is used where source calls for it.
- Validate scanner glare, unsharp and optical blur still compose cleanly with grain at large and small pixels.

**Status: implemented.** `pixel_um` (from the film gauge/format) drives the particle-count scaling and the absolute-um overrides; the grain stage composes after the developed density and before scanner glare/unsharp, and default renders stay byte-identical to `tools/refrender`.

## Verification

- `make -C hosts/ps8bf/test`
- `./hosts/ps8bf/test/build/harness selftest`
- `make -C spektra-core build/refrender`
- `python3 tools/parity/parity.py`
- Default render byte identity; every added stage visibly changes output when toggled.
- Seed repeatability.
- Layer-count and granularity mutations cause expected output-size appropriate changes.
