/* SPDX-License-Identifier: GPL-3.0-or-later */
/* params.h — the .8bf's persisted parameter block.
 *
 * The block lives in the host's parameters Handle (FilterRecord::parameters)
 * so Photoshop/Affinity can remember settings between invocations and across
 * sessions. Its flat key set is the shared parameter surface (see
 * sp_params.c); the dialog edits it, and the filter loop turns it into an
 * sp_chain_params_t.
 *
 * Do not add fields to the middle of the struct: bump
 * SF_PARAMS_BLOCK_VERSION instead and keep the read path compatible with
 * older blocks (init defaults for missing fields — the vertical slice just
 * resets when the version differs).
 *
 * A field that is part of a *look* must also be added to the field table in
 * preset.c (and named in its header comment if it is not): presets are written
 * field-by-field, so one left out keeps the engine default when a preset is
 * loaded.
 */
#pragma once

#include <stdint.h>

#include "sp_chain.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Four-char tag: 'SFP1'. */
#define SF_PARAMS_BLOCK_MAGIC 0x31504653u
#define SF_PARAMS_BLOCK_VERSION 21

/* Highest halation ceiling the surface accepts, um. The GUI's "Sigma max um"
 * slider stops here and sf_params_validate() rejects anything above it, so the
 * two cannot drift apart: the ceiling is what bounds the halation stage's cost
 * (see SP_HALATION_FIRST_SIGMA_UM in sp_chain.h), and 1000 um is already far
 * past any film's own first-bounce radius. */
#define SF_HALATION_SIGMA_MAX_CAP_UM 1000.0

/* The spatial-stage radius multipliers (scatter, halation, camera and print
 * diffusion) all take this range: 0 turns the stage off, 1 is the film's own
 * scale, 4 is as wide as the surface goes. They are multipliers on a radius, so
 * an unbounded one is unbounded cost: halation's run time grows with the square
 * of the resulting sigma, and a pasted preset is not stopped by a slider. The
 * sliders stop at 4 and sf_params_validate() rejects anything outside this
 * range, so a hand-edited preset cannot ask for a blur the render cannot
 * finish. */
#define SF_BLUR_SCALE_MIN 0.0
#define SF_BLUR_SCALE_MAX 4.0

/* A blur wide enough that a preview of it stops feeling instant, in pixels.
 * The spatial stages run as a cascade of recursive Gaussian passes whose count
 * is (sigma_px / 150)^2, so cost per channel goes as the square of the width:
 * at 1200 px that is 64 passes, and halation runs three bounces of it per
 * channel while diffusion runs one per bank component. On a pane-sized preview
 * that is the point where the dialog pauses long enough to look stuck rather
 * than busy -- which is why the settings pane warns about it before you ask
 * for the update, rather than reporting a duration during it (the render runs
 * inside the frame, so nothing repaints while it does).
 *
 * Deliberately above anything a real look reaches: the widest radius any stage
 * physically models is cinebloom's bloom at 4,330 um, which is 1,083 px at a
 * 4 um pitch and so stays quiet at a normal one. */
#define SF_PREVIEW_SLOW_SIGMA_PX 1200.0

typedef struct sf_params_block
{
  uint32_t magic;           /* SF_PARAMS_BLOCK_MAGIC */
  int32_t version;          /* SF_PARAMS_BLOCK_VERSION */

  int16_t film_index;       /* index into SF_FILMS[] (pack_list.h) */
  int16_t paper_index;      /* index into SF_PAPERS[] */
  int32_t scan_film;        /* 0 negative process, 1 scan (develop only) */
  int32_t process_mode;     /* 0 PrintSimulation, 1 ScanNegative, 2 ProcessNegative */
  int32_t scan_negative_invert; /* invert the developed negative on scan */
  int32_t print_timing;         /* 0 FilteredEnlarger, 1 ApdPrinterDensity */
  int32_t printer_lights_gang;
  int32_t printer_light_calibration;
  float printer_lights_r, printer_lights_g, printer_lights_b;
  int32_t camera_uv_filter_enabled;
  float camera_uv_cut_nm;
  int32_t camera_ir_filter_enabled;
  float camera_ir_cut_nm;
  double exposure_ev;       /* exposure_comp_ev */
  double print_exposure;    /* print stage exposure multiplier */
  int32_t print_auto_exposure; /* 1 = re-normalise mid-grey (subtle exposure);
                                  0 = full uncompensated exposure (default) */
  int32_t couplers_active;  /* 1 = DIR coupler diffusion on */
  double coupler_diffusion_um; /* < 0 = take core size from the pack */
  int32_t lut_steps;        /* 0 = exact spectral; >=2 = runtime 3D tables */
  double pixel_um;          /* image pixel size on film (blur scale) */
  int32_t input_space;      /* input gamut index (sp_color.h; 0 = sRGB) */
  int32_t input_transfer;   /* sp_input_transfer_t: how samples map to linear */
  int32_t input_profile_auto; /* auto document ICC */
  int32_t input_custom_xyz_valid; /* derived from document ICC */
  double input_custom_rgb_to_xyz[9];
  double input_custom_white_xy[2];
  int32_t auto_exposure;          /* auto exposure enable */
  int32_t auto_exposure_method;    /* 0 centre weighted, 1 median */

  /* DIR coupler matrix controls (the spatial diffusion above is the caller's
   * blur; these scale/inhibit the engine's coupler matrix). Engine defaults
   * are 1.0 across the board, so a block with the defaults renders exactly as
   * before. 0 disables the effect without switching couplers off entirely. */
  double coupler_amount;               /* 0..2, OFX dirAmount */
  double coupler_inhibition_samelayer; /* 0..2, OFX dirInhibitionSameLayer */
  double coupler_inhibition_interlayer;/* 0..2, OFX dirInhibitionInterlayer */

  /* Phase 2 spatial look stages (mirror sp_chain_params_t). The on/off flags
   * default to 0 so an unedited block renders exactly the Phase-1 slice. */
  float boost_ev, boost_range, protect_ev;
  int32_t diffusion_on, diffusion_family;
  float diffusion_strength, diffusion_scale, diffusion_warmth;
  float diffusion_core_intensity, diffusion_core_size, diffusion_halo_intensity,
    diffusion_halo_size, diffusion_bloom_intensity, diffusion_bloom_size;
  int32_t halation_on;
  float scatter_amount, scatter_scale, halation_amount, halation_scale;
  int32_t grain_on;
  float grain_amount, grain_saturation, grain_blur, grain_granularity, grain_uniformity;
  int grain_seed;
  float grain_micro_structure_scale;
  float grain_micro_structure_sigma_nm;
  float grain_sublayer_scale, grain_density_min, grain_dye_cloud;
  float grain_usm_sigma, grain_usm_amount;
  /* Grain surface (OFX GrainParams): model + sub-layer split and the
   * per-channel/per-layer overrides. Every default reproduces the stock's own
   * figures, so an unedited block is a no-op. */
  int32_t grain_model;                 /* 0 = preview, 1 = production */
  int32_t grain_sublayers_enabled;     /* 0 = single layer, 1 = layered */
  int32_t grain_sublayer_count;        /* 0 = stock, else 1..8 */
  float grain_particle_scale[3];       /* per-channel particle-area scale (1 = stock) */
  float grain_particle_scale_layer[3]; /* per-layer particle-area scale (1 = stock) */
  float grain_uniformity_rgb[3];       /* per-channel uniformity scale (1 = stock) */
  float grain_density_min_rgb[3];      /* per-channel density-floor scale (1 = stock) */
  float grain_final_blur_um;           /* > 0 = absolute clump blur um; < 0 = stock */
  float grain_dye_cloud_um;            /* > 0 = absolute dye-cloud blur um; < 0 = stock */
  int32_t print_diffusion_on, print_diffusion_family;
  float print_diffusion_strength, print_diffusion_scale, print_diffusion_warmth;
  float print_diffusion_core_intensity, print_diffusion_core_size, print_diffusion_halo_intensity,
    print_diffusion_halo_size, print_diffusion_bloom_intensity, print_diffusion_bloom_size;
  float scan_blur, scan_usm_sigma, scan_usm_amount, glare_percent,
  scan_glare_roughness, scan_glare_blur;

  /* Locked v1 scope. DIR chroma gammas are 0 = "the stock's own fitted
   * coefficients"; the print filtration trims and the preflash are neutral at
   * 0; the halation weights and per-channel first-bounce radii are -1 = "the
   * stock's own"; the scanner black/white
   * point matches the engine defaults (both corrections on, 0.01/0.98). */
  double dir_gamma_same[3];       /* same-layer R,G,B gamma */
  double dir_gamma_r_gb[2];       /* R -> G,B */
  double dir_gamma_g_rb[2];       /* G -> R,B */
  double dir_gamma_b_rg[2];       /* B -> R,G */
  double filter_m_shift, filter_y_shift, filter_c_shift;
  double preflash_exposure, preflash_m_shift, preflash_y_shift;
  double halation_strength[3];    /* per-channel return weight */
  double halation_first_sigma_um[3]; /* per-channel first-bounce radius, um;
                                        < 0 = the stock's own */
  double halation_sigma_max_um;    /* independent upper clamp on the
                                       first-sigma radii above, um;
                                       < 0 = the stock ceiling (65 um) */
  int32_t scan_black_correction, scan_white_correction;
  double scan_black_level, scan_white_level;
  double scan_mtf50_lpmm, scan_usm_radius_um;

  /* Film/print curve chemistry (OFX filmPushPullStops / filmGamma / printGamma).
   * 0/1/1 are the no-ops, so an unedited block renders the Phase-1 slice. */
  double film_push_pull_stops;    /* Standard push/pull, stops (+ = push) */
  int film_push_pull_mode;        /* 0=Standard, 1=Experimental */
  double negative_bleach_bypass_amount; /* 0..1 */
  double negative_leuco_cyan_coupling;  /* 0..2 (1 = neutral) */
  double print_bleach_bypass_amount;    /* 0..1 */
  double film_gamma;              /* film density-curve slope scale (1 = stock) */
  double print_gamma;             /* print density-curve slope scale (1 = paper) */
  double print_push_pull_stops;   /* print slope x 2^(st/4); 0 = off (OFX) */
  double print_shadow_shape;      /* print toe bias, -1..1 (0 = off) */
  double print_highlight_shape;   /* print shoulder bias, -1..1 (0 = off) */

  /* Film format (OFX filmFormat). 0 = "Custom" (use pixel_um directly); else an
   * index into the gauge table in sp_chain.h (sp_film_format_mm), from which
   * the chain derives the film pixel size and scales the grain. */
  int32_t film_format;

  /* RGB -> film raw spectral upsampling method (sf_rgb_to_raw_method_t:
   * 0 Hanatos2025, 1 Mallett2019, 2 Hanatos2026). 2 is the reference default
   * and the engine's historical behaviour, so an unedited block renders
   * byte-identically. Persisted as the enum value, not the combo index. */
  int32_t rgb_to_raw_method;

  /* Auto-exposure correction (sp_chain_params_t::auto_exposure_ev): the host
   * meters the frame before the dialog and writes the EV here; sp_chain.c adds
   * it to the film exposure at filter start. It is recomputed and overwritten
   * every invocation, so persisting it is idempotent. It occupies what was tail
   * padding, so the block size is unchanged and a block written by an older
   * build (zeroed padding) reads as "no correction". */
  double auto_exposure_ev;

  /* pad to a fixed, stable size */
  uint8_t reserved[8];
} sf_params_block;

/* Fill *b with the shared defaults (sp_params_defaults, film/paper indices
 * resolved from pack_list.h). */
void sf_params_init(sf_params_block *b);

/* Sanity-check a block read from a handle. Returns 0 when the block is
 * usable (matching magic+version and sane indices). */
int sf_params_validate(const sf_params_block *b);

/* Project the block onto an sp_chain_params_t (film/paper become profile
 * stems from pack_list.h). */
void sf_params_to_chain(const sf_params_block *b, sp_chain_params_t *out);

#ifdef __cplusplus
}
#endif