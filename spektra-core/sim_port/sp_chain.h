/* SPDX-License-Identifier: GPL-3.0-or-later */
/* sp_chain — the render pipeline the port hosts share (Phase 1 vertical slice).
 *
 * Owns pack + film/paper profiles + the built sf_sim, and runs the per-image
 * chain the way darktable's spektrafilm iop does, including the caller-side
 * spatial effects the engine intentionally leaves out:
 *
 *   decode(transfer) -> expose -> boost -> camera diffusion -> halation ->
 *   lograw -> develop_corr -> [DIR coupler diffusion blur] -> develop ->
 *   [grain] -> (print_expose -> [print diffusion] -> print_develop) -> scan ->
 *   [scanner blur/USM/glare] -> encode(transfer)
 *
 * All the bracketed spatial stages are off at their defaults (the Phase-1
 * slice), so a caller that does not set them renders byte-for-byte as before.
 * The DIR coupler blur reproduces darktable's spektrafilm.c exactly via the
 * engine's own sf_blur_plane3_wide(): a core Gaussian of sigma
 * size_um/pixel_um, combined with the exponential-tail mixture (SF_EXPTAIL_*
 * constants) at tail_um/pixel_um when the pack carries a tail. Deterministic
 * and bit-identical to the native reference's spatial stage.
 *
 * Compiled for Linux (with real glib/json via the shims) AND for Windows
 * (MSVC, same shims, embedded asset pack, no CRT DLLs).
 */
#pragma once

#include <stddef.h>

#include "spektra_sim.h"
#include "sp_color.h" /* sp_input_transfer_t + the input primaries/curve tables */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sp_chain_t sp_chain_t;

/* How the host's samples map to scene-linear light. The engine stages always
 * run on linear light; sp_chain_render applies this at its boundary (decode on
 * the way in, encode on the way out), so a host that carries gamma-encoded
 * samples — 8/16-bit sRGB, the common case — feeds the engine correctly, and a
 * host that already works in linear light selects SP_TRANSFER_LINEAR.
 * The curve table lives in sp_color.h (sp_input_transfer_t). */

/* Diffusion-filter families; indices match the engine's SF_DIFF_FAMILIES[]. */
typedef enum
{
  SP_DIFF_GLIMMERGLASS = 0,
  SP_DIFF_BLACK_PRO_MIST = 1,
  SP_DIFF_PRO_MIST = 2,
  SP_DIFF_CINEBLOOM = 3
} sp_diffusion_family_t;

/* Film-format presets (the OFX FilmFormat surface). Index 0 is "Custom": the
 * caller supplies the film pixel size (pixel_um) directly. Indices 1..8 mirror
 * the OFX FilmFormat enum, using its own mm value, so selecting a format
 * derives the pixel size from the format's physical frame long edge and the
 * image's long edge ("35mm (stills)" is the 36 x 24 stills frame; the mm
 * value stays the OFX 35.0 so the render matches the reference). */
int sp_film_format_count(void);
const char *sp_film_format_label(int index);
double sp_film_format_mm(int index); /* 0 for "Custom", else the OFX gauge */
/* Film pixel size (um) for an image whose long edge is `long_edge_px` pixels,
 * shot on a `format_mm`-wide gauge: mm * 1000 / long_edge_px. Returns 0 when
 * either argument is non-positive (caller falls back to its manual pixel_um). */
double sp_pixel_um_for_format(double format_mm, double long_edge_px);
/* The OFX grainFinalBlurFormatScale: pow(format_mm / 35, 0.62), the extra
 * grain-blur scale the OFX applies on top of the pixel size. */
double sp_grain_format_scale(double format_mm);

/* ---- spatial-stage ceilings ---------------------------------------------
 * Halation is the only spatial stage whose radius a caller can move past the
 * stock's own value, so it is the only one that needs a ceiling: scatter's core
 * and tail are pinned inside the engine (2.2 / 9.7 um) and the DIR coupler blur
 * runs through sf_blur_plane3_wide(), whose cost is O(pixels) at any sigma.
 * Halation runs the plain recursive cascade instead, whose cost grows as
 * (sigma_px / SF_GAUSS_MAX_IIR_SIGMA)^2 passes, so this ceiling is what bounds
 * the stage's run time.
 *
 * It is the stock first-bounce radius the ceiling falls back to while
 * halation_sigma_max_um carries its < 0 sentinel. Public because three layers
 * have to agree on one number: the chain clamps the radii to it,
 * sf_params_validate() rejects a radius above it, and the GUI caps its sliders
 * at it. */
#define SP_HALATION_FIRST_SIGMA_UM 65.0

/* input_space: index into the input gamuts (sp_color.h; 0 = sRGB, the
 * default). The engine adapts the space's white point to the table illuminant.
 * The output is space-preserving: it is encoded in the same primaries + white
 * point as the input, so a host filter returns pixels in the document's native
 * colour space (and a wide-gamut document keeps its gamut).
 * input_transfer selects how those samples map to linear (sp_color.h). */
typedef struct sp_chain_params_t
{
  const char *pack_dir;   /* asset namespace ("pack" in the plugin; a real dir on Linux) */
  const char *film;       /* profile stem, e.g. "kodak_portra_400" */
  const char *paper;      /* profile stem, e.g. "kodak_2383"; unused when scan_film */
  int scan_film;          /* 1 = develop straight to scan (no print) */
  int process_mode;       /* print 0 / scan 1 / process 2 -negative */
  int scan_negative_invert; /* 1 = invert the scanned output */
  int print_timing;        /* 0 FilteredEnlarger, 1 ApdPrinterDensity */
  int printer_lights_gang;
  int printer_light_calibration;
  float printer_lights_r, printer_lights_g, printer_lights_b;
  int camera_uv_filter_enabled;
  float camera_uv_cut_nm;
  int camera_ir_filter_enabled;
  float camera_ir_cut_nm;
  float development_min;  /* B&W development family pick, minutes; <=0 = auto */
  double exposure_ev;     /* exposure_comp_ev */
  int auto_exposure;         /* auto-exposure enable */
  int auto_exposure_method;  /* 0 centre weighted, 1 median */
  double auto_exposure_ev;   /* computed correction applied at filter start */
  double print_exposure;
  int print_auto_exposure;      /* 1 = re-normalise mid-grey; 0 (default) = uncompensated */
  /* Film/print curve chemistry (OFX filmPushPullStops / filmGamma / printGamma).
   * All three are no-ops at their defaults, so a caller that leaves them alone
   * still renders the Phase-1 slice byte-for-byte:
   *   film_push_pull_stops  Standard push/pull: shifts the film exposure by the
   *                         stop count and the film density-curve slope by
   *                         1.15^stops (darktable's Standard model);
   *   film_gamma            scales the film density-curve slope (1 = stock);
   *   print_gamma           scales the print density-curve slope (1 = paper). */
  double film_push_pull_stops; /* 0 */
  int film_push_pull_mode;      /* 0=Standard, 1=Experimental */
  double negative_bleach_bypass_amount; /* 0..1 */
  double negative_leuco_cyan_coupling;  /* 0..2, default 1 */
  double print_bleach_bypass_amount;   /* 0..1 */
  double film_gamma;           /* 1 = the stock profile's own */
  double print_gamma;          /* 1 = the paper profile's own */
  double print_push_pull_stops;/* 0 = no print push/pull (OFX: gamma x 2^(st*0.25)) */
  double print_shadow_shape;   /* 0 = off; OFX printShadowShape (-1..1) */
  double print_highlight_shape;/* 0 = off; OFX printHighlightShape (-1..1) */
  int lut_steps;          /* 0 = exact spectral; >=2 = runtime 3D tables */
  int couplers_active;
  double coupler_diffusion_um; /* corr blur core sigma on film, um; <0 = pack */
  int input_transfer;     /* sp_input_transfer_t: host sample -> linear */
  /* DIR coupler matrix controls (the spatial diffusion above is the caller's
   * blur). Defaults 1.0 match the engine, so a params struct that leaves them
   * alone renders exactly as before. */
  double coupler_amount;               /* engine DIR coupler strength */
  double coupler_inhibition_samelayer; /* 1 = engine default */
  double coupler_inhibition_interlayer;/* 1 = engine default */
  double pixel_um;        /* image pixel size on film, um (blur scale) */
  /* Film format (OFX filmFormat). film_format_mm > 0 selects a gauge and
   * derives pixel_um from image_long_edge_px, overriding pixel_um; 0.0 keeps the
   * manual pixel_um, so the default renders the Phase-1 slice byte-for-byte.
   * image_long_edge_px is supplied by the caller (not persisted): the dialog
   * uses its full source dims, refrender the input PPM. */
  double film_format_mm;     /* 0 = manual pixel_um */
  double image_long_edge_px; /* full image long edge; 0 = unknown */
  int input_space;        /* input primaries index (sp_color.h; 0 = sRGB) */
  int input_custom_xyz_valid; /* derived from document ICC profile */
  double input_custom_rgb_to_xyz[9];
  double input_custom_white_xy[2];
  /* RGB -> film raw spectral upsampling method (sf_rgb_to_raw_method_t:
   * 0 Hanatos2025, 1 Mallett2019, 2 Hanatos2026). 2 is the reference default
   * and this engine's historical behaviour, so leaving it unset renders
   * byte-identically. */
  int rgb_to_raw_method;

  /* --- Phase 2 spatial look stages ---------------------------------------
   * Every stage below is a no-op at its default (see sp_params_defaults), so a
   * caller that leaves these alone renders exactly the Phase-1 slice. The
   * dialog exposes them as the "Film Look" group. Order in the render follows
   * darktable's spektrafilm.c:
   *   boost -> camera diffusion -> halation/scatter  (pre-film, linear)
   *   ... develop ... grain (on the developed CMY density) ...
   *   ... print diffusion ... scan ...
   *   scanner blur -> scanner USM -> viewing glare. */
  float boost_ev;         /* highlight boost, stops; 0 = off */
  float boost_range;      /* stops above the protect threshold */
  float protect_ev;
  int diffusion_on;       /* camera diffusion (pre-film, linear) */
  int diffusion_family;   /* sp_diffusion_family_t */
  float diffusion_strength;
  float diffusion_scale;
  float diffusion_warmth;
  float diffusion_core_intensity;
  float diffusion_core_size;
  float diffusion_halo_intensity;
  float diffusion_halo_size;
  float diffusion_bloom_intensity;
  float diffusion_bloom_size;
  int halation_on;        /* in-emulsion scatter + back-reflection halation */
  float scatter_amount;
  float scatter_scale;
  float halation_amount;
  float halation_scale;
  int grain_on;           /* production grain, on the developed density */
  float grain_amount;
  float grain_saturation; /* 0 = neutral (luminance) noise, 1 = full colour */
  int grain_seed;         /* 0 = stock pattern */
  float grain_micro_structure_scale;
  float grain_micro_structure_sigma_nm;
  float grain_blur;
  float grain_granularity;    /* sim-build scale on the stock's rms figure */
  float grain_uniformity;     /* sim-build scale */
  float grain_sublayer_scale; /* sim-build scale on the finer sub-layers */
  float grain_density_min;    /* sim-build scale on the stock's floor */
  float grain_dye_cloud;
  float grain_usm_sigma;
  float grain_usm_amount;
  /* Grain surface (OFX GrainParams). Everything below is a no-op at its
   * default, so an unedited block renders exactly as before:
   *   grain_model               0 = preview (single-layer), 1 = production.
   *   grain_sublayers_enabled   0 forces the single-layer preview draw even in
   *                             production; 1 keeps the stock's sub-layer fit.
   *   grain_sublayer_count      0 = the stock's own sub-layer count, else clamp
   *                             to 1..SF_GRAIN_MAX_SUBLAYERS.
   *   grain_particle_scale[3]   per-channel particle-area scale; > 1 makes that
   *                             channel coarser (fewer, larger particles).
   *   grain_particle_scale_layer[3] per-sub-layer particle-area scale.
   *   grain_uniformity_rgb[3]   per-channel multiplier on the stock uniformity.
   *   grain_density_min_rgb[3]  per-channel multiplier on the stock floor.
   *   grain_final_blur_um       > 0 = absolute clump-blur sigma in um (overrides
   *                             the stock-scaled GrainParams.blur); < 0 = default.
   *   grain_dye_cloud_um        > 0 = absolute dye-cloud blur in um (overrides
   *                             the dye-cloud scale); < 0 = default. */
  int grain_model;
  int grain_sublayers_enabled;
  int grain_sublayer_count;
  float grain_particle_scale[3];
  float grain_particle_scale_layer[3];
  float grain_uniformity_rgb[3];
  float grain_density_min_rgb[3];
  float grain_final_blur_um;
  float grain_dye_cloud_um;
  int print_diffusion_on;     /* between print expose and develop */
  int print_diffusion_family;
  float print_diffusion_strength;
  float print_diffusion_scale;
  float print_diffusion_warmth;
  float print_diffusion_core_intensity;
  float print_diffusion_core_size;
  float print_diffusion_halo_intensity;
  float print_diffusion_halo_size;
  float print_diffusion_bloom_intensity;
  float print_diffusion_bloom_size;
  float scan_blur;            /* scanner lens blur, px (fixed radius) */
  float scan_usm_sigma;       /* scanner unsharp-mask radius, px */
  float scan_usm_amount;
  float glare_percent;        /* viewing glare veil; print mode only */
  float scan_glare_roughness; /* viewing glare texture roughness (0.7 default) */
  float scan_glare_blur;     /* viewing glare blur, px (0.5 default) */

  /* ---- Locked v1 scope: DIR chroma, print chemistry, halation colour -----
   * Every entry is a no-op at its default (see sp_params_defaults), so a caller
   * that leaves them alone still renders the Phase-1 slice byte-for-byte. */
  double dir_gamma_same[3];    /* same-layer R,G,B gamma; 0 = the stock's own */
  double dir_gamma_r_gb[2];    /* R -> G,B; 0 = the stock's own */
  double dir_gamma_g_rb[2];    /* G -> R,B; 0 = the stock's own */
  double dir_gamma_b_rg[2];    /* B -> R,G; 0 = the stock's own */
  double filter_m_shift;       /* print magenta trim, CC units (0 = neutral) */
  double filter_y_shift;       /* print yellow trim, CC units (0 = neutral) */
  double filter_c_shift;       /* print cyan trim, CC units (0 = neutral) */
  double preflash_exposure;    /* enlarger preflash exposure (0 = off) */
  double preflash_m_shift;     /* magenta trim applied to the preflash only */
  double preflash_y_shift;     /* yellow trim applied to the preflash only */
  double halation_strength[3]; /* per-channel return weight; < 0 = stock's own */
  double halation_first_sigma_um[3]; /* per-channel first-bounce radius, um;
                                        < 0 = the stock's own */
  double halation_sigma_max_um;       /* independent upper clamp on the
                                         first-bounce radii above, um;
                                         < 0 = the stock ceiling (65 um) */
  int scan_black_correction;   /* 1 = normalise the black level (engine default) */
  int scan_white_correction;   /* 1 = normalise the white level (engine default) */
  double scan_black_level;     /* scanner input level mapped to black (0.01) */
  double scan_white_level;     /* scanner input level mapped to white (0.98) */
  double scan_mtf50_lpmm, scan_usm_radius_um;
} sp_chain_params_t;

/* Loads pack + profiles and builds the sim. errmsg is malloc'd on failure
 * (free it); NULL on success. */
sp_chain_t *sp_chain_create(const sp_chain_params_t *p, char **errmsg);

/* Frees pack references and the sim. */
void sp_chain_destroy(sp_chain_t *c);

/* Render the whole image. in/out are interleaved f32 row-major w*h*nch;
 * channels 0..2 are read/written. in can alias out (stages copy on their own
 * if needed — read carefully below: expose reads in, out is a separate
 * buffer, so aliasing is safe). Returns 0 on success, 1 on bad arguments, 2 on
 * allocation failure, or 3 when the progress hook (below) aborted the render. */
int sp_chain_render(sp_chain_t *c,
                    const float *in, float *out,
                    int w, int h, int nch);

/* ---- optional progress / abort ------------------------------------------
 * Called at pipeline-stage boundaries with a monotonically increasing `done`
 * out of `total` (0..100). Returning non-zero aborts the render, which then
 * returns 3 with no further writes. The Linux harness leaves it NULL. */
typedef int (*sp_progress_fn)(void *ctx, int done, int total);
void sp_chain_set_progress(sp_chain_t *c, sp_progress_fn fn, void *ctx);

/* ---- optional host parallelism ------------------------------------------
 * Every per-pixel stage of the chain (expose, lograw, develop, print, scan) is
 * element-wise: each pixel is computed from its own input, with no cross-pixel
 * dependency. The work can therefore be split into contiguous, disjoint pixel
 * ranges and run concurrently with a bit-identical result.
 *
 * When the engine is built without OpenMP (the MSVC .8bf build, which must not
 * import vcomp140.dll), a host installs `exec` here to run one range job over
 * all its threads. `fn(ctx, start, count)` must process pixels
 * [start, start + count). Install with sp_chain_set_parallel(); pass NULL to
 * restore strictly serial execution (the Linux default). */
typedef void (*sp_range_fn)(void *ctx, size_t start, size_t count);
typedef void (*sp_parallel_exec)(sp_range_fn fn, void *ctx, size_t npix);
void sp_chain_set_parallel(sp_parallel_exec exec);

/* The resolved engine params (coupler diffusion bytes, film dmax, etc.). */
const sf_sim_params_t *sp_chain_params(const sp_chain_t *c);

/* The DIR-coupler spatial stage, applied to an interleaved w*h*3 correction
 * field in place: corr = (1-w)*G(corr, core) + w*sum_i A_i*G(corr, R_i*tail),
 * with core = cdiff_um/pixel_um and tail = ctail_um/pixel_um. work/mix are
 * w*h*3 scratch; scratch is a w*h plane. Public (rather than static) so the
 * Linux harness can pin this stage directly (hosts/ps8bf/test/harness.c). */
void sp_coupler_diffuse(float *corr, float *work, float *mix, float *scratch,
                        int w, int h, float pixel_um,
                        double cdiff_um, double ctail_um, double ctail_w);

#ifdef __cplusplus
}
#endif