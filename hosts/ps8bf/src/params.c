/* SPDX-License-Identifier: GPL-3.0-or-later */
/* params.c — implementation (see params.h). */
#include <math.h>
#include <string.h>

#include "params.h"

#include "pack_list.h"
#include "sp_params.h"

static int sf_params_find(const char *const *list, int n, const char *stem)
{
  int i;
  if(!stem) return -1;
  for(i = 0; i < n; i++)
    if(strcmp(list[i], stem) == 0) return i;
  return -1;
}

void sf_params_init(sf_params_block *b)
{
  sp_chain_params_t cp;
  if(!b) return;
  memset(b, 0, sizeof *b);
  b->magic = SF_PARAMS_BLOCK_MAGIC;
  b->version = SF_PARAMS_BLOCK_VERSION;

  /* The factory look: Kodak Ektar 100 on Kodak Ektacolor Edge. The shared
   * defaults in sp_params_defaults() carry the same pair for refrender and the
   * harness, and the end-to-end test compares the two, so change them
   * together. */
  b->film_index = sf_params_find(SF_FILMS, SF_FILMS_COUNT, "kodak_ektar_100");
  if(b->film_index < 0) b->film_index = 0;
  b->paper_index = sf_params_find(SF_PAPERS, SF_PAPERS_COUNT,
                                  "kodak_ektacolor_edge");
  if(b->paper_index < 0) b->paper_index = 0;

  sp_params_defaults(&cp);
  b->scan_film = cp.scan_film;
  b->process_mode = cp.process_mode;
  b->scan_negative_invert = cp.scan_negative_invert;
  b->print_timing = cp.print_timing;
  b->printer_lights_gang = cp.printer_lights_gang;
  b->printer_light_calibration = cp.printer_light_calibration;
  b->printer_lights_r = cp.printer_lights_r;
  b->printer_lights_g = cp.printer_lights_g;
  b->printer_lights_b = cp.printer_lights_b;
  b->camera_uv_filter_enabled = cp.camera_uv_filter_enabled;
  b->camera_uv_cut_nm = cp.camera_uv_cut_nm;
  b->camera_ir_filter_enabled = cp.camera_ir_filter_enabled;
  b->camera_ir_cut_nm = cp.camera_ir_cut_nm;
  b->exposure_ev = cp.exposure_ev;
  b->print_exposure = cp.print_exposure;
  b->print_auto_exposure = cp.print_auto_exposure;
  b->couplers_active = cp.couplers_active;
  b->coupler_diffusion_um = cp.coupler_diffusion_um;
  b->coupler_amount = cp.coupler_amount;
  b->coupler_inhibition_samelayer = cp.coupler_inhibition_samelayer;
  b->coupler_inhibition_interlayer = cp.coupler_inhibition_interlayer;
  b->lut_steps = cp.lut_steps;
  b->pixel_um = cp.pixel_um;
  b->film_format = 0; /* Custom: use pixel_um directly */
  b->input_space = cp.input_space;
  b->input_transfer = cp.input_transfer;
  b->input_profile_auto = 0;
  b->input_custom_xyz_valid = 0;
  for(int i=0;i<9;i++) b->input_custom_rgb_to_xyz[i]= (i%4==0)?1.0:0.0; // identity-ish
  b->input_custom_white_xy[0]=0.3127;
  b->input_custom_white_xy[1]=0.3290;
  b->scan_mtf50_lpmm = 0.0;
  b->scan_usm_radius_um = 0.0;
  b->auto_exposure = 0;
  b->auto_exposure_method = 0;
  b->auto_exposure_ev = 0.0;

  b->boost_ev = cp.boost_ev;
  b->boost_range = cp.boost_range;
  b->protect_ev = cp.protect_ev;
  b->diffusion_on = cp.diffusion_on;
  b->diffusion_family = cp.diffusion_family;
  b->diffusion_strength = cp.diffusion_strength;
  b->diffusion_scale = cp.diffusion_scale;
  b->diffusion_warmth = cp.diffusion_warmth;
  b->diffusion_core_intensity = cp.diffusion_core_intensity;
  b->diffusion_core_size = cp.diffusion_core_size;
  b->diffusion_halo_intensity = cp.diffusion_halo_intensity;
  b->diffusion_halo_size = cp.diffusion_halo_size;
  b->diffusion_bloom_intensity = cp.diffusion_bloom_intensity;
  b->diffusion_bloom_size = cp.diffusion_bloom_size;
  b->halation_on = cp.halation_on;
  b->scatter_amount = cp.scatter_amount;
  b->scatter_scale = cp.scatter_scale;
  b->halation_amount = cp.halation_amount;
  b->halation_scale = cp.halation_scale;
  b->grain_on = cp.grain_on;
  b->grain_amount = cp.grain_amount;
  b->grain_saturation = cp.grain_saturation;
  b->grain_seed = cp.grain_seed;
  b->grain_micro_structure_scale = cp.grain_micro_structure_scale;
  b->grain_micro_structure_sigma_nm = cp.grain_micro_structure_sigma_nm;
  b->grain_blur = cp.grain_blur;
  b->grain_granularity = cp.grain_granularity;
  b->grain_uniformity = cp.grain_uniformity;
  b->grain_sublayer_scale = cp.grain_sublayer_scale;
  b->grain_density_min = cp.grain_density_min;
  b->grain_dye_cloud = cp.grain_dye_cloud;
  b->grain_usm_sigma = cp.grain_usm_sigma;
  b->grain_usm_amount = cp.grain_usm_amount;
  b->grain_model = cp.grain_model;
  b->grain_sublayers_enabled = cp.grain_sublayers_enabled;
  b->grain_sublayer_count = cp.grain_sublayer_count;
  memcpy(b->grain_particle_scale, cp.grain_particle_scale, sizeof b->grain_particle_scale);
  memcpy(b->grain_particle_scale_layer, cp.grain_particle_scale_layer,
         sizeof b->grain_particle_scale_layer);
  memcpy(b->grain_uniformity_rgb, cp.grain_uniformity_rgb, sizeof b->grain_uniformity_rgb);
  memcpy(b->grain_density_min_rgb, cp.grain_density_min_rgb, sizeof b->grain_density_min_rgb);
  b->grain_final_blur_um = cp.grain_final_blur_um;
  b->grain_dye_cloud_um = cp.grain_dye_cloud_um;
  b->print_diffusion_on = cp.print_diffusion_on;
  b->print_diffusion_family = cp.print_diffusion_family;
  b->print_diffusion_strength = cp.print_diffusion_strength;
  b->print_diffusion_scale = cp.print_diffusion_scale;
  b->print_diffusion_warmth = cp.print_diffusion_warmth;
  b->print_diffusion_core_intensity = cp.print_diffusion_core_intensity;
  b->print_diffusion_core_size = cp.print_diffusion_core_size;
  b->print_diffusion_halo_intensity = cp.print_diffusion_halo_intensity;
  b->print_diffusion_halo_size = cp.print_diffusion_halo_size;
  b->print_diffusion_bloom_intensity = cp.print_diffusion_bloom_intensity;
  b->print_diffusion_bloom_size = cp.print_diffusion_bloom_size;
  b->scan_blur = cp.scan_blur;
  b->scan_usm_sigma = cp.scan_usm_sigma;
  b->scan_usm_amount = cp.scan_usm_amount;
  b->glare_percent = cp.glare_percent;
  b->scan_glare_roughness = cp.scan_glare_roughness;
  b->scan_glare_blur = cp.scan_glare_blur;

  memcpy(b->dir_gamma_same, cp.dir_gamma_same, sizeof b->dir_gamma_same);
  memcpy(b->dir_gamma_r_gb, cp.dir_gamma_r_gb, sizeof b->dir_gamma_r_gb);
  memcpy(b->dir_gamma_g_rb, cp.dir_gamma_g_rb, sizeof b->dir_gamma_g_rb);
  memcpy(b->dir_gamma_b_rg, cp.dir_gamma_b_rg, sizeof b->dir_gamma_b_rg);
  b->filter_m_shift = cp.filter_m_shift;
  b->filter_y_shift = cp.filter_y_shift;
  b->filter_c_shift = cp.filter_c_shift;
  b->preflash_exposure = cp.preflash_exposure;
  b->preflash_m_shift = cp.preflash_m_shift;
  b->preflash_y_shift = cp.preflash_y_shift;
  memcpy(b->halation_strength, cp.halation_strength, sizeof b->halation_strength);
  memcpy(b->halation_first_sigma_um, cp.halation_first_sigma_um,
         sizeof b->halation_first_sigma_um);
  b->halation_sigma_max_um = cp.halation_sigma_max_um;
  b->scan_black_correction = cp.scan_black_correction;
  b->scan_white_correction = cp.scan_white_correction;
  b->scan_black_level = cp.scan_black_level;
  b->scan_white_level = cp.scan_white_level;
  b->film_push_pull_stops = cp.film_push_pull_stops;
  b->film_push_pull_mode = cp.film_push_pull_mode;
  b->negative_bleach_bypass_amount = cp.negative_bleach_bypass_amount;
  b->negative_leuco_cyan_coupling = cp.negative_leuco_cyan_coupling;
  b->print_bleach_bypass_amount = cp.print_bleach_bypass_amount;
  b->film_gamma = cp.film_gamma;
  b->print_gamma = cp.print_gamma;
  b->print_push_pull_stops = cp.print_push_pull_stops;
  b->print_shadow_shape = cp.print_shadow_shape;
  b->print_highlight_shape = cp.print_highlight_shape;
  b->rgb_to_raw_method = cp.rgb_to_raw_method;
}

int sf_params_validate(const sf_params_block *b)
{
  if(!b) return 1;
  if(b->magic != SF_PARAMS_BLOCK_MAGIC) return 2;
  if(b->version != SF_PARAMS_BLOCK_VERSION) return 3;
  if(b->film_index < 0 || b->film_index >= SF_FILMS_COUNT) return 4;
  if(!b->scan_film && (b->paper_index < 0 || b->paper_index >= SF_PAPERS_COUNT))
    return 5;
  if(!(b->print_exposure >= 0.0 && b->print_exposure <= 10.0) ||
     !isfinite(b->print_exposure)) return 6;
  if(!(b->exposure_ev >= -8.0 && b->exposure_ev <= 8.0) ||
     !isfinite(b->exposure_ev)) return 7;
  if(!isfinite(b->auto_exposure_ev)) return 42;
  /* The runtime LUT table is 0 (exact) or 2..33 steps; 1 is meaningless. */
  if(b->lut_steps < 0 || b->lut_steps == 1 || b->lut_steps > 33) return 8;
  if(!(b->pixel_um >= 0.5 && b->pixel_um <= 40.0) || !isfinite(b->pixel_um))
    return 9;
  if(b->film_format < 0 || b->film_format >= sp_film_format_count()) return 16;
  if(b->input_space < 0 || b->input_space >= sp_color_space_count()) return 10;
  if(b->input_transfer < SP_TRANSFER_SRGB || b->input_transfer >= SP_TRANSFER_COUNT)
    return 15;
  /* RGB -> raw method enum (0 Hanatos2025, 1 Mallett2019, 2 Hanatos2026). */
  if(b->rgb_to_raw_method < 0 || b->rgb_to_raw_method > 2) return 17;
  /* -1.0 is the "use the pack's diffusion" sentinel; anything else must be > 0. */
  if(b->coupler_diffusion_um != -1.0 &&
     !(b->coupler_diffusion_um > 0.0 && isfinite(b->coupler_diffusion_um)))
    return 11;
  /* DIR coupler matrix controls: 0..2 inclusive, finite. */
  if(!(b->coupler_amount >= 0.0 && b->coupler_amount <= 2.0) ||
     !isfinite(b->coupler_amount)) return 12;
  if(!(b->coupler_inhibition_samelayer >= 0.0 &&
       b->coupler_inhibition_samelayer <= 2.0) ||
     !isfinite(b->coupler_inhibition_samelayer)) return 13;
  if(!(b->coupler_inhibition_interlayer >= 0.0 &&
       b->coupler_inhibition_interlayer <= 2.0) ||
     !isfinite(b->coupler_inhibition_interlayer)) return 14;
  /* Phase 2 spatial look: flags must be 0/1, families in range, strengths and
   * radii finite and non-negative. A block from an older build is re-inited
   * (see the version check above), so this only rejects corrupted data. */
  if(b->diffusion_on < 0 || b->diffusion_on > 1) return 16;
  if(b->diffusion_family < 0 || b->diffusion_family > 3) return 17;
  if(b->halation_on < 0 || b->halation_on > 1) return 18;
  if(b->process_mode < 0 || b->process_mode > 2) return 18;
  if(b->scan_negative_invert < 0 || b->scan_negative_invert > 1) return 18;
  if(b->print_timing < 0 || b->print_timing > 1) return 18;
  if(b->printer_lights_gang < 0 || b->printer_lights_gang > 1) return 18;
  if(b->printer_light_calibration < 0 || b->printer_light_calibration > 1) return 18;
  if(b->camera_uv_filter_enabled < 0 || b->camera_uv_filter_enabled > 1) return 18;
  if(b->camera_ir_filter_enabled < 0 || b->camera_ir_filter_enabled > 1) return 18;
  if(!isfinite(b->printer_lights_r) || !isfinite(b->printer_lights_g) ||
     !isfinite(b->printer_lights_b) || !isfinite(b->camera_uv_cut_nm) ||
     !isfinite(b->camera_ir_cut_nm)) return 18;
  if(b->camera_uv_cut_nm < 280.0 || b->camera_uv_cut_nm > 460.0) return 18;
  if(b->camera_ir_cut_nm < 600.0 || b->camera_ir_cut_nm > 760.0) return 18;
  if(b->grain_on < 0 || b->grain_on > 1) return 19;
  if(b->grain_model < 0 || b->grain_model > 1) return 36;
  if(b->grain_sublayers_enabled < 0 || b->grain_sublayers_enabled > 1) return 37;
  if(b->grain_sublayer_count < 0 || b->grain_sublayer_count > 8) return 38;
  if(b->print_diffusion_on < 0 || b->print_diffusion_on > 1) return 20;
  if(b->print_diffusion_family < 0 || b->print_diffusion_family > 3) return 21;
  if(!isfinite(b->boost_ev) || !isfinite(b->boost_range) || !isfinite(b->protect_ev) ||
     !isfinite(b->diffusion_strength) || !isfinite(b->diffusion_scale) ||
     !isfinite(b->diffusion_warmth) || !isfinite(b->diffusion_core_intensity) ||
     !isfinite(b->diffusion_core_size) || !isfinite(b->diffusion_halo_intensity) ||
     !isfinite(b->diffusion_halo_size) || !isfinite(b->diffusion_bloom_intensity) ||
     !isfinite(b->diffusion_bloom_size) || !isfinite(b->scatter_amount) ||
     !isfinite(b->scatter_scale) || !isfinite(b->halation_amount) ||
     !isfinite(b->halation_scale) || !isfinite(b->grain_amount) ||
     !isfinite(b->grain_blur) || !isfinite(b->grain_granularity) ||
     !isfinite(b->grain_uniformity) || !isfinite(b->grain_sublayer_scale) ||
     !isfinite(b->grain_density_min) || !isfinite(b->grain_dye_cloud) ||
     !isfinite(b->grain_usm_sigma) || !isfinite(b->grain_usm_amount) ||
     !isfinite(b->print_diffusion_strength) || !isfinite(b->print_diffusion_scale) ||
     !isfinite(b->print_diffusion_warmth) || !isfinite(b->print_diffusion_core_intensity) ||
     !isfinite(b->print_diffusion_core_size) || !isfinite(b->print_diffusion_halo_intensity) ||
     !isfinite(b->print_diffusion_halo_size) || !isfinite(b->print_diffusion_bloom_intensity) ||
     !isfinite(b->print_diffusion_bloom_size) || !isfinite(b->scan_blur) ||
     !isfinite(b->scan_usm_sigma) || !isfinite(b->scan_usm_amount) ||
     !isfinite(b->glare_percent) ||
     !isfinite(b->scan_glare_roughness) || !isfinite(b->scan_glare_blur))
    return 22;
  /* Grain surface extras: every scale finite, the absolute um overrides finite
   * (negative is the "use the stock-scaled path" sentinel). */
  {
    int k;
    for(k = 0; k < 3; k++)
      if(!isfinite(b->grain_particle_scale[k]) ||
         !isfinite(b->grain_particle_scale_layer[k]) ||
         !isfinite(b->grain_uniformity_rgb[k]) ||
         !isfinite(b->grain_density_min_rgb[k]))
        return 39;
  }
  if(!isfinite(b->grain_final_blur_um) || !isfinite(b->grain_dye_cloud_um)) return 39;
  /* Locked v1 scope extras: flags 0/1, every value finite. */
  if(b->scan_black_correction < 0 || b->scan_black_correction > 1) return 23;
  if(b->scan_white_correction < 0 || b->scan_white_correction > 1) return 24;
  {
    int k;
    for(k = 0; k < 3; k++)
      if(!isfinite(b->dir_gamma_same[k]) || !isfinite(b->halation_strength[k]) ||
         !isfinite(b->halation_first_sigma_um[k]) || !isfinite(b->halation_sigma_max_um))
        return 25;
    for(k = 0; k < 2; k++)
      if(!isfinite(b->dir_gamma_r_gb[k]) || !isfinite(b->dir_gamma_g_rb[k]) ||
         !isfinite(b->dir_gamma_b_rg[k]))
        return 25;
    if(!isfinite(b->filter_m_shift) || !isfinite(b->filter_y_shift) ||
       !isfinite(b->filter_c_shift) ||
       !isfinite(b->preflash_exposure) || !isfinite(b->preflash_m_shift) ||
       !isfinite(b->preflash_y_shift) || !isfinite(b->scan_black_level) ||
       !isfinite(b->scan_white_level))
      return 25;
  }
  /* halation Sigma Max: < 0 = the stock ceiling, else positive finite and no
   * higher than the surface's own cap. */
  if(b->halation_sigma_max_um >= 0.0 &&
     !(b->halation_sigma_max_um > 0.0 &&
       b->halation_sigma_max_um <= SF_HALATION_SIGMA_MAX_CAP_UM))
    return 41;
  /* Per-channel halation first-bounce radius: < 0 = "the stock's own", else a
   * radius the CPU port can clamp to (the independent ceiling). */
  {
    int k;
    double cap = (b->halation_sigma_max_um >= 0.0) ? b->halation_sigma_max_um
                                                   : SP_HALATION_FIRST_SIGMA_UM;
    for(k = 0; k < 3; k++)
      if(b->halation_first_sigma_um[k] >= 0.0 && b->halation_first_sigma_um[k] > cap)
        return 40;
  }
  /* Spatial radius multipliers: the sliders' own range (params.h). Each one
   * multiplies a radius in micrometres, so an out-of-range multiplier is an
   * out-of-range blur, and the finiteness test above is not enough: a preset
   * read from disk or the clipboard never passes a slider, and halation runs
   * (sigma_px / 150)^2 recursive passes. */
  if(!(b->scatter_scale >= SF_BLUR_SCALE_MIN && b->scatter_scale <= SF_BLUR_SCALE_MAX) ||
     !(b->halation_scale >= SF_BLUR_SCALE_MIN && b->halation_scale <= SF_BLUR_SCALE_MAX) ||
     !(b->diffusion_scale >= SF_BLUR_SCALE_MIN && b->diffusion_scale <= SF_BLUR_SCALE_MAX) ||
     !(b->print_diffusion_scale >= SF_BLUR_SCALE_MIN &&
       b->print_diffusion_scale <= SF_BLUR_SCALE_MAX))
    return 43;
  /* Film/print curve chemistry: development stops in +-4, gammas 0.25..4. */
  if(!(b->film_push_pull_stops >= -4.0 && b->film_push_pull_stops <= 4.0) ||
     !isfinite(b->film_push_pull_stops)) return 26;
  if(!(b->film_gamma >= 0.25 && b->film_gamma <= 4.0) || !isfinite(b->film_gamma))
    return 27;
  if(!(b->print_gamma >= 0.25 && b->print_gamma <= 4.0) || !isfinite(b->print_gamma))
    return 28;
  /* OFX ranges: filterC in 0..120 CC; print push/pull in +-2 stops; the print
   * toe/shoulder shaping in +-1. */
  if(!(b->filter_c_shift >= 0.0 && b->filter_c_shift <= 120.0)) return 29;
  if(!(b->print_push_pull_stops >= -2.0 && b->print_push_pull_stops <= 2.0) ||
     !isfinite(b->print_push_pull_stops)) return 30;
  if(!(b->print_shadow_shape >= -1.0 && b->print_shadow_shape <= 1.0) ||
     !(b->print_highlight_shape >= -1.0 && b->print_highlight_shape <= 1.0) ||
     !isfinite(b->print_shadow_shape) || !isfinite(b->print_highlight_shape))
    return 31;
  /* Bleach bypass (OFX ranges): bypass amounts 0..1, leuco cyan coupling 0..2. */
  if(!isfinite(b->negative_bleach_bypass_amount) ||
     !isfinite(b->negative_leuco_cyan_coupling) ||
     !isfinite(b->print_bleach_bypass_amount))
    return 32;
  if(!(b->negative_bleach_bypass_amount >= 0.0 &&
       b->negative_bleach_bypass_amount <= 1.0)) return 33;
  if(!(b->negative_leuco_cyan_coupling >= 0.0 &&
       b->negative_leuco_cyan_coupling <= 2.0)) return 34;
  if(!(b->print_bleach_bypass_amount >= 0.0 &&
       b->print_bleach_bypass_amount <= 1.0)) return 35;
  return 0;
}

void sf_params_to_chain(const sf_params_block *b, sp_chain_params_t *out)
{
  if(!out) return;
  sp_params_defaults(out); /* baseline */

  out->pack_dir = "pack"; /* embedded asset namespace */
  out->film = SF_FILMS[b->film_index];
  out->paper = SF_PAPERS[b->paper_index >= 0 && b->paper_index < SF_PAPERS_COUNT
                            ? b->paper_index
                            : 0];
  out->scan_film = (b->process_mode == 0) ? (b->scan_film ? 1 : 0) : 1;
  out->process_mode = b->process_mode;
  out->scan_negative_invert = b->scan_negative_invert;
  out->print_timing = b->print_timing;
  out->printer_lights_gang = b->printer_lights_gang;
  out->printer_light_calibration = b->printer_light_calibration;
  out->printer_lights_r = (float)b->printer_lights_r;
  out->printer_lights_g = (float)b->printer_lights_g;
  out->printer_lights_b = (float)b->printer_lights_b;
  out->camera_uv_filter_enabled = b->camera_uv_filter_enabled;
  out->camera_uv_cut_nm = (float)b->camera_uv_cut_nm;
  out->camera_ir_filter_enabled = b->camera_ir_filter_enabled;
  out->camera_ir_cut_nm = (float)b->camera_ir_cut_nm;
  out->exposure_ev = b->exposure_ev;
  /* The meter correction is only meaningful while auto exposure is enabled; a
   * stale value from a previous frame (or a disabled toggle) must not leak into
   * the chain. */
  out->auto_exposure_ev = b->auto_exposure ? b->auto_exposure_ev : 0.0;
  out->print_exposure = b->print_exposure;
  out->print_auto_exposure = b->print_auto_exposure ? 1 : 0;
  out->couplers_active = b->couplers_active ? 1 : 0;
  out->coupler_diffusion_um = b->coupler_diffusion_um;
  out->coupler_amount = b->coupler_amount;
  out->coupler_inhibition_samelayer = b->coupler_inhibition_samelayer;
  out->coupler_inhibition_interlayer = b->coupler_inhibition_interlayer;
  out->lut_steps = b->lut_steps;
  out->pixel_um = b->pixel_um;
  out->film_format_mm = sp_film_format_mm(b->film_format);
  out->rgb_to_raw_method = b->rgb_to_raw_method;
  out->input_space = b->input_space;
  out->input_transfer = b->input_transfer;
  out->input_custom_xyz_valid = b->input_custom_xyz_valid;
  for(int i=0;i<9;i++) out->input_custom_rgb_to_xyz[i] = b->input_custom_rgb_to_xyz[i];
  out->input_custom_white_xy[0] = b->input_custom_white_xy[0];
  out->input_custom_white_xy[1] = b->input_custom_white_xy[1];

  out->boost_ev = b->boost_ev;
  out->boost_range = b->boost_range;
  out->protect_ev = b->protect_ev;
  out->diffusion_on = b->diffusion_on;
  out->diffusion_family = b->diffusion_family;
  out->diffusion_strength = b->diffusion_strength;
  out->diffusion_scale = b->diffusion_scale;
  out->diffusion_warmth = b->diffusion_warmth;
  out->diffusion_core_intensity = b->diffusion_core_intensity;
  out->diffusion_core_size = b->diffusion_core_size;
  out->diffusion_halo_intensity = b->diffusion_halo_intensity;
  out->diffusion_halo_size = b->diffusion_halo_size;
  out->diffusion_bloom_intensity = b->diffusion_bloom_intensity;
  out->diffusion_bloom_size = b->diffusion_bloom_size;
  out->halation_on = b->halation_on;
  out->scatter_amount = b->scatter_amount;
  out->scatter_scale = b->scatter_scale;
  out->halation_amount = b->halation_amount;
  out->halation_scale = b->halation_scale;
  out->grain_on = b->grain_on;
  out->grain_amount = b->grain_amount;
  out->grain_saturation = b->grain_saturation;
  out->grain_seed = b->grain_seed;
  out->grain_micro_structure_scale = b->grain_micro_structure_scale;
  out->grain_micro_structure_sigma_nm = b->grain_micro_structure_sigma_nm;
  out->grain_blur = b->grain_blur;
  out->grain_granularity = b->grain_granularity;
  out->grain_uniformity = b->grain_uniformity;
  out->grain_sublayer_scale = b->grain_sublayer_scale;
  out->grain_density_min = b->grain_density_min;
  out->grain_dye_cloud = b->grain_dye_cloud;
  out->grain_usm_sigma = b->grain_usm_sigma;
  out->grain_usm_amount = b->grain_usm_amount;
  out->grain_model = b->grain_model;
  out->grain_sublayers_enabled = b->grain_sublayers_enabled;
  out->grain_sublayer_count = b->grain_sublayer_count;
  for(int i = 0; i < 3; i++)
  {
    out->grain_particle_scale[i] = b->grain_particle_scale[i];
    out->grain_particle_scale_layer[i] = b->grain_particle_scale_layer[i];
    out->grain_uniformity_rgb[i] = b->grain_uniformity_rgb[i];
    out->grain_density_min_rgb[i] = b->grain_density_min_rgb[i];
  }
  out->grain_final_blur_um = b->grain_final_blur_um;
  out->grain_dye_cloud_um = b->grain_dye_cloud_um;
  out->print_diffusion_on = b->print_diffusion_on;
  out->print_diffusion_family = b->print_diffusion_family;
  out->print_diffusion_strength = b->print_diffusion_strength;
  out->print_diffusion_scale = b->print_diffusion_scale;
  out->print_diffusion_warmth = b->print_diffusion_warmth;
  out->print_diffusion_core_intensity = b->print_diffusion_core_intensity;
  out->print_diffusion_core_size = b->print_diffusion_core_size;
  out->print_diffusion_halo_intensity = b->print_diffusion_halo_intensity;
  out->print_diffusion_halo_size = b->print_diffusion_halo_size;
  out->print_diffusion_bloom_intensity = b->print_diffusion_bloom_intensity;
  out->print_diffusion_bloom_size = b->print_diffusion_bloom_size;
  out->scan_blur = b->scan_blur;
  out->scan_usm_sigma = b->scan_usm_sigma;
  out->scan_usm_amount = b->scan_usm_amount;
  out->glare_percent = b->glare_percent;
  out->scan_glare_roughness = b->scan_glare_roughness;
  out->scan_glare_blur = b->scan_glare_blur;

  memcpy(out->dir_gamma_same, b->dir_gamma_same, sizeof out->dir_gamma_same);
  memcpy(out->dir_gamma_r_gb, b->dir_gamma_r_gb, sizeof out->dir_gamma_r_gb);
  memcpy(out->dir_gamma_g_rb, b->dir_gamma_g_rb, sizeof out->dir_gamma_g_rb);
  memcpy(out->dir_gamma_b_rg, b->dir_gamma_b_rg, sizeof out->dir_gamma_b_rg);
  out->filter_m_shift = b->filter_m_shift;
  out->filter_y_shift = b->filter_y_shift;
  out->filter_c_shift = b->filter_c_shift;
  out->print_push_pull_stops = b->print_push_pull_stops;
  out->print_shadow_shape = b->print_shadow_shape;
  out->print_highlight_shape = b->print_highlight_shape;
  out->preflash_exposure = b->preflash_exposure;
  out->preflash_m_shift = b->preflash_m_shift;
  out->preflash_y_shift = b->preflash_y_shift;
  memcpy(out->halation_strength, b->halation_strength, sizeof out->halation_strength);
  memcpy(out->halation_first_sigma_um, b->halation_first_sigma_um,
         sizeof out->halation_first_sigma_um);
  out->halation_sigma_max_um = b->halation_sigma_max_um;
  out->scan_black_correction = b->scan_black_correction;
  out->scan_white_correction = b->scan_white_correction;
  out->scan_black_level = b->scan_black_level;
  out->scan_white_level = b->scan_white_level;
  out->scan_mtf50_lpmm = b->scan_mtf50_lpmm;
  out->scan_usm_radius_um = b->scan_usm_radius_um;
  out->film_push_pull_stops = b->film_push_pull_stops;
  out->film_push_pull_mode = b->film_push_pull_mode;
  out->negative_bleach_bypass_amount = b->negative_bleach_bypass_amount;
  out->negative_leuco_cyan_coupling = b->negative_leuco_cyan_coupling;
  out->print_bleach_bypass_amount = b->print_bleach_bypass_amount;
  out->film_gamma = b->film_gamma;
  out->print_gamma = b->print_gamma;
}