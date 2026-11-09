/* SPDX-License-Identifier: GPL-3.0-or-later */
/* sp_params — implementation of the shared params parse (see sp_params.h).
 *
 * The film/paper names arrive as JSON strings; they are copied into
 * module-static scratch (single-threaded guest + refrender both use the
 * pointer immediately, before the next parse overwrites it). */
#include <string.h>

#include "pc_json.h"
#include "sp_params.h"

static char g_film[256];
static char g_paper[256];

/* Defaults are the shared param defaults so both sides agree whether a key
 * arrives absent (the host fills defaults; refrender's params file may be
 * partial). */
void sp_params_defaults(sp_chain_params_t *out)
{
  memset(out, 0, sizeof(*out));
  out->pack_dir = "pack";
  out->film = g_film;
  out->paper = g_paper;
  /* The same pair as sf_params_init() in the .8bf host, which is what keeps
   * the harness byte-identical to refrender; change them together. */
  strcpy(g_film, "kodak_ektar_100");
  strcpy(g_paper, "kodak_ektacolor_edge");
  out->scan_film = 0;
  out->process_mode = 0;
  out->scan_negative_invert = 0;
  out->print_timing = 0;
  out->printer_lights_gang = 0;
  out->printer_light_calibration = 1;
  out->printer_lights_r = out->printer_lights_g = out->printer_lights_b = 0.0f;
  out->camera_uv_filter_enabled = 0;
  out->camera_uv_cut_nm = 410.0f;
  out->camera_ir_filter_enabled = 0;
  out->camera_ir_cut_nm = 675.0f;
  out->development_min = 0.0f;
  out->exposure_ev = 0.0;
  out->print_exposure = 1.0;
  out->print_auto_exposure = 0; /* off by default, matching the darktable
                                   module: film-stage exposure stays visible */
  out->film_push_pull_stops = 0.0; /* no push/pull */
  out->film_push_pull_mode = 0;   /* Standard */
  out->negative_bleach_bypass_amount = 0.0f;
  out->negative_leuco_cyan_coupling = 0.0f;
  out->print_bleach_bypass_amount = 0.0f;
  out->film_gamma = 1.0;           /* the stock profile's own curve */
  out->print_gamma = 1.0;          /* the paper profile's own curve */
  out->print_push_pull_stops = 0.0;/* no print push/pull */
  out->print_shadow_shape = 0.0;   /* no print toe/shoulder shaping */
  out->print_highlight_shape = 0.0;
  out->lut_steps = 17;      /* runtime 3D tables — reference default */
  out->couplers_active = 1;
  out->coupler_diffusion_um = -1.0; /* from pack */
  out->coupler_amount = 1.0;          /* engine peaks at 1 */
  out->coupler_inhibition_samelayer = 1.0;
  out->coupler_inhibition_interlayer = 1.0;
  out->pixel_um = 4.0;
  out->input_space = 0;
  out->input_transfer = SP_TRANSFER_SRGB; /* 8/16-bit host data is sRGB-encoded */     /* linear sRGB in */
  out->auto_exposure = 0;
  out->auto_exposure_method = 0;
  out->auto_exposure_ev = 0.0;
  out->input_custom_xyz_valid = 0;
  out->input_custom_rgb_to_xyz[0] = 1; out->input_custom_rgb_to_xyz[1] = 0; out->input_custom_rgb_to_xyz[2] = 0;
  out->input_custom_rgb_to_xyz[3] = 0; out->input_custom_rgb_to_xyz[4] = 1; out->input_custom_rgb_to_xyz[5] = 0;
  out->input_custom_rgb_to_xyz[6] = 0; out->input_custom_rgb_to_xyz[7] = 0; out->input_custom_rgb_to_xyz[8] = 1;
  out->input_custom_white_xy[0] = 0.3127;
  out->input_custom_white_xy[1] = 0.3290;
  /* RGB -> raw method: Hanatos2026, the reference default and this engine's
   * historical behaviour. */
  out->rgb_to_raw_method = 2;

  /* Phase 2 spatial look. The stage enablers default off so a caller that
   * leaves them alone renders exactly the Phase-1 slice; the sub-parameters
   * carry the reference's own values so flipping one on lands at the reference
   * look. */
  out->boost_ev = 0.0f;
  out->boost_range = 0.0f;   /* 0 -> SF_BOOST_SPAN_EV in sp_chain_create */
  out->protect_ev = 0.0f;
  out->diffusion_on = 0;
  out->diffusion_family = SP_DIFF_BLACK_PRO_MIST;
  out->diffusion_strength = 1.0f;
  out->diffusion_scale = 1.0f;
  out->diffusion_warmth = 0.0f;
  out->diffusion_core_intensity = 1.0f; out->diffusion_core_size = 1.0f;
  out->diffusion_halo_intensity = 1.0f; out->diffusion_halo_size = 1.0f;
  out->diffusion_bloom_intensity = 1.0f; out->diffusion_bloom_size = 1.0f;
  out->halation_on = 0;
  out->scatter_amount = 1.0f;
  out->scatter_scale = 1.0f;
  out->halation_amount = 1.0f;
  out->halation_scale = 1.0f;
  out->grain_on = 0;
  out->grain_amount = 1.0f;
  out->grain_saturation = 1.0f;
  out->grain_seed = 0;
  out->grain_micro_structure_scale = 1.0f;
  out->grain_micro_structure_sigma_nm = 30.0f;
  out->grain_blur = 1.0f;
  out->grain_granularity = 1.0f;
  out->grain_uniformity = 1.0f;
  out->grain_sublayer_scale = 1.0f;
  out->grain_density_min = 1.0f;
  out->grain_dye_cloud = 1.0f;
  out->grain_usm_sigma = 0.7f;
  out->grain_usm_amount = 1.5f;
  /* Grain surface (OFX GrainParams): production model, stock sub-layer split,
   * and no per-channel/per-layer override (all 1.0 = the stock's own figures).
   * The absolute um overrides are negative = "use the stock-scaled path". */
  out->grain_model = 1;               /* production */
  out->grain_sublayers_enabled = 1;   /* layered */
  out->grain_sublayer_count = 0;      /* stock */
  for(int i = 0; i < 3; i++)
  {
    out->grain_particle_scale[i] = 1.0f;
    out->grain_particle_scale_layer[i] = 1.0f;
    out->grain_uniformity_rgb[i] = 1.0f;
    out->grain_density_min_rgb[i] = 1.0f;
  }
  out->grain_final_blur_um = -1.0f;
  out->grain_dye_cloud_um = -1.0f;
  out->print_diffusion_on = 0;
  out->print_diffusion_family = SP_DIFF_BLACK_PRO_MIST;
  out->print_diffusion_strength = 1.0f;
  out->print_diffusion_scale = 1.0f;
  out->print_diffusion_warmth = 0.0f;
  out->print_diffusion_core_intensity = 1.0f; out->print_diffusion_core_size = 1.0f;
  out->print_diffusion_halo_intensity = 1.0f; out->print_diffusion_halo_size = 1.0f;
  out->print_diffusion_bloom_intensity = 1.0f; out->print_diffusion_bloom_size = 1.0f;
  out->scan_blur = 0.0f;
  out->scan_usm_sigma = 0.0f;
  out->scan_usm_amount = 0.0f;
  out->glare_percent = 0.0f;
  out->scan_glare_roughness = 0.7f;
  out->scan_glare_blur = 0.5f;

  /* Locked v1 scope extras. The DIR chroma gammas are 0 = "keep the stock's
   * own fitted coefficients"; the print filtration and preflash are neutral at
   * 0; the halation weights are -1 = "keep the stock's own"; the scanner
   * black/white point matches the engine's own defaults (both corrections on,
   * 0.01/0.98), so leaving them alone is a no-op. */
  out->dir_gamma_same[0] = out->dir_gamma_same[1] = out->dir_gamma_same[2] = 0.0;
  out->dir_gamma_r_gb[0] = out->dir_gamma_r_gb[1] = 0.0;
  out->dir_gamma_g_rb[0] = out->dir_gamma_g_rb[1] = 0.0;
  out->dir_gamma_b_rg[0] = out->dir_gamma_b_rg[1] = 0.0;
  out->filter_m_shift = 0.0;
  out->filter_y_shift = 0.0;
  out->filter_c_shift = 0.0;
  out->preflash_exposure = 0.0;
  out->preflash_m_shift = 0.0;
  out->preflash_y_shift = 0.0;
  out->halation_strength[0] = out->halation_strength[1] = out->halation_strength[2] = -1.0;
  out->halation_first_sigma_um[0] = out->halation_first_sigma_um[1] =
      out->halation_first_sigma_um[2] = -1.0;
  out->halation_sigma_max_um = -1.0;
  out->scan_black_correction = 1;
  out->scan_white_correction = 1;
  out->scan_black_level = 0.01;
  out->scan_white_level = 0.98;
  out->scan_mtf50_lpmm = 0.0;
  out->scan_usm_radius_um = 0.0;
}

static double parse_num(const pj_u8 *s, pj_u32 *ip, pj_u32 end, double dflt)
{
  pj_f64 v = dflt;
  pj_u32 i = *ip;
  if(i < end && (s[i] == '-' || (s[i] >= '0' && s[i] <= '9')))
    i = pj_number(s, i, end, &v);
  *ip = i;
  return v;
}

int sp_params_parse(const char *params, int len, sp_chain_params_t *out)
{
  if(!params || len <= 0) return 1;
  const pj_u8 *j = (const pj_u8 *)params;
  pj_u32 end = (pj_u32)len;
  pj_u32 i = pj_ws(j, 0, end);
  if(i < end && j[i] == '{') i = pj_ws(j, i + 1, end);

  while(i < end && j[i] != '}')
  {
    if(j[i] == ',') { i = pj_ws(j, i + 1, end); continue; }
    if(j[i] != '"') break;
    pj_u32 ks = i + 1;
    pj_u32 ke = pj_string_end(j, i, end);
    i = pj_ws(j, ke, end);
    if(i < end && j[i] == ':') i = pj_ws(j, i + 1, end);

    if(pj_key_is(j, ks, ke - 1, "film"))
    {
      if(i < end && j[i] == '"') i = pj_string(j, i, end, g_film, sizeof g_film);
      else i = pj_skip_value(j, i, end);
      out->film = g_film;
    }
    else if(pj_key_is(j, ks, ke - 1, "paper"))
    {
      if(i < end && j[i] == '"') i = pj_string(j, i, end, g_paper, sizeof g_paper);
      else i = pj_skip_value(j, i, end);
      out->paper = g_paper;
    }
    else if(pj_key_is(j, ks, ke - 1, "process"))
    {
      char proc[16] = { 0 };
      if(i < end && j[i] == '"') i = pj_string(j, i, end, proc, sizeof proc);
      else i = pj_skip_value(j, i, end);
      if(proc[0] && strcmp(proc, "print") == 0) { out->process_mode = 0; out->scan_film = 0; }
      else if(proc[0] && strcmp(proc, "scan") == 0) { out->process_mode = 1; out->scan_film = 1; }
      else if(proc[0] && (strcmp(proc, "process") == 0 || strcmp(proc, "negative") == 0)) { out->process_mode = 2; out->scan_film = 1; }
      else { (void)proc; }
    }
    else if(pj_key_is(j, ks, ke - 1, "scan_negative_invert"))
    {
      out->scan_negative_invert = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "print_timing"))
    {
      out->print_timing = (int)parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "printer_lights_r"))
    {
      out->printer_lights_r = (float)parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "printer_lights_g"))
    {
      out->printer_lights_g = (float)parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "printer_lights_b"))
    {
      out->printer_lights_b = (float)parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "printer_lights_gang"))
    {
      out->printer_lights_gang = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "printer_light_calibration"))
    {
      out->printer_light_calibration = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "camera_uv_filter_enabled"))
    {
      out->camera_uv_filter_enabled = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "camera_uv_cut_nm"))
    {
      out->camera_uv_cut_nm = (float)parse_num(j, &i, end, 410.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "camera_ir_filter_enabled"))
    {
      out->camera_ir_filter_enabled = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "camera_ir_cut_nm"))
    {
      out->camera_ir_cut_nm = (float)parse_num(j, &i, end, 675.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "exposure") ||
            pj_key_is(j, ks, ke - 1, "exposure_ev"))
    {
      out->exposure_ev = parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "print_exposure"))
    {
      out->print_exposure = parse_num(j, &i, end, 1.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "print_auto_exposure"))
    {
      out->print_auto_exposure = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "auto_exposure"))
    {
      out->auto_exposure = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "auto_exposure_method"))
    {
      out->auto_exposure_method = (int)parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "auto_exposure_ev"))
    {
      out->auto_exposure_ev = parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "film_push_pull_stops"))
    {
      out->film_push_pull_stops = parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "film_push_pull_mode"))
    {
      out->film_push_pull_mode = (int)parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "film_gamma"))
    {
      out->film_gamma = parse_num(j, &i, end, 1.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "negative_bleach_bypass_amount"))
    {
      out->negative_bleach_bypass_amount = (float)parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "negative_leuco_cyan_coupling"))
    {
      out->negative_leuco_cyan_coupling = (float)parse_num(j, &i, end, 1.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "print_bleach_bypass_amount"))
    {
      out->print_bleach_bypass_amount = (float)parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "print_gamma"))
    {
      out->print_gamma = parse_num(j, &i, end, 1.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "print_push_pull_stops"))
    {
      out->print_push_pull_stops = parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "print_shadow_shape"))
    {
      out->print_shadow_shape = parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "print_highlight_shape"))
    {
      out->print_highlight_shape = parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "lut_steps"))
    {
      /* 0 = exact spectral; 2..33 = runtime tables. Clamp, don't cast a
       * wild JSON number to a bogus int. */
      double v = parse_num(j, &i, end, 17.0);
      out->lut_steps = (v <= 0.0) ? 0 : (v >= 33.0 ? 33 : (int)v);
    }
    else if(pj_key_is(j, ks, ke - 1, "couplers"))
    {
      out->couplers_active = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
      else if(pj_key_is(j, ks, ke - 1, "coupler_um"))
    {
      out->coupler_diffusion_um = parse_num(j, &i, end, -1.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "coupler_amount"))
    {
      out->coupler_amount = parse_num(j, &i, end, 1.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "coupler_inhibition_samelayer"))
    {
      out->coupler_inhibition_samelayer = parse_num(j, &i, end, 1.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "coupler_inhibition_interlayer"))
    {
      out->coupler_inhibition_interlayer = parse_num(j, &i, end, 1.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "pixel_um"))
    {
      out->pixel_um = parse_num(j, &i, end, 4.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "scan_mtf50_lpmm"))
    {
      out->scan_mtf50_lpmm = parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "scan_usm_radius_um"))
    {
      out->scan_usm_radius_um = parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "film_format_mm"))
    {
      /* Film gauge in mm (0 = use pixel_um directly). The caller supplies the
       * image long edge (image_long_edge_px) before sp_chain_create. */
      double v = parse_num(j, &i, end, 0.0);
      out->film_format_mm = v > 0.0 ? v : 0.0;
    }
    else if(pj_key_is(j, ks, ke - 1, "image_long_edge_px"))
    {
      double v = parse_num(j, &i, end, 0.0);
      out->image_long_edge_px = v > 0.0 ? v : 0.0;
    }
    else if(pj_key_is(j, ks, ke - 1, "input_space"))
    {
      double v = parse_num(j, &i, end, 0.0);
      out->input_space = (v <= 0.0) ? 0 : (int)v;
      if(out->input_space >= sp_color_space_count())
        out->input_space = sp_color_space_count() - 1;
    }
    else if(pj_key_is(j, ks, ke - 1, "input_transfer"))
    {
      double v = parse_num(j, &i, end, 0.0);
      out->input_transfer = (v <= 0.0) ? SP_TRANSFER_SRGB : (int)v;
      if(out->input_transfer >= (int)SP_TRANSFER_COUNT)
        out->input_transfer = (int)SP_TRANSFER_COUNT - 1;
    }
    else if(pj_key_is(j, ks, ke - 1, "rgb_to_raw_method"))
    {
      /* 0 = Hanatos2025, 1 = Mallett2019, 2 = Hanatos2026 (default). Clamp a
       * wild value rather than casting it to a bogus method. */
      double v = parse_num(j, &i, end, 2.0);
      out->rgb_to_raw_method = (v <= 0.0) ? 0 : (v >= 2.0 ? 2 : (int)v);
    }
    else if(pj_key_is(j, ks, ke - 1, "input_custom_xyz_valid"))
    {
      out->input_custom_xyz_valid = (int)parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "input_custom_rgb_to_xyz"))
    {
      pj_u32 k, pos = i;
      pos = pj_ws(j, pos, end);
      if(pos < end && j[pos] == '[')
      {
        pos++;
        for(k = 0; k < 9 && pos < end; k++)
        {
          pos = pj_ws(j, pos, end);
          if(pos < end && (j[pos] == '-' || (j[pos] >= '0' && j[pos] <= '9')))
            pos = pj_number(j, pos, end, (pj_f64 *)&out->input_custom_rgb_to_xyz[k]);
          else out->input_custom_rgb_to_xyz[k] = 0.0;
          pos = pj_ws(j, pos, end);
          if(pos < end && j[pos] == ',') pos++;
        }
        while(pos < end && j[pos] != ']') pos++;
        if(pos < end) pos++;
      }
      i = pos;
    }
    else if(pj_key_is(j, ks, ke - 1, "input_custom_white_xy"))
    {
      pj_u32 k, pos = i;
      pos = pj_ws(j, pos, end);
      if(pos < end && j[pos] == '[')
      {
        pos++;
        for(k = 0; k < 2 && pos < end; k++)
        {
          pos = pj_ws(j, pos, end);
          if(pos < end && (j[pos] == '-' || (j[pos] >= '0' && j[pos] <= '9')))
            pos = pj_number(j, pos, end, (pj_f64 *)&out->input_custom_white_xy[k]);
          else out->input_custom_white_xy[k] = 0.0;
          pos = pj_ws(j, pos, end);
          if(pos < end && j[pos] == ',') pos++;
        }
        while(pos < end && j[pos] != ']') pos++;
        if(pos < end) pos++;
      }
      i = pos;
    }
    else if(pj_key_is(j, ks, ke - 1, "boost_ev")) out->boost_ev = (float)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "boost_range")) out->boost_range = (float)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "protect_ev")) out->protect_ev = (float)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "diffusion"))
    {
      out->diffusion_on = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "diffusion_family")) out->diffusion_family = (int)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "diffusion_strength")) out->diffusion_strength = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "diffusion_scale")) out->diffusion_scale = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "diffusion_warmth")) out->diffusion_warmth = (float)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "diffusion_core_intensity")) out->diffusion_core_intensity = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "diffusion_core_size")) out->diffusion_core_size = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "diffusion_halo_intensity")) out->diffusion_halo_intensity = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "diffusion_halo_size")) out->diffusion_halo_size = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "diffusion_bloom_intensity")) out->diffusion_bloom_intensity = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "diffusion_bloom_size")) out->diffusion_bloom_size = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "halation"))
    {
      out->halation_on = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "scatter_amount")) out->scatter_amount = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "scatter_scale")) out->scatter_scale = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "halation_amount")) out->halation_amount = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "halation_scale")) out->halation_scale = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain"))
    {
      out->grain_on = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "grain_amount")) out->grain_amount = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_saturation")) out->grain_saturation = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_seed")) out->grain_seed = (int)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_micro_structure_scale")) out->grain_micro_structure_scale = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_micro_structure_sigma_nm")) out->grain_micro_structure_sigma_nm = (float)parse_num(j, &i, end, 30.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_blur")) out->grain_blur = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_granularity")) out->grain_granularity = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_uniformity")) out->grain_uniformity = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_sublayer_scale")) out->grain_sublayer_scale = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_density_min")) out->grain_density_min = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_dye_cloud")) out->grain_dye_cloud = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_usm_sigma")) out->grain_usm_sigma = (float)parse_num(j, &i, end, 0.7);
    else if(pj_key_is(j, ks, ke - 1, "grain_usm_amount")) out->grain_usm_amount = (float)parse_num(j, &i, end, 1.5);
    /* Grain surface (OFX GrainParams): model, sub-layer split and the
     * per-channel/per-layer overrides. Defaults reproduce the stock's own
     * figures, so an absent key is a no-op. */
    else if(pj_key_is(j, ks, ke - 1, "grain_model")) out->grain_model = (int)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_sublayers_enabled"))
    {
      out->grain_sublayers_enabled = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "grain_sublayer_count")) out->grain_sublayer_count = (int)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_particle_scale_r")) out->grain_particle_scale[0] = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_particle_scale_g")) out->grain_particle_scale[1] = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_particle_scale_b")) out->grain_particle_scale[2] = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_particle_scale_layer_0")) out->grain_particle_scale_layer[0] = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_particle_scale_layer_1")) out->grain_particle_scale_layer[1] = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_particle_scale_layer_2")) out->grain_particle_scale_layer[2] = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_uniformity_r")) out->grain_uniformity_rgb[0] = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_uniformity_g")) out->grain_uniformity_rgb[1] = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_uniformity_b")) out->grain_uniformity_rgb[2] = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_density_min_r")) out->grain_density_min_rgb[0] = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_density_min_g")) out->grain_density_min_rgb[1] = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_density_min_b")) out->grain_density_min_rgb[2] = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_final_blur_um")) out->grain_final_blur_um = (float)parse_num(j, &i, end, -1.0);
    else if(pj_key_is(j, ks, ke - 1, "grain_dye_cloud_um")) out->grain_dye_cloud_um = (float)parse_num(j, &i, end, -1.0);
    else if(pj_key_is(j, ks, ke - 1, "print_diffusion"))
    {
      out->print_diffusion_on = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "print_diffusion_family")) out->print_diffusion_family = (int)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "print_diffusion_strength")) out->print_diffusion_strength = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "print_diffusion_scale")) out->print_diffusion_scale = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "print_diffusion_warmth")) out->print_diffusion_warmth = (float)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "print_diffusion_core_intensity")) out->print_diffusion_core_intensity = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "print_diffusion_core_size")) out->print_diffusion_core_size = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "print_diffusion_halo_intensity")) out->print_diffusion_halo_intensity = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "print_diffusion_halo_size")) out->print_diffusion_halo_size = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "print_diffusion_bloom_intensity")) out->print_diffusion_bloom_intensity = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "print_diffusion_bloom_size")) out->print_diffusion_bloom_size = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "scan_blur")) out->scan_blur = (float)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "scan_usm_sigma")) out->scan_usm_sigma = (float)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "scan_usm_amount")) out->scan_usm_amount = (float)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "glare_percent")) out->glare_percent = (float)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "scan_glare_roughness")) out->scan_glare_roughness = (float)parse_num(j, &i, end, 0.7);
    else if(pj_key_is(j, ks, ke - 1, "scan_glare_blur")) out->scan_glare_blur = (float)parse_num(j, &i, end, 0.5);
    else if(pj_key_is(j, ks, ke - 1, "dir_gamma_same_r")) out->dir_gamma_same[0] = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "dir_gamma_same_g")) out->dir_gamma_same[1] = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "dir_gamma_same_b")) out->dir_gamma_same[2] = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "dir_gamma_r_gb_g")) out->dir_gamma_r_gb[0] = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "dir_gamma_r_gb_b")) out->dir_gamma_r_gb[1] = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "dir_gamma_g_rb_r")) out->dir_gamma_g_rb[0] = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "dir_gamma_g_rb_b")) out->dir_gamma_g_rb[1] = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "dir_gamma_b_rg_r")) out->dir_gamma_b_rg[0] = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "dir_gamma_b_rg_g")) out->dir_gamma_b_rg[1] = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "filter_m_shift")) out->filter_m_shift = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "filter_y_shift")) out->filter_y_shift = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "filter_c_shift")) out->filter_c_shift = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "negative_bleach_bypass_amount")) out->negative_bleach_bypass_amount = (float)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "negative_leuco_cyan_coupling")) out->negative_leuco_cyan_coupling = (float)parse_num(j, &i, end, 1.0);
    else if(pj_key_is(j, ks, ke - 1, "print_bleach_bypass_amount")) out->print_bleach_bypass_amount = (float)parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "preflash_exposure")) out->preflash_exposure = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "preflash_m_shift")) out->preflash_m_shift = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "preflash_y_shift")) out->preflash_y_shift = parse_num(j, &i, end, 0.0);
    else if(pj_key_is(j, ks, ke - 1, "halation_strength_r")) out->halation_strength[0] = parse_num(j, &i, end, -1.0);
    else if(pj_key_is(j, ks, ke - 1, "halation_strength_g")) out->halation_strength[1] = parse_num(j, &i, end, -1.0);
    else if(pj_key_is(j, ks, ke - 1, "halation_strength_b")) out->halation_strength[2] = parse_num(j, &i, end, -1.0);
    else if(pj_key_is(j, ks, ke - 1, "halation_first_sigma_um_r")) out->halation_first_sigma_um[0] = parse_num(j, &i, end, -1.0);
    else if(pj_key_is(j, ks, ke - 1, "halation_first_sigma_um_g")) out->halation_first_sigma_um[1] = parse_num(j, &i, end, -1.0);
    else if(pj_key_is(j, ks, ke - 1, "halation_first_sigma_um_b")) out->halation_first_sigma_um[2] = parse_num(j, &i, end, -1.0);
    else if(pj_key_is(j, ks, ke - 1, "halation_sigma_max_um")) out->halation_sigma_max_um = parse_num(j, &i, end, -1.0);
    else if(pj_key_is(j, ks, ke - 1, "scan_black_correction"))
    {
      out->scan_black_correction = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "scan_white_correction"))
    {
      out->scan_white_correction = (i < end && j[i] == 't') ? 1 : 0;
      i = pj_skip_value(j, i, end);
    }
    else if(pj_key_is(j, ks, ke - 1, "scan_black_level")) out->scan_black_level = parse_num(j, &i, end, 0.01);
    else if(pj_key_is(j, ks, ke - 1, "scan_white_level")) out->scan_white_level = parse_num(j, &i, end, 0.98);
    else if(pj_key_is(j, ks, ke - 1, "development_min"))
    {
      out->development_min = (float)parse_num(j, &i, end, 0.0);
    }
    else if(pj_key_is(j, ks, ke - 1, "pack_dir"))
    {
      char dir[64] = { 0 };
      if(i < end && j[i] == '\"') i = pj_string(j, i, end, dir, sizeof dir);
      else i = pj_skip_value(j, i, end);
      if(dir[0])
      {
        static char g_dir[64];
        strcpy(g_dir, dir);
        out->pack_dir = g_dir;
      }
    }
    else
    {
      i = pj_skip_value(j, i, end); /* e.g. the host's _image {…} */
    }
    i = pj_ws(j, i, end);
    if(i < end && j[i] == ',') i = pj_ws(j, i + 1, end);
  }
  return 0;
}