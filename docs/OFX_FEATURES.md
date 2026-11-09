# spektrafilm OFX — user-facing parameter surface

Extracted from `upstream/spektrafilm-ofx/src/SpektraFilmPlugin.cpp`
(`describeInContext`) + the `kParamMetadata` visibility table. This is exactly what
a user sees in DaVinci Resolve (and any OFX host).

> **Note (2026-10-15):** this document covers the source snapshot in
> `upstream/spektrafilm-ofx` (OFX **v0.2.1**). The shipping runtime bundles in `docs/`
> (OFX **v0.3.12**, Vulkan) add a whole GPU-only **Effects** family — lens
> distortion, vignette, bokeh, coma, chromatic aberration, film gate, gate weave,
> flicker/breathing, timing shift, film damage, telecine. None of it exists in the
> reference source or the CPU engine; see `docs/EFFECTS_FAMILY_FINDINGS.md`.

## The three installable builds (flavors)

The repo ships three .ofx.bundle variants; each shows a *different* subset of the
~155 controls defined in the descriptor:

| Flavor | Install name   | Shows                                              |
|--------|----------------|----------------------------------------------------|
| Flow   | `spektrafilm flow` | **F** tier only — the everyday set              |
| Pro    | `spektrafilm`  | **F + P** — the flagship build                   |
| Dev    | `spektrafilm dev` | everything (F + P + **D**)                       |

Tier legend:

- **F** = visible in all builds (Flow tier)
- **P** = added by the flagship "Pro" build
- **D** = only in the "dev" build (FilmDev controls)

Also gated at compile time: `kSpektraAcademyPrinterDensityEnabled=true` currently,
so the *Printer Density* print-timing mode and *Printer Lights* group are active.
The "Manage" group (LUT export / presets / bookkeeping) is visible in every build.

darktable column: **✓** implemented in darktable's spektrafilm module,
**~** partial / different representation, **✗** absent.

---

## Color Management

| Control (param) | Type | Range / Options (default) | Tier | darktable |
|---|---|---|---|---|
| Mode (`process`) | choice | Print simulation · Scan negative · Process negative | F | ~ |
| Invert Negative Scan (`scanNegativeInvert`) | bool | off | F | ~ |
| Input Color Space (`inputColorSpace`) | choice | 26 spaces (ARRI LogC4, LogC3, BMDFilm Gen5, DaVinci Intermediate, RED, Sony S-Log3 ×2, Canon Log2/Log3, Panasonic V-Log, ACES2065-1/cg/cct/cc, Linear 2020/709, Linear P3, sRGB, Display P3, ProPhoto, Adobe RGB, DCI-P3, P3 g2.2/g2.6, Rec709 g2.2/g2.4) | F | ✗ |
| Input Color Space (`rcmInputColorSpace`) | choice | 16 RCM spaces (DaVinci Intermediate + …) | F | ✗ |
| Output Role (`outputRole`) | choice | Display Out SDR · Display Out HDR · RCM/ACES (Beta) | F | ✗ |
| Output Color Space (`sdrOutputColorSpace`) | choice | 9 SDR spaces (sRGB, Display P3, ProPhoto, Adobe RGB, DCI-P3, P3 g2.2/g2.6, Rec709 g2.2/g2.4) | F | ✗ |
| Output Color Space (`sceneOutputColorSpace`) | choice | DaVinci Intermediate WideGamut | D | ✗ |
| HDR Preset (`hdrPreset`) | choice | PQ 1000 · PQ 4000 · HLG 1000 · Custom | F | ✗ |
| HDR Transfer (`hdrTransfer`) | choice | Rec.2100 ST2084 (PQ) · Rec.2100 HLG | F | ✗ |
| Reference White Nits (`hdrReferenceWhiteNits`) | double | 48–1000 (203) | F | ✗ |
| Peak Nits (`hdrPeakNits`) | double | 100–10000 (1000) | F | ✗ |
| HDR Exposure EV (`hdrExposureEv`) | double | −8..+8 (0) | F | ✗ |
| HDR Tone Mapping (`hdrToneMapping`) | choice | Soft Rolloff · Hard Clip | F | ✗ |
| Color Adaptation (`colorAdaptation`) | bool | off | F | ~ |
| Input Compression (`colorAdaptationInputCompression`) | bool | on | P | ~ |
| Curve Smoothing (`colorAdaptationCurveSmoothing`) | bool | on | P | ~ |
| Output Lightness Compression | bool | on | P | ~ |
| Output Chroma Compression | bool | on | P | ~ |

## Filtering

| Control | Type | Range / Options (default) | Tier | darktable |
|---|---|---|---|---|
| Filter UV (`cameraUvFilterEnabled`) | bool | off | P | ✗ |
| UV Cut nm (`cameraUvCutNm`) | double | 380–450 (410) | P | ✗ |
| Filter IR (`cameraIrFilterEnabled`) | bool | off | P | ✗ |
| IR Cut nm (`cameraIrCutNm`) | double | 600–780 (675) | P | ✗ |

## Film

| Control | Type | Range / Options (default) | Tier | darktable |
|---|---|---|---|---|
| RGB to Raw (`rgbToRawMethod`) | choice | Hanatos 2026 · Hanatos 2025 · Mallett 2019 (**default: 2026**) | F | ~ |
| Stock (`film`) | choice | dynamic: ~dozens of filmstocks from data pack | F | ✓ |
| Film Format (`filmFormat`) | choice | 8mm · Super 8 · 16mm · Super 16 · 35mm · Super 35 · 65mm · 70mm/IMAX (35) | F | ✓ |
| Push / Pull Mode (`filmPushPullMode`) | choice | Standard · Experimental | F | ✗ |
| Film Push / Pull Stops (`filmPushPullStops`) | double | −2..+2 (0) | F | ✓ |
| Negative Bleach Bypass (`negativeBleachBypassAmount`) | double | 0–1 (0) | D | ✗ |
| Leuco-Cyan Coupling (`negativeLeucoCyanCoupling`) | double | 0–2 (1) | D | ✗ |
| Exposure EV (`filmExposureEv`) | double | −8..+8 (0) | F | ✓ |
| Auto Exposure (`autoExposure`) | bool | off | P | ~ |
| Auto Exposure Meter (`autoExposureMethod`) | choice | Center weighted · Median | P | ✗ |
| Gamma (`filmGamma`) | double | 0.1–2 (1) | D | ✓ |

## Print

| Control | Type | Range / Options (default) | Tier | darktable |
|---|---|---|---|---|
| Paper (`paper`) | choice | dynamic: paper stocks from data pack | F | ✓ |
| Print Timing (`printTiming`) | choice | Filtered Enlarger · Printer Density | F | ~ |
| Print Push / Pull Stops (`printPushPullStops`) | double | −2..+2 (0) | F | ✗ |
| Print Bleach Bypass (`printBleachBypassAmount`) | double | 0–1 (0) | D | ✗ |
| Exposure EV (`printExposureEv`) | double | −5..+5 (0) | F | ✓ |
| Gamma (`printGamma`) | double | 0.1–2 (1) | D | ✓ |
| Shadow Shape (`printShadowShape`) | double | −1..+1 (0) | F | ✗ |
| Highlight Shape (`printHighlightShape`) | double | −1..+1 (0) | F | ✗ |
| C Filter (`filterC`) | double | 0–120 (0) | F | ~ |
| M Filter Shift (`filterMShift`) | double | −60..+60 (0) | F | ✓ |
| Y Filter Shift (`filterYShift`) | double | −60..+60 (0) | F | ✓ |
| Preflash Exposure (`preflashExposure`) | double | 0–1 (0) | F | ✓ |
| Preflash M Filter Shift | double | −60..+60 (0) | F | ✓ |
| Preflash Y Filter Shift | double | −60..+60 (0) | F | ✓ |
| Gang Printer Points (`printerLightsGang`) | bool | off | F | ✗ |
| Group Printer Points (`printerLightsGroup`) | bool | off | F | ✗ |
| Printer Point R (`printerLightR`) | double | −24..+24 (0) | F | ✗ |
| Printer Point G (`printerLightG`) | double | −24..+24 (0) | F | ✗ |
| Printer Point B (`printerLightB`) | double | −24..+24 (0) | F | ✗ |
| Printer Point Calibration | bool | on | P | ✗ |

## Film Plane (enlarger)

| Control | Type | Range / Options (default) | Tier | darktable |
|---|---|---|---|---|
| Scale (`enlargerScale`) | double | 1–32 (1) | P | ✗ |
| Offset X % (`enlargerOffsetXPercent`) | double | −100..+100 (0) | P | ✗ |
| Offset Y % (`enlargerOffsetYPercent`) | double | −100..+100 (0) | P | ✗ |

## DIR Couplers

| Control | Type | Range / Options (default) | Tier | darktable |
|---|---|---|---|---|
| Amount (`dirAmount`) | double | 0–2 (0) | F | ✓ |
| Diffusion um (`dirDiffusionUm`) | double | 0–100 (20) | F | ✓ |
| Tail um (`dirDiffusionTailUm`) | double | 0–1000 (200) | P | ✓ |
| Tail Weight (`dirDiffusionTailWeight`) | double | 0–1 (0.06) | P | ✓ |
| Same-Layer Inhibition (`dirInhibitionSameLayer`) | double | 0–2 (1) | F | ✓ |
| Interlayer Inhibition (`dirInhibitionInterlayer`) | double | 0–2 (1) | F | ✓ |
| Same-Layer Gamma RGB (`dirGammaSameLayerRgb`) | 3D | 0–1 (0.336, 0.319, 0.273) | P | ✗ |
| R → G/B Gamma (`dirGammaRToGb`) | 2D | 0–1 (0.353, 0.302) | P | ✗ |
| G → R/B Gamma (`dirGammaGToRb`) | 2D | 0–1 (0.154, 0.353) | P | ✗ |
| B → R/G Gamma (`dirGammaBToRg`) | 2D | 0–1 (0.168, 0.226) | P | ✗ |
| Calibrate to Stock (`dirCalibrateToStock`) | button | — | P | ~ |

## Grain

| Control | Type | Range / Options (default) | Tier | darktable |
|---|---|---|---|---|
| Enabled (`grainEnabled`) | bool | off | F | ✓ |
| Model (`grainModel`) | choice | Preview · Production · **Grain Synthesis** (Dev only) | F | ✗ |
| Amount (`grainAmount`) | double | 0–2 (1) | F | ✓ |
| Saturation (`grainSaturation`) | double | 0–1 (1) | F | ✗ |
| Particle Area um² (`grainParticleAreaUm2`) | double | 0.01–5 (0.1) | F | ~ |
| Sublayers (`grainSublayersEnabled`) | bool | on | P | ~ |
| Sub Layer Count (`grainSubLayerCount`) | int | 1–8 (1) | P | ~ |
| Particle Scale RGB (`grainParticleScale`) | 3D | (1.2, 1.0, 2.5) | P | ~ |
| Layer Scale (`grainParticleScaleLayers`) | 3D | (6.0, 1.0, 0.4) | P | ~ |
| Density Min (`grainDensityMin`) | 3D | (0.04, 0.05, 0.06) | P | ✓ |
| Uniformity RGB (`grainUniformity`) | 3D | (0.99, 0.97, 0.98) | P | ✓ |
| Final Grain Blur (`grainFinalBlurUm`) | double | 0–25 (7.17) | P | ✓ |
| Dye Cloud Blur um (`grainBlurDyeCloudsUm`) | double | 0–10 (1) | P | ✓ |
| Micro Structure (`grainMicroStructure`) | 2D | (0.2, 30.0) | P | ✗ |
| Seed (`grainSeed`) | int | 0–1,000,000 | P | ✗ |
| Animate (`grainAnimate`) | bool | on | P | ✗ |
| Synthesis Size (`grainSynthesisSize`) | double | 0.25–4 (1) | D | ✗ |
| Synthesis Amount (`grainSynthesisAmount`) | double | 0–3 (1) | D | ✗ |
| Synthesis Sharpness (`grainSynthesisSharpness`) | double | 0.25–4 (1) | D | ✗ |
| Synthesis Quality (`grainSynthesisQuality`) | double | 0.25–4 (1) | D | ✗ |

## Grain Synthesis (advanced)

| Control | Type | Range / Options (default) | Tier | darktable |
|---|---|---|---|---|
| Samples (`grainSynthesisSamples`) | int | 1–2048 (128) | D | ✗ |
| Mean Radius um (`grainSynthesisMeanRadiusUm`) | double | 0.05–10 (0.25) | D | ✗ |
| Radius StdDev Ratio | double | 0–1 (0) | D | ✗ |
| Observation Aperture Sigma um | double | 0–20 (1) | D | ✗ |
| Cell Size Ratio | double | 0.25–2 (1) | D | ✗ |
| Max Radius Quantile | double | 0.95–0.9999 (0.999) | D | ✗ |
| Coverage Epsilon | double | 1e-6–0.01 (1e-4) | D | ✗ |
| Max Grains Per Cell | int | 1–128 (32) | D | ✗ |
| Radius Scale RGB | 3D | (1.2, 1.0, 2.5) | D | ✗ |
| Layer Scale | 3D | (6.0, 1.0, 0.4) | D | ✗ |
| Layered | bool | on | D | ✗ |

## Halation

| Control | Type | Range / Options (default) | Tier | darktable |
|---|---|---|---|---|
| Enabled (`halationEnabled`) | bool | off | F | ✓ |
| Scatter Amount (`scatterAmount`) | double | 0–2 (1) | P | ✓ |
| Scatter Scale (`scatterScale`) | double | 0–4 (1) | P | ✓ |
| Amount (`halationAmount`) | double | 0–4 (1) | F | ✓ |
| Scale (`halationScale`) | double | 0–4 (1) | F | ✓ |
| Boost EV (`halationBoostEv`) | double | 0–20 (0) | F | ✓ |
| Boost Range (`halationBoostRange`) | double | 0–1 (0.3) | P | ✓ |
| Protect EV (`halationProtectEv`) | double | 0–10 (4) | P | ✓ |
| Strength RGB (`halationStrength`) | RGB | (0.05, 0.015, 0) | F | ~ |

## Diffusion

Each stage (camera + print) has the same 11 controls.

| Control | Type | Range / Options (default) | Tier | darktable |
|---|---|---|---|---|
| Camera Enabled / Print Enabled | bool | off | F | ✓ |
| Family | choice | Glimmerglass · Black Pro-Mist · Pro-Mist · CineBloom | F | ✓ |
| Strength | double | 0–2 (0.5) | F | ✓ |
| Spatial Scale | double | 0–4 (1.0) | P | ✓ |
| Halo Warmth | double | −1.5..+1.5 (0) | F | ✓ |
| Core Intensity | double | 0–4 (1) | P | ✗ |
| Core Size | double | 0.1–4 (1) | P | ✗ |
| Halo Intensity | double | 0–4 (1) | P | ✗ |
| Halo Size | double | 0.1–4 (1) | P | ✗ |
| Bloom Intensity | double | 0–4 (1) | P | ✗ |
| Bloom Size | double | 0.1–4 (1) | P | ✗ |

## Scanner

| Control | Type | Range / Options (default) | Tier | darktable |
|---|---|---|---|---|
| Enabled (`scannerEnabled`) | bool | off | F | ✓ |
| White Correction | bool | off | F | ✓ |
| Black Correction | bool | off | F | ✓ |
| White Level | double | 0–1 (0.98) | F | ✓ |
| Black Level | double | 0–1 (0.01) | F | ✓ |
| Glare Percent | double | 0–0.2 (0.03) | P | ✓ |
| Glare Roughness | double | 0–4 (0.7) | P | ✗ |
| Glare Blur | double | 0–32 (0.5) | P | ✗ |
| MTF50 lp/mm | double | 0–300 (60) | P | ~ |
| Unsharp Radius um | double | 0–100 (5) | P | ✓ |
| Unsharp Amount | double | 0–4 (0.7) | P | ✓ |

## Info (labels, all builds)

Version · Created by (Aedan Diez) · Based on work by (Andrea Volpato & Johannes Hanika)

## Manage (visible in all builds)

| Control | Type | Notes | Tier |
|---|---|---|---|
| GPU Render Tiling (`gpuRenderTiling`) | choice | Full-frame · Experimental tiled (Pro) | P |
| LUT Size | choice | 33 · 65 (65) | all |
| LUT Destination | choice | User · DaVinci Resolve · Nuke · Adobe Creative · Final Cut Pro | all |
| LUT Identifier | string | "spektrafilm" | all |
| Export LUT | button | exports the currently configured pipeline as a 3D LUT | all |
| Preset Name | string | "spektrafilm_preset" | all |
| Preset | choice | user presets on disk | all |
| Save Preset / Load Preset / Copy Params / Paste Params | buttons | param management | all |
| Set Defaults / Reset Factory Defaults | buttons | defaults persistence | all |
| Open User Manual | button | — | all |

---

## Summary numbers

- **~155 controls** total (~130 real sliders/toggles/choices + buttons/labels + 1 hidden bool).
- **Flow build** (everyday): ~79 controls.
- **Pro build** (flagship): +57 → ~136.
- **Dev build**: +19 (HDR scene output, bleach bypass ×2, leuco cyan, film/print gamma, grain synthesis) → all 155.

These flavour numbers describe the **OFX distribution as shipped** — they are *not* our
scope. Our v1 scope is locked in the appendix below.

### What darktable does NOT have (the parity gap, by tier)

> **Updated 2026-10-15.** This section describes the gap against the *darktable
> seed*, not the current `.8bf`. Since the `.8bf` engine now carries its own
> chemistry, scanner, DIR-gamma, UV/IR and film-format terms, most of the items
> below are **closed**; see the status columns in
> `docs/PORT_PARITY_ROADMAP.md` for the authoritative list.

- **F-tier but missing in darktable:** grain *Saturation*, grain *Model* selector, push/pull *Mode*, print *Shadow/Highlight Shape*, *C-filter*, *Printer Lights* (R/G/B, gang, grouping), *LUT Export* + the whole *Manage* group, *Input/Output color spaces*, *HDR output* controls, *Output Role*.
- **P-tier missing:** UV/IR filtering, enlarger scale/offset, DIR gamma couplings, seed/animate, micro structure, diffusion core/halo/bloom detail, glare roughness/blur.
- **D-tier missing:** everything (grain synthesis, bleach bypass, leuco cyan, film/print gamma, scene output).

---

## v1 scope — locked 2026-10-10

Use case: **still-photography development step** for raw/DNG. Develop wide-gamut in
Affinity (ProPhoto or Display P3, 16-bit) → apply this filter → convert to sRGB/Adobe RGB
at export, **in Affinity** (delivery is not the plugin's job; Display P3 is wide-gamut
SDR and Affinity handles it).

**Shipped:** all "✓/~" darktable-covered features plus the OFX-only film/print/grain/
halation/diffusion features marked "(new)" below. Out-of-scope OFX controls stay in the
descriptor but render **disabled** in the UI.

| Group | Status |
|---|---|
| Mode (Print/Scan/Process) + Invert | in |
| Input colour space | **auto-detect document profile + 26-space manual override** |
| Output colour space / roles / HDR | disabled (delivery is Affinity's job) |
| Film: stock, format, push/pull (Std), Exposure EV, Gamma | in |
| Film: RGB→raw | **in (new)** — engine runs **Hanatos 2026** (default) and the dialog selects **Hanatos 2025 / Mallett 2019** (`rgb_to_raw_method`; Hanatos 2025 needs `tools/merge_bandpass_hanatos2025.py`, Mallett is opt-in pending golden validation) |
| Film: auto exposure + meter | **in (new)** |
| Film: negative bleach bypass, leuco-cyan | **in (new)** |
| Print: paper, timing (enlarger), push/pull, Exposure EV, Gamma | in |
| Print: shadow/highlight shape, C filter | **in (new)** |
| Print: M/Y filtration, preflash + M/Y, print bleach bypass | in + **new print bleach** |
| DIR couplers: cores | in (darktable) |
| DIR couplers: per-channel gamma couplings, calibrate-to-stock | **in (new)** |
| Grain: **Preview + Production model** + full param group + **Seed (random default)** | in; Synthesis/Animate disabled |
| Halation: full incl. per-channel Strength RGB | in (strength RGB is **new**) |
| Diffusion: camera + print full group | in; core/halo/bloom detail is **new** |
| Scanner | engine in (free from darktable); UI hideable |
| Film plane (enlarger), LUT export, GPU tiling | disabled |
| Manage (presets, copy/paste, defaults persistence) | deferred to Phase 4 |
| Printer lights | **implemented** (R/G/B trim + gang + calibrate; APD spectral calibration pending the licensed ST 2065-2 table) |
| Effects family (lens distortion, vignette, bokeh, coma, chromatic aberration, film gate, gate weave, flicker/breathing, timing shift, film damage, telecine) | **out of scope** — exists only in the shipping OFX 0.3.12 Vulkan runtime (GPU-only, no engine path). See `docs/EFFECTS_FAMILY_FINDINGS.md` |

**Parity target:** OFX minus HDR/delivery surface, film plane, LUT export,
and GPU grain synthesis.