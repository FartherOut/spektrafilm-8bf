# Halation and scatter system roadmap

> **Historical roadmap - scope shipped.** The halation/scatter surface
> described here is implemented and released in v0.1.0; the Phase 5
> cross-cutting gaps it defers were closed by the later port work, except
> the APD printer-timing calibration (blocked on licensed spectral data) -
> current state in `hosts/ps8bf/README.md` §*Validation status*. The body is
> kept unmodified as the port record.

## Scope

Halation is the system that implements pre-film diffusion/scatter, near-emulsion scatter, and film-base back-reflection around bright content. The .8bf path uses `sf_halation` from `spektra-core/third_party/darktable/spektra_core.c`.

## Source and engine references

Primary references:

- `upstream/spektrafilm-ofx/src/SpektraParameters.h`
- `upstream/spektrafilm-ofx/src/SpektraTooltips.h`
- `spektra-core/third_party/darktable/spektra_core.c`
- `spektra-core/third_party/darktable/spektra_core.h`
- `spektra-core/sim_port/sp_chain.c`

Current darktable halation/shooter chain:

1. Highlight boost.
2. Camera diffusion (optional, separate stage).
3. Scatter (core Gaussian + three-Gaussian tail).
4. Multi-bounce halation (three bounces, weighted and spread).

## Feature matrix

| Feature | OFX/source parameter | Host field | Engine field | Status | Notes |
|---|---|---|---|---|---|
| Halation on/off | `halationEnabled` | `halation_on` | `c->halation_on` | Implemented | Gate stage. |
| Scatter amount | `scatterAmount` | `scatter_amount` | `c->scatter_amount` | Implemented | Weight for scatter. |
| Scatter scale | `scatterScale` | `scatter_scale` | `c->scatter_scale` | Implemented | Shared spatial multiplier. |
| Halation amount | `halationAmount` | `halation_amount` | `c->halation_amount` | Implemented | Multiplier for channel return weights. |
| Halation scale | `halationScale` | `halation_scale` | `c->halation_scale` | Implemented | Own spatial-scale multiplier. |
| Scatter power words override R/G/B | `halationStrength[0..2]` aliases | `halation_strength[0..2]` | `c->hal_strength_override` | Implemented | Sentinel `< 0` keeps pack stock. |
| First sigma | `halationFirstSigmaUmR/G/B` | `halation_first_sigma_um[3]` | `c->hal_sigma_override` | Implemented | Per-channel first-bounce radius, um. Sentinel `< 0` keeps pack stock; clamps to the upstream 65 um ceiling. |
| First-sigma ceiling | (Phase-2 advance) | `halation_sigma_max_um` | `c->hal_sigma_max_um` | Implemented | Independent per-channel upper clamp on the first-sigma radii, um. Sentinel `< 0` keeps the stock 65 um ceiling; raised to allow explicit larger radii. |
| Boost threshold | `halationBoostEv` | `boost_ev` | `c->boost_ev` | Implemented | Highlight bowing. |
| Boost range | `halationBoostRange` | `boost_range` | `c->boost_spread` | Implemented | Shape. |
| Protect EV | `halationProtectEv` | `protect_ev` | `c->protect_ev` | Implemented | Onset. |
| Highlight pass / self-consistency | pack scatter params | host not exposed | pack values | Implemented | Whole-image radii. |

## Detailed behavior

Stage 1 produces near-emulsion scatter:

- weighted core Gaussian;
- three-tail Gaussians with fixed amplitudes and radius/width ratios;
- per-channel core, tail, and tail-weight values from the pack;
- convex blend restricted to `[0, 1]`.

Stage 2 produces colored back-reflected halation:

- square alpha per channel, controlled by `a_tot = channelStrength * halation_amount`;
- bounces driven by a per-channel first sigma and an independent spatial multiplier;
- weights `1`, `0.5`, and `0.25`, normalized by their sum.

A linear image is expected. All strength fields remain identity-safe at defaults where possible, and protected values guard division and nonconvexity.

## Roadmap

### Phase 0 - Invariant baseline

- All new halation fields identity at defaults.
- Verify default render remains byte-idientical.
- Add per-option test that mutated field changes output.

### Phase 1 - Per-channel first sigma

- Extend `sf_halation` to accept a three-channel sigma instead of one scalar.
- Replace stock override model.
- Add host controls: `IDC_HALATION_SIG_R`, `IDC_HALATION_SIG_G`, `IDC_HALATION_SIG_B`.
- Give defaults of `-1` meaning stock.
- Add parse and validation.
- Test each channel mutates and preserves color bias.

**Status: implemented.** `sf_halation` now takes `const double halation_first_sigma_um[3]`, `sf_sim_t` stores `halation_sigma_um[3]` (the pack's `first_sigma_um[3]` is no longer collapsed to R), and `sf_sim_halation_params` exports all three channels. `sp_chain_params_t.halation_first_sigma_um[3]` carries absolute micrometres with the `< 0` = "the stock's own" sentinel (mirroring `halation_strength[3]`), is parsed from `halation_first_sigma_um_r/g/b`, and is clamped to 65 um per channel to match the upstream padding ceiling. The dialog exposes the three controls in the "Halation (Chroma)" group, the block is version 16, and validation rejects a positive radius above 65 um (code 40) and non-finite values (code 25). The per-channel bounce math is the same double expression the scalar used — `fmaxf((float)((sigma * hscl / pixel_um) * sqrt((double)k)), 1e-6f)` — evaluated per channel, so equal channels (every pack stock today) stay byte-identical. `test_halation_surface` pins the defaults, the no-op contract, each channel moving the render (blue needs a nonzero return weight to contribute), and the block→chain projection.

### Phase 2 - Advanced bounce controls

- Add optional bounce-count / decay curve. *(deferred — source has no such control; left as engine constants pending a source change)*
- Add independent first-sigma clamp and cap-independent behavior. **Implemented**: `halation_sigma_max_um` (host block v17 + chain), default `-1` = stock 65 um ceiling; raised value lifts the clamp.
- Add UI for halation weights and profiles. **Implemented**: existing weight controls plus the `IDC_HALATION_SIG_MAX` ceiling field, the `IDC_HALATION_PROFILE` combo (`Stock / Off / Still / Cine / Double-X / Strong remjet`) that fills the weight and radius fields, and matching tooltips.
- Compare against feature pack golden profiles. **Partially**: pack-preset regression pins the three pack families; the OFX golden diff still depends on the Windows GPU capture set.

### Phase 3 - Verification and numerical stability

- Larger highlight selection tests.
- Color-channel symmetry test when R/G/B sigma equal.
- NaN/infinite input guard.
- Clamp to maximum frame padding.
- Disable negative or undefined σ.
- Cache behavior and progress abort.
- Performance profiling for large images.

**Status: partially implemented.** Equal-channel byte-identity is enforced (the default-anchor render and the new no-op test both require it), non-finite radii are rejected at validation (code 25) and the per-channel radius is clamped to the upstream 65 um ceiling (code 40 / runtime clamp), the `< 0` sentinel is the only "undefined" value and is resolved to the pack stock, and the existing stage progress/abort still wraps the halation stage. Larger-highlight selection tests and large-image profiling remain open. No caching is added for the halation stage.

### Phase 4 - Documentation and parity

- Add parameter help strings.
- Add `tools/parity` stage cases.
- Ensure threaded output still byte-idientical to serial for all new parameters.
- Document validity of per-host FF compression and pixel pitch scaling.

**Status: partially implemented.** The three first-sigma controls have dialog tooltips; `tools/parity/parity.py` and `hosts/ps8bf/test/run.sh` confirm the default anchor and the threaded path stay byte-identical to `tools/refrender`/serial. Dedicated parity cases that drive a nonzero per-channel sigma (to compare against a golden image) are not yet added.

## Verification commands

```bash
make -C hosts/ps8bf/test
./hosts/ps8bf/test/build/harness selftest
make -C spektra-core build/refrender
python3 tools/parity/parity.py
```

Exit criteria:

- Default unchanged.
- Each added parameter visibly affects halation when nonzero.
- Any new per-channel sigma changes color balance in the expected direction.
- Threaded output remains identical.
- No regression in the default anchor image.

## Phase 5 — wider feature parity with upstream

Cross-cutting gaps (process modes, enlarger/printer lights, camera UV/IR
filters, input gamut coverage, RGB-to-raw selector, grain synthesis model,
output conversion) are inventoried in `docs/PORT_PARITY_ROADMAP.md`. They are
outside the halation/scatter scope but required for full source parity.
