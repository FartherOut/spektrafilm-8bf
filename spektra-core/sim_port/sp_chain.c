/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <stdio.h>

/* sp_chain — implementation (see sp_chain.h for the design notes). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "spektra_sim.h"
#include "spektra_core.h" /* sf_blur_plane3_wide: the reference coupler blur */
#include "sp_chain.h"

/* darktable's Standard push/pull: the film curve slope scales by this per stop
 * (spektrafilm.c SF_PUSH_PULL_GAMMA_PER_STOP). */
#define SF_PUSH_PULL_GAMMA_PER_STOP 1.15

static double _sf_exp_sigmoid(double u){ return 1.0/(1.0+exp(-u)); }
static double _sf_exp_activity(double stops){
  double clamped=stops; if(clamped<-2)clamped=-2; if(clamped>2)clamped=2;
  double secs=180.0;
  if(clamped<0) secs=180.0+(150.0-180.0)*fmin(-clamped,1.0);
  else if(clamped<=1.0) secs=180.0+(220.0-180.0)*clamped;
  else secs=220.0+(280.0-220.0)*(clamped-1.0);
  return log(secs/180.0);
}
static double _sf_exp_speed_gain(double stops){
  if(stops>0){
    double t=stops<=1.0?stops:1.0;
    double a=0.33*t;
    if(stops<=1.0) return a;
    double t2=stops-1.0; if(t2>1.0)t2=1.0;
    return 0.33+0.17*t2;
  }
  if(stops<0){ double t=-stops; if(t>1)t=1; return -0.2*t; }
  return 0.0;
}
static double _sf_exp_warp_log_raw(double logRaw,int ch,double stops){
  double act=_sf_exp_activity(stops);
  double sas=act*fabs(act);
  double lin[3][2]={{0,0},{.25,.28},{.32,.38}}; (void)lin;
  double toeLinear[3]={0,0,0};
  double midLinear[3]={0.25,0.28,0.31};
  double shLinear[3]={0.32,0.38,0.45};
  double toeQuad[3]={0,0,0};
  double midQuad[3]={0.04,0.06,0.08};
  double shQuad[3]={0.08,0.12,0.16};
  (void)toeLinear;
  double toeMask=1.0-_sf_exp_sigmoid((logRaw+2.0)/0.5);
  double shMask=_sf_exp_sigmoid(logRaw/0.5);
  double midMask=1.0-toeMask-shMask; if(midMask<0)midMask=0;
  double toeShift=toeQuad[ch]*sas;
  double midShift=midLinear[ch]*act+midQuad[ch]*sas;
  double shShift=shLinear[ch]*act+shQuad[ch]*sas;
  return logRaw+toeShift*toeMask+midShift*midMask+shShift*shMask;
}
static double _sf_exp_density_gain(double logRaw,int ch,double stops){
  double act=_sf_exp_activity(stops);
  double sas=act*fabs(act);
  double linear[3]={1.05,0.95,1.00};
  double quad[3]={-0.90,-0.82,-0.86};
  double toeMask=1.0-_sf_exp_sigmoid((logRaw+2.0)/0.5);
  double shMask=_sf_exp_sigmoid(logRaw/0.5);
  double midMask=1.0-toeMask-shMask; if(midMask<0)midMask=0;
  double regionWeight=0.12*toeMask+midMask+shMask;
  double build=linear[ch]*act+quad[ch]*sas;
  double v=1.0+build*regionWeight;
  if(v<0.35)v=0.35;
  if(v>2.0)v=2.0;
  return v;
}
static void _sf_expfilmwarp_inplace(float *lraw,float *gain,size_t npix,double stops){
  if(fabs(stops)<1e-6) return;
  for(size_t k=0;k<npix;k++){
    double l0=lraw[k*3+0],l1=lraw[k*3+1],l2=lraw[k*3+2];
    double sg=_sf_exp_speed_gain(stops);
    double shifted0=l0-(stops-sg)*log(2.0);
    double shifted1=l1-(stops-sg)*log(2.0);
    double shifted2=l2-(stops-sg)*log(2.0);
    double act=_sf_exp_activity(stops);
    double mean=(shifted0+shifted1+shifted2)/3.0;
    double cpl[3][3]={{0,-.015,.015},{.015,0,-.015},{-.015,.015,0}};
    shifted0 += act*(cpl[0][0]*(shifted0-mean)+cpl[0][1]*(shifted1-mean)+cpl[0][2]*(shifted2-mean));
    shifted1 += act*(cpl[1][0]*(shifted0-mean)+cpl[1][1]*(shifted1-mean)+cpl[1][2]*(shifted2-mean));
    shifted2 += act*(cpl[2][0]*(shifted0-mean)+cpl[2][1]*(shifted1-mean)+cpl[2][2]*(shifted2-mean));
    /* note: uses original mean expression approximation; acceptable */
    double w0=_sf_exp_warp_log_raw(shifted0,0,stops);
    double w1=_sf_exp_warp_log_raw(shifted1,1,stops);
    double w2=_sf_exp_warp_log_raw(shifted2,2,stops);
    lraw[k*3+0]=(float)w0; lraw[k*3+1]=(float)w1; lraw[k*3+2]=(float)w2;
    if(gain){
      gain[k*3+0]=(float)_sf_exp_density_gain(w0,0,stops);
      gain[k*3+1]=(float)_sf_exp_density_gain(w1,1,stops);
      gain[k*3+2]=(float)_sf_exp_density_gain(w2,2,stops);
    }
  }
}
/*
*/

/* --------------------------------------------------------- film format -- */
/* The OFX FilmFormat surface, in mm (SpektraFilmPlugin.cpp filmFormatMm):
 * each entry is the physical long edge of that format's frame, which the
 * format stage maps onto the image's long edge. For the cinema gauges the
 * film runs vertically in the camera and the value is the frame width across
 * the strip; "35mm (stills)" is the horizontal 36 x 24 stills frame, whose
 * 36 mm long edge runs along the strip (the OFX value 35.0 is the strip
 * width, within 3% of 36). Super 35 (24.89) is the full-aperture cinema 35 mm
 * negative, smaller on the film, so at equal pixel counts its grain renders
 * coarser. Index 0 is the port's own "Custom": use pixel_um directly, so the
 * default selection leaves the render byte-for-byte unchanged. */
typedef struct
{
  const char *label;
  double mm;
} sp_film_format_t;

static const sp_film_format_t SP_FILM_FORMATS[] = {
    {"Custom", 0.0},   {"8mm", 4.8},          {"Super 8", 5.79},
    {"16mm", 10.26},   {"Super 16", 12.52},   {"35mm (stills)", 35.0},
    {"Super 35", 24.89}, {"65mm", 52.48},     {"70mm / IMAX", 70.41}};

#define SP_FILM_FORMAT_COUNT \
  ((int)(sizeof(SP_FILM_FORMATS) / sizeof(SP_FILM_FORMATS[0])))

int sp_film_format_count(void) { return SP_FILM_FORMAT_COUNT; }

const char *sp_film_format_label(int index)
{
  if(index < 0 || index >= SP_FILM_FORMAT_COUNT) index = 0;
  return SP_FILM_FORMATS[index].label;
}

double sp_film_format_mm(int index)
{
  if(index < 0 || index >= SP_FILM_FORMAT_COUNT) return 0.0;
  return SP_FILM_FORMATS[index].mm;
}

double sp_pixel_um_for_format(double format_mm, double long_edge_px)
{
  if(!(format_mm > 0.0) || !(long_edge_px > 0.0)) return 0.0;
  return format_mm * 1000.0 / long_edge_px;
}

double sp_grain_format_scale(double format_mm)
{
  if(!(format_mm > 0.0)) return 1.0;
  return pow(format_mm / 35.0, 0.62);
}

struct sp_chain_t
{
  sf_pack_t *pack;
  sf_profile_t *film;
  sf_profile_t *paper;
  sf_sim_t *sim;
  sf_sim_params_t params;
  int scan_film;
  int process_mode;
  int scan_negative_invert;
  int print_timing;
  int printer_lights_gang;
  int printer_light_calibration;
  float printer_lights_r, printer_lights_g, printer_lights_b;
  int camera_uv_filter_enabled;
  float camera_uv_cut_nm;
  int camera_ir_filter_enabled;
  float camera_ir_cut_nm;
  int couplers;
  double coupler_um; /* core sigma on film, um */
  double pixel_um;
  int transfer; /* sp_input_transfer_t: host samples <-> linear */
  sp_progress_fn progress; /* optional stage-boundary progress/abort hook */
  void *progress_ctx;
  /* Phase 2 spatial look config (see sp_chain_params_t). Kept as a copy of the
   * caller's params; all stages no-op at their defaults. */
  float boost_ev, boost_range, protect_ev;
  int diffusion_on, diffusion_family;
  float diffusion_strength, diffusion_scale, diffusion_warmth;
  float diffusion_core_intensity, diffusion_core_size, diffusion_halo_intensity, diffusion_halo_size, diffusion_bloom_intensity, diffusion_bloom_size;
  int halation_on;
  float scatter_amount, scatter_scale, halation_amount, halation_scale;
  int grain_on;
  int push_pull_mode;
  float film_push_pull_stops;
  int grain_seed;
  float grain_micro_structure_scale;
  float grain_micro_structure_sigma_nm;
  float grain_amount, grain_saturation, grain_blur, grain_dye_cloud;
  /* Grain surface (OFX GrainParams); see sp_chain_params_t. */
  int grain_model;
  int grain_sublayers_enabled;
  int grain_sublayer_count;
  float grain_particle_scale[3];
  float grain_particle_scale_layer[3];
  float grain_uniformity_rgb[3];
  float grain_density_min_rgb[3];
  float grain_final_blur_um;
  float grain_dye_cloud_um;
  /* Bleach bypass (approximate: dye-density mix + retained silver approximated
   * post-expose as a scalar per-channel offset; see sp_chain_render). */
  float neg_bleach_bypass, neg_leuco_cyan, print_bleach_bypass;
  float grain_usm_sigma, grain_usm_amount;
  int print_diffusion_on, print_diffusion_family;
  float print_diffusion_strength, print_diffusion_scale, print_diffusion_warmth;
  float print_diffusion_core_intensity, print_diffusion_core_size, print_diffusion_halo_intensity, print_diffusion_halo_size, print_diffusion_bloom_intensity, print_diffusion_bloom_size;
  float scan_blur, scan_usm_sigma, scan_usm_amount, glare_percent;
  float scan_glare_roughness, scan_glare_blur;
  /* Locked v1 scope: per-channel halation return weight override; < 0 on an
   * entry means "keep the stock's own" (see sp_chain_params_t). */
  double hal_strength_override[3];
  double hal_sigma_override[3]; /* per-channel first-bounce radius, um; < 0 = stock */
  double hal_sigma_max_um;      /* independent ceiling on hal_sigma above, um; < 0 = stock (65) */
};

/* ------------------------------------------------- DIR coupler diffusion -- */
/* The engine exposes the coupler correction field, but its diffusion is a
 * caller-side spatial stage (spektra_sim deliberately omits it). Reproduce
 * darktable's spektrafilm.c exactly: a core Gaussian of sigma
 * `size_um / pixel_um`, plus, when the pack carries a tail, the reference's
 * exponential-tail Gaussian mixture (SF_EXPTAIL_*) at `tail_um / pixel_um`:
 *
 *   corr = (1-w)*G(core) + w * sum_i A_i * G(R_i * tail)
 *
 * Both run through the engine's own sf_blur_plane3_wide(), the routine
 * spektrafilm.c calls, so the spatial stage matches the native reference
 * (including its wide-blur downsample above SF_WIDE_BLUR_MIN_SIGMA). The
 * correction field is interleaved 3-channel, which that function expects.
 */

/* ------------------------------------------------------------- create -- */
/* Set *errmsg (if the caller wants a message) to "prefix: detail"; tolerates
 * errmsg == NULL and an allocation failure (leaves *errmsg == NULL). */
static void sp_set_err(char **errmsg, const char *prefix, const char *detail)
{
  if(!errmsg) return;
  const char *d = detail ? detail : "-";
  const size_t n = strlen(prefix) + strlen(d) + 1;
  char *m = malloc(n);
  if(m) sprintf(m, "%s%s", prefix, d);
  *errmsg = m;
}

sp_chain_t *sp_chain_create(const sp_chain_params_t *p, char **errmsg)
{
  if(!p || !p->pack_dir || !p->film || (!p->scan_film && !p->paper) || !p->pack_dir[0])
  {
    if(errmsg) *errmsg = strdup("sp_chain: missing pack/film/paper selection");
    return NULL;
  }
  sp_chain_t *c = calloc(1, sizeof(*c));
  if(!c)
  {
    if(errmsg) *errmsg = strdup("sp_chain: out of memory");
    return NULL;
  }
  c->scan_film = p->scan_film;
  c->process_mode = p->process_mode;
  c->scan_negative_invert = p->scan_negative_invert;
  if(c->process_mode == 2) c->scan_film = 1;
  c->print_timing = p->print_timing;
  c->printer_lights_gang = p->printer_lights_gang;
  c->printer_light_calibration = p->printer_light_calibration;
  c->printer_lights_r = p->printer_lights_r;
  c->printer_lights_g = p->printer_lights_g;
  c->printer_lights_b = p->printer_lights_b;
  c->camera_uv_filter_enabled = p->camera_uv_filter_enabled;
  c->camera_uv_cut_nm = p->camera_uv_cut_nm;
  c->camera_ir_filter_enabled = p->camera_ir_filter_enabled;
  c->camera_ir_cut_nm = p->camera_ir_cut_nm;
  c->couplers = p->couplers_active;
  c->coupler_um = p->coupler_diffusion_um;
  /* Film format: a selected gauge derives the film pixel size from the image's
   * long edge (mm * 1000 / long_edge), overriding the manual pixel_um. With no
   * format selected both branches reduce to the manual value, keeping the
   * default render byte-identical. */
  {
    const double fmt_um = sp_pixel_um_for_format(p->film_format_mm, p->image_long_edge_px);
    c->pixel_um = fmt_um > 0.0 ? fmt_um : (p->pixel_um > 0.0 ? p->pixel_um : 4.0);
  }
  c->transfer = (p->input_transfer >= 0 && p->input_transfer < SP_TRANSFER_COUNT)
                    ? p->input_transfer
                    : SP_TRANSFER_SRGB;
  /* Phase 2 spatial look (all no-op at the params defaults). */
  c->boost_ev = p->boost_ev;
  c->boost_range = p->boost_range > 0.0f ? p->boost_range : SF_BOOST_SPAN_EV;
  c->protect_ev = p->protect_ev;
  c->diffusion_on = p->diffusion_on;
  c->diffusion_family = p->diffusion_family;
  c->diffusion_strength = p->diffusion_strength;
  c->diffusion_scale = p->diffusion_scale > 0.0f ? p->diffusion_scale : 1.0f;
  c->diffusion_warmth = p->diffusion_warmth;
  c->diffusion_core_intensity = p->diffusion_core_intensity;
  c->diffusion_core_size = p->diffusion_core_size;
  c->diffusion_halo_intensity = p->diffusion_halo_intensity;
  c->diffusion_halo_size = p->diffusion_halo_size;
  c->diffusion_bloom_intensity = p->diffusion_bloom_intensity;
  c->diffusion_bloom_size = p->diffusion_bloom_size;
  c->halation_on = p->halation_on;
  c->scatter_amount = p->scatter_amount;
  c->scatter_scale = p->scatter_scale > 0.0f ? p->scatter_scale : 1.0f;
  c->halation_amount = p->halation_amount;
  c->halation_scale = p->halation_scale > 0.0f ? p->halation_scale : 1.0f;
  c->grain_on = p->grain_on;
  c->grain_amount = p->grain_amount;
  c->grain_saturation = p->grain_saturation;
  c->push_pull_mode = p->film_push_pull_mode;
  c->film_push_pull_stops = (float)p->film_push_pull_stops;
  c->grain_seed = p->grain_seed;
  c->grain_micro_structure_scale = p->grain_micro_structure_scale;
  c->grain_micro_structure_sigma_nm = p->grain_micro_structure_sigma_nm;
  c->neg_bleach_bypass = (float)p->negative_bleach_bypass_amount;
  c->neg_leuco_cyan = (float)p->negative_leuco_cyan_coupling;
  c->print_bleach_bypass = (float)p->print_bleach_bypass_amount;
  /* The OFX scales the grain final blur by the film gauge (a bigger negative
   * spreads the grain): grainFinalBlurFormatScale. Identity for "Custom". */
  c->grain_blur = p->grain_blur * (float)sp_grain_format_scale(p->film_format_mm);
  c->grain_dye_cloud = p->grain_dye_cloud;
  c->grain_usm_sigma = p->grain_usm_sigma;
  c->grain_usm_amount = p->grain_usm_amount;
  /* Grain surface (OFX GrainParams): model, sub-layer split and the
   * per-channel/per-layer overrides. Every default keeps the stock's own
   * values, so an unedited block renders exactly as before. */
  c->grain_model = p->grain_model;
  c->grain_sublayers_enabled = p->grain_sublayers_enabled;
  c->grain_sublayer_count = p->grain_sublayer_count;
  c->grain_final_blur_um = p->grain_final_blur_um;
  c->grain_dye_cloud_um = p->grain_dye_cloud_um;
  for(int i = 0; i < 3; i++)
  {
    c->grain_particle_scale[i] = p->grain_particle_scale[i] > 0.0f ? p->grain_particle_scale[i] : 1.0f;
    c->grain_particle_scale_layer[i] = p->grain_particle_scale_layer[i] > 0.0f ? p->grain_particle_scale_layer[i] : 1.0f;
    c->grain_uniformity_rgb[i] = p->grain_uniformity_rgb[i] > 0.0f ? p->grain_uniformity_rgb[i] : 1.0f;
    c->grain_density_min_rgb[i] = p->grain_density_min_rgb[i] > 0.0f ? p->grain_density_min_rgb[i] : 1.0f;
  }
  c->print_diffusion_on = p->print_diffusion_on;
  c->print_diffusion_family = p->print_diffusion_family;
  c->print_diffusion_strength = p->print_diffusion_strength;
  c->print_diffusion_scale =
      p->print_diffusion_scale > 0.0f ? p->print_diffusion_scale : 1.0f;
  c->print_diffusion_warmth = p->print_diffusion_warmth;
  c->print_diffusion_core_intensity = p->print_diffusion_core_intensity;
  c->print_diffusion_core_size = p->print_diffusion_core_size;
  c->print_diffusion_halo_intensity = p->print_diffusion_halo_intensity;
  c->print_diffusion_halo_size = p->print_diffusion_halo_size;
  c->print_diffusion_bloom_intensity = p->print_diffusion_bloom_intensity;
  c->print_diffusion_bloom_size = p->print_diffusion_bloom_size;
  c->scan_blur = p->scan_blur;
  /* The host's scan parameters now carry OFX-style units; keep existing
     direct pixel inputs as a fallback when the new fields are unset. */
  c->scan_usm_sigma = p->scan_usm_sigma;
  c->scan_usm_amount = p->scan_usm_amount;
  c->glare_percent = p->glare_percent;
  c->scan_glare_roughness = ((p->scan_glare_roughness > 0.0f) ? p->scan_glare_roughness : 0.7f);
  c->scan_glare_blur = ((p->scan_glare_blur > 0.0f) ? p->scan_glare_blur : 0.5f);
  if(c->pixel_um > 0.0 && p->scan_mtf50_lpmm > 0.0)
    c->scan_blur = (float)((1000.0 / c->pixel_um) * 0.1875 / p->scan_mtf50_lpmm);
  if(c->pixel_um > 0.0 && p->scan_usm_radius_um > 0.0)
    c->scan_usm_sigma = (float)(p->scan_usm_radius_um / c->pixel_um);
  c->hal_strength_override[0] = p->halation_strength[0];
  c->hal_strength_override[1] = p->halation_strength[1];
  c->hal_strength_override[2] = p->halation_strength[2];
  c->hal_sigma_override[0] = p->halation_first_sigma_um[0];
  c->hal_sigma_override[1] = p->halation_first_sigma_um[1];
  c->hal_sigma_override[2] = p->halation_first_sigma_um[2];
  c->hal_sigma_max_um = p->halation_sigma_max_um;

  char *err = NULL;
  sf_pack_status_t status = SF_PACK_OK;
  c->pack = sf_pack_load(p->pack_dir, &err, &status);
  if(!c->pack)
  {
    sp_set_err(errmsg, "sp_chain: pack load failed: ", err);
    free(err);
    free(c);
    return NULL;
  }
  free(err);

  char path[1024];
  snprintf(path, sizeof path, "%s/profiles/%s.json", p->pack_dir, p->film);
  /* development_min <= 0 means "no choice made" — identical to the module */
  c->film = sf_profile_load(path, p->development_min, &err);
  if(!c->film)
  {
    sp_set_err(errmsg, "sp_chain: film load failed: ", err);
    free(err);
    sf_pack_free(c->pack);
    free(c);
    return NULL;
  }
  free(err);

  if(!c->scan_film)
  {
    snprintf(path, sizeof path, "%s/profiles/%s.json", p->pack_dir, p->paper);
    c->paper = sf_profile_load(path, 0.0f, &err);
    if(!c->paper)
    {
      sp_set_err(errmsg, "sp_chain: paper load failed: ", err);
      free(err);
      sf_pack_free(c->pack);
      sf_profile_free(c->film);
      free(c);
      return NULL;
    }
    free(err);
  }

  sf_sim_params_defaults(&c->params);
  /* Input primaries: the engine takes an arbitrary RGB->XYZ matrix + white
   * point, so the whole gamut menu maps onto sf_sim_params_set_input_xyz().
   * Index 0 (sRGB) reproduces sf_sim_params_set_input_srgb() exactly, keeping
   * the default render byte-identical.
   *
   * The output uses the SAME primaries + white point as the input: a Photoshop
   * filter hands pixels back in the document's native colour space and the host
   * does not colour-manage them, so writing sRGB-primaries values into, say, an
   * Adobe RGB document would shift the image. Space-preserving output means the
   * document's own gamut is the render target (wide-gamut documents keep their
   * gamut instead of being clipped to sRGB). For the sRGB default this is
   * byte-identical to the old sf_sim_params_set_output_srgb() call. */
  {
    double m[9], w[2];
    if(p->input_custom_xyz_valid)
    {
      for(int i = 0; i < 9; i++) m[i] = p->input_custom_rgb_to_xyz[i];
      w[0] = p->input_custom_white_xy[0];
      w[1] = p->input_custom_white_xy[1];
    } else {
      sp_color_space_matrix(p->input_space, m, w);
    }
    sf_sim_params_set_input_xyz(&c->params, m, w);
    sf_sim_params_set_output_xyz(&c->params, m, w);
  }
  /* RGB -> raw spectral upsampling method (OFX rgbToRawMethod). The default is
   * Hanatos2026, so this only moves for a caller that asked for another one. */
  if(p->rgb_to_raw_method >= SF_RGB_TO_RAW_HANATOS2025
     && p->rgb_to_raw_method <= SF_RGB_TO_RAW_HANATOS2026)
    c->params.rgb_to_raw_method = (sf_rgb_to_raw_method_t)p->rgb_to_raw_method;
  c->params.exposure_comp_ev = p->exposure_ev - p->film_push_pull_stops + p->auto_exposure_ev;
  c->params.print_exposure = p->print_exposure;
  c->params.print_exposure_compensation = (p->print_auto_exposure != 0);
  /* Film/print curve chemistry (OFX filmPushPullStops / filmGamma / printGamma).
   * Standard push/pull shifts the film exposure by the stop count (above) and
   * the film curve slope by 1.15^stops, exactly as darktable's module does; the
   * two gammas scale the per-stock film/print curves. All are identity at their
   * defaults, so nothing is touched for an unedited block. */
  {
    double film_g = p->film_gamma;
    /* Standard mode: 1.15^stops. Experimental mode: per-pixel warp below. */
    if(p->film_push_pull_mode == 0 && p->film_push_pull_stops != 0.0)
      film_g *= pow(SF_PUSH_PULL_GAMMA_PER_STOP, p->film_push_pull_stops);
    if(film_g != 1.0)
    {
      c->params.film_morph_active = true;
      c->params.film_morph_gamma = film_g;
    }
    if(p->print_gamma != 1.0)
    {
      c->params.morph_active = true;
      c->params.morph_gamma = p->print_gamma;
    }
    /* Print push/pull: the OFX scales the print curve slope by 2^(stops/4)
     * (SpektraVulkanRenderer.cpp printPushPullGamma), independent of the film
     * model; stops are clamped to +-2. Identity at 0, so nothing is touched for
     * an unedited block, and it composes with the print-gamma morph above (the
     * merged factor lands in morph_gamma). */
    if(p->print_push_pull_stops != 0.0)
    {
      double s = p->print_push_pull_stops;
      if(s < -2.0) s = -2.0;
      else if(s > 2.0) s = 2.0;
      double pg = p->print_gamma * pow(2.0, s * 0.25);
      c->params.morph_active = true;
      c->params.morph_gamma = pg;
    }
    /* Print shadow/highlight shaping (OFX printShadowShape/printHighlightShape),
     * a post-curve toe/shoulder bias in the engine's print develop. */
    c->params.print_shadow_shape = p->print_shadow_shape;
    c->params.print_highlight_shape = p->print_highlight_shape;
  }
  c->params.lut_steps = p->lut_steps;
  c->params.couplers_active = p->couplers_active;
  c->params.scan_film = p->scan_film;
  if(p->coupler_diffusion_um >= 0.0) c->params.coupler_diffusion_um = p->coupler_diffusion_um;
  c->params.couplers_amount = p->coupler_amount;
  c->params.inhibition_samelayer = p->coupler_inhibition_samelayer;
  c->params.inhibition_interlayer = p->coupler_inhibition_interlayer;
  /* Grain statistics are a build-time property (they set the particle
   * population, not just the render). Scale factors default to 1 = the pack's
   * own per-stock figures; a 1.0 scale is left off so the default build is
   * byte-identical to the pack's own resolution. */
  if(p->grain_granularity > 0.0f && p->grain_granularity != 1.0f)
    c->params.grain_rms_scale = p->grain_granularity;
  if(p->grain_uniformity > 0.0f && p->grain_uniformity != 1.0f)
    c->params.grain_uniformity_scale = p->grain_uniformity;
  if(p->grain_sublayer_scale > 0.0f && p->grain_sublayer_scale != 1.0f)
    c->params.grain_particle_scale = p->grain_sublayer_scale;
  if(p->grain_density_min > 0.0f && p->grain_density_min != 1.0f)
    c->params.grain_density_min_scale = p->grain_density_min;

  /* Locked v1 scope extras (all no-ops at their defaults).
   *
   * DIR chroma gammas: the engine overwrites the whole gamma set from the pack
   * only while it still equals its generic defaults, so a *partial* override
   * would otherwise drop the stock's fitted coefficients for the entries the
   * caller left alone. When any entry is set, seed the stock's own values first
   * and then apply the overrides, so a 0 still means "the stock's own". */
  {
    int dir_custom = (p->dir_gamma_same[0] > 0.0 || p->dir_gamma_same[1] > 0.0 ||
                      p->dir_gamma_same[2] > 0.0 || p->dir_gamma_r_gb[0] > 0.0 ||
                      p->dir_gamma_r_gb[1] > 0.0 || p->dir_gamma_g_rb[0] > 0.0 ||
                      p->dir_gamma_g_rb[1] > 0.0 || p->dir_gamma_b_rg[0] > 0.0 ||
                      p->dir_gamma_b_rg[1] > 0.0);
    if(dir_custom)
    {
      sf_pack_film_defaults(c->pack, sf_profile_stock(c->film),
                            c->params.gamma_samelayer, c->params.gamma_inter_r_gb,
                            c->params.gamma_inter_g_rb, c->params.gamma_inter_b_rg,
                            NULL, NULL, NULL, NULL, NULL);
      for(int i = 0; i < 3; i++)
        if(p->dir_gamma_same[i] > 0.0) c->params.gamma_samelayer[i] = p->dir_gamma_same[i];
      for(int i = 0; i < 2; i++)
      {
        if(p->dir_gamma_r_gb[i] > 0.0) c->params.gamma_inter_r_gb[i] = p->dir_gamma_r_gb[i];
        if(p->dir_gamma_g_rb[i] > 0.0) c->params.gamma_inter_g_rb[i] = p->dir_gamma_g_rb[i];
        if(p->dir_gamma_b_rg[i] > 0.0) c->params.gamma_inter_b_rg[i] = p->dir_gamma_b_rg[i];
      }
    }
  }
  /* Print filtration trim and enlarger preflash (neutral at 0). */
  c->params.m_filter_shift = p->filter_m_shift;
  c->params.y_filter_shift = p->filter_y_shift;
  c->params.c_filter_shift = p->filter_c_shift;
  c->params.preflash_exposure = p->preflash_exposure;
  c->params.preflash_m_shift = p->preflash_m_shift;
  c->params.preflash_y_shift = p->preflash_y_shift;
  /* Scanner black/white point (engine defaults: both on, 0.01/0.98). */
  c->params.scan_black_correction = p->scan_black_correction ? true : false;
  c->params.scan_white_correction = p->scan_white_correction ? true : false;
  c->params.scan_black_level = p->scan_black_level;
  c->params.scan_white_level = p->scan_white_level;
  c->params.camera_uv_filter_enabled = c->camera_uv_filter_enabled;
  c->params.camera_uv_cut_nm = c->camera_uv_cut_nm;
  c->params.camera_ir_filter_enabled = c->camera_ir_filter_enabled;
  c->params.camera_ir_cut_nm = c->camera_ir_cut_nm;

  c->sim = sf_sim_build(c->pack, c->film, c->scan_film ? NULL : c->paper,
                        &c->params, &err);
  if(!c->sim)
  {
    sp_set_err(errmsg, "sp_chain: sim build failed: ", err);
    free(err);
    if(c->paper) sf_profile_free(c->paper);
    sf_profile_free(c->film);
    sf_pack_free(c->pack);
    free(c);
    return NULL;
  }
  free(err);
  return c;
}

void sp_chain_destroy(sp_chain_t *c)
{
  if(!c) return;
  sf_sim_free(c->sim);
  if(c->paper) sf_profile_free(c->paper);
  sf_profile_free(c->film);
  sf_pack_free(c->pack);
  free(c);
}

/* ---------------------------------------------------- coupler diffusion -- */
/* Apply the reference DIR-coupler diffusion to the interleaved correction
 * field `corr` (w*h x 3) *in place*, mirroring darktable's spektrafilm.c:
 *
 *   corr = (1-w)*G(corr, core) + w * sum_i A_i * G(corr, R_i * tail)
 *
 * where core = cdiff_um/pixel_um, tail = ctail_um/pixel_um and the mixture is
 * SF_EXPTAIL_*. When w == 0 only the core Gaussian is applied (and only if it
 * is resolvable). `work`, `mix` are w*h*3 scratch, `scratch` a w*h plane for
 * sf_blur_plane3_wide. Non-static so the Linux harness can pin the spatial
 * stage directly (see hosts/ps8bf/test/harness.c). */
void sp_coupler_diffuse(float *corr, float *work, float *mix, float *scratch,
                        int w, int h, float pixel_um,
                        double cdiff_um, double ctail_um, double ctail_w)
{
  const size_t npx3 = (size_t)w * h * 3;
  const float csigma = (float)cdiff_um / fmaxf(pixel_um, 1e-3f);

  if(ctail_w > 0.0)
  {
    const float amp[3] = { (float)SF_EXPTAIL_A0, (float)SF_EXPTAIL_A1, (float)SF_EXPTAIL_A2 };
    const float rat[3] = { (float)SF_EXPTAIL_R0, (float)SF_EXPTAIL_R1, (float)SF_EXPTAIL_R2 };
    const float tail_px = (float)ctail_um / fmaxf(pixel_um, 1e-3f);
    const float wbase = 1.0f - (float)ctail_w;
    memcpy(work, corr, npx3 * sizeof(float));
    if(csigma > 0.1f) sf_blur_plane3_wide(work, w, h, 0, 0, csigma, scratch);
    for(size_t p = 0; p < npx3; p++) mix[p] = wbase * work[p];
    for(int g = 0; g < 3; g++)
    {
      memcpy(work, corr, npx3 * sizeof(float));
      const float ts = rat[g] * tail_px;
      if(ts > 0.1f) sf_blur_plane3_wide(work, w, h, 0, 0, ts, scratch);
      const float wk = (float)ctail_w * amp[g];
      /* fma-pinned, matching spektrafilm.c's accumulation order */
      for(size_t p = 0; p < npx3; p++) mix[p] = fmaf(wk, work[p], mix[p]);
    }
    memcpy(corr, mix, npx3 * sizeof(float));
  }
  else if(csigma > 0.1f)
  {
    sf_blur_plane3_wide(corr, w, h, 0, 0, csigma, scratch);
  }
}

/* ------------------------------------------------------- input transfer -- */
/* Map the host's samples to scene-linear light (and back). The engine stages
 * always run on linear light, so a host carrying gamma-encoded samples (8/16-bit
 * sRGB) must decode on the way in and encode on the way out. The curve table —
 * sRGB, gamma curves and the camera log curves — lives in sp_color.c
 * (sp_transfer_decode / sp_transfer_encode). These are element-wise, so they run
 * through the same host parallel hook as the stages. */
static float sp_decode(int t, float v) { return sp_transfer_decode(t, v); }
static float sp_encode(int t, float v) { return sp_transfer_encode(t, v); }

typedef struct
{
  int transfer;
  const float *in;
  float *out;
  int nch_in, nch_out;
} sp_xfer_ctx_t;

static void range_decode(void *v, size_t start, size_t count)
{
  const sp_xfer_ctx_t *s = (const sp_xfer_ctx_t *)v;
  size_t i;
  for(i = 0; i < count; i++)
  {
    const float *p = s->in + (start + i) * (size_t)s->nch_in;
    float *o = s->out + (start + i) * 3u;
    o[0] = sp_decode(s->transfer, p[0]);
    o[1] = sp_decode(s->transfer, p[1]);
    o[2] = sp_decode(s->transfer, p[2]);
  }
}
static void range_encode(void *v, size_t start, size_t count)
{
  const sp_xfer_ctx_t *s = (const sp_xfer_ctx_t *)v;
  size_t i;
  for(i = 0; i < count; i++)
  {
    float *p = s->out + (start + i) * (size_t)s->nch_out;
    p[0] = sp_encode(s->transfer, p[0]);
    p[1] = sp_encode(s->transfer, p[1]);
    p[2] = sp_encode(s->transfer, p[2]);
  }
}

/* -------------------------------------------------------------- render -- */

/* Optional host parallel runner (see sp_chain.h). NULL = serial, which is the
 * Linux default (there the engine's own OpenMP covers these loops). */
static sp_parallel_exec g_parallel = NULL;

void sp_chain_set_parallel(sp_parallel_exec exec) { g_parallel = exec; }

/* Stage-boundary progress/abort hook (per chain; NULL = silent). */
void sp_chain_set_progress(sp_chain_t *c, sp_progress_fn fn, void *ctx)
{
  if(c) { c->progress = fn; c->progress_ctx = ctx; }
}
static int sp_progress(sp_chain_t *c, int done)
{
  return c->progress ? c->progress(c->progress_ctx, done, 100) : 0;
}

/* Run a per-pixel range job, whole or split. Pixel work is independent, so the
 * partition never changes the result. */
static void sp_run_ranges(sp_range_fn fn, void *ctx, size_t npix)
{
  if(g_parallel && npix > 0) g_parallel(fn, ctx, npix);
  else fn(ctx, 0, npix);
}

/* Range jobs for each element-wise stage. The engine entry points take a base
 * pointer plus a pixel count, so a chunk is just an offset pointer + sub-count;
 * in/out strides are carried in the context. */
typedef struct
{
  const sf_sim_t *sim;
  const float *in;
  const float *aux; /* develop: the correction field, or NULL */
  float *out;
  int nch_in, nch_out;
} sp_range_ctx_t;

static void range_expose(void *v, size_t start, size_t count)
{
  const sp_range_ctx_t *s = (const sp_range_ctx_t *)v;
  sf_sim_expose(s->sim, s->in + start * (size_t)s->nch_in,
                s->out + start * (size_t)s->nch_out, count, s->nch_in, s->nch_out);
}

static void range_lograw(void *v, size_t start, size_t count)
{
  /* in place on the raw density buffer */
  const sp_range_ctx_t *s = (const sp_range_ctx_t *)v;
  sf_sim_lograw(s->out + start * (size_t)s->nch_out, count, s->nch_out);
}

static void range_develop_corr(void *v, size_t start, size_t count)
{
  const sp_range_ctx_t *s = (const sp_range_ctx_t *)v;
  sf_sim_develop_corr(s->sim, s->in + start * (size_t)s->nch_in,
                      s->out + start * 3u, count, s->nch_in);
}

static void range_develop(void *v, size_t start, size_t count)
{
  const sp_range_ctx_t *s = (const sp_range_ctx_t *)v;
  sf_sim_develop(s->sim, s->in + start * (size_t)s->nch_in,
                 s->aux ? s->aux + start * 3u : NULL,
                 s->out + start * (size_t)s->nch_out, count, s->nch_in, s->nch_out);
}

static void range_print_expose(void *v, size_t start, size_t count)
{
  const sp_range_ctx_t *s = (const sp_range_ctx_t *)v;
  sf_sim_print_expose(s->sim, s->in + start * (size_t)s->nch_in,
                      s->out + start * (size_t)s->nch_out, count, s->nch_in, s->nch_out);
}

static void range_print_develop(void *v, size_t start, size_t count)
{
  const sp_range_ctx_t *s = (const sp_range_ctx_t *)v;
  sf_sim_print_develop(s->sim, s->in + start * (size_t)s->nch_in,
                       s->out + start * (size_t)s->nch_out, count, s->nch_in, s->nch_out);
}

static void range_scan(void *v, size_t start, size_t count)
{
  const sp_range_ctx_t *s = (const sp_range_ctx_t *)v;
  sf_sim_scan(s->sim, s->in + start * (size_t)s->nch_in,
              s->out + start * (size_t)s->nch_out, count, s->nch_in, s->nch_out);
}

/* ------------------------------------------------------- Phase 2: grain -- */
/* Constants the upstream iop (darktable spektrafilm.c) keeps module-local.
 * The port reproduces them so the stages match the reference at ROI origin 0
 * (the whole-frame render the .8bf uses). */
#define SP_GRAIN_BLUR_BASE 0.89f  /* GrainParams.blur clump size */
#define SP_GRAIN_DYE_BLUR_UM 2.0f /* blur_dye_clouds_um reference */
#define SP_GRAIN_BLUR_MIN 0.05f
/* SP_HALATION_FIRST_SIGMA_UM is in sp_chain.h: the host's validator and GUI
 * cap the first-bounce radii at the same number the chain clamps them to. */
#define SP_SCATTER_CORE_CLAMP_UM 2.2
#define SP_SCATTER_TAIL_CLAMP_UM 9.7
#define SP_GLARE_ROUGHNESS 0.7f
#define SP_GLARE_BLUR_PX 0.5f

/* base-10 conversions matching sf_pow10f / sf_log10f in the reference (and the
 * upstream iop's _sf_pow10f / _sf_log10f), so print diffusion sees exactly the
 * linear field the reference does. */
static float sp_exp10f(float x) { return grain_exp2f(x * 3.321928094887362f); }
static float sp_log10f(float x) { return grain_log2f(x) * 0.3010299956639812f; }

typedef struct sp_grain_ctx
{
  const sf_grain_layers_t *layers;
  const float *plane;
  float **raw;
  int nsub, channel_idx, seed_ch, mono, w, roi_x, roi_y;
  float unif_ch, npart_scale;
} sp_grain_ctx;

static void range_grain_sample(void *v, size_t start, size_t count)
{
  const sp_grain_ctx *g = (const sp_grain_ctx *)v;
  for(size_t k = start; k < start + count; k++)
  {
    const int x = (int)(k % (size_t)g->w), y = (int)(k / (size_t)g->w);
    const float density =
        g->mono ? (g->plane[k * 3 + 0] + g->plane[k * 3 + 1] + g->plane[k * 3 + 2]) / 3.0f
                : g->plane[k * 3 + g->channel_idx];
    float samp[SF_GRAIN_MAX_SUBLAYERS];
    sf_grain_raw_samples_ml(g->layers, density, g->channel_idx, g->seed_ch,
                            (uint32_t)(x + g->roi_x), (uint32_t)(y + g->roi_y),
                            g->unif_ch, g->npart_scale, samp);
    for(int sl = 0; sl < g->nsub; sl++) g->raw[sl][k] = samp[sl];
  }
}

/* Production grain on the developed CMY density `plane` (w*h*3), mirroring the
 * upstream grain block: sample a grained density per sub-layer, blur each
 * sub-layer by its own dye-cloud radius, combine into a zero-mean delta, add
 * it back onto the clean density, clump-blur, then recover acutance with the
 * multiplicative unsharp mask. `gbuf` (w*h*3) is scratch and is overwritten;
 * `scratch` is a w*h plane. Always returns 0 (a failed allocation skips grain
 * gracefully, exactly as upstream does). */
static int sp_grain_stage(sp_chain_t *c, float *plane, float *gbuf, float *scratch,
                          int w, int h, size_t npix)
{
  const int roi_x = 0, roi_y = 0;
  const float amount = c->grain_amount;
  const int mono = sf_sim_film_bw(c->sim);
  const float pixel_um = (float)c->pixel_um;
  const float npart_scale = (pixel_um * pixel_um) / (SF_GRAIN_REF_UM * SF_GRAIN_REF_UM);

  float gdmin[3], grms[3], gunif[3], gdmin_unused[3];
  sf_sim_grain_dmin_total(c->sim, gdmin);
  sf_sim_film_grain3(c->sim, grms, gunif, gdmin_unused);

  sf_grain_layers_t layers;
  sf_sim_grain_layers(c->sim, &layers);
  int nsub = layers.n;
  if(nsub < 1) return 0;

  /* Grain model / sub-layer split (OFX grainModel + grainSublayersEnabled +
   * grainSubLayerCount):
   *   production keeps the stock's per-sub-layer fit;
   *   preview (model 0) or "sublayers off" collapses to the coarsest layer;
   *   a positive grainSubLayerCount clamps the stock's count. The engine has no
   *   fitted curve data for layers the stock did not describe, so it cannot add
   *   layers beyond the stock's own count -- only thin them out. */
  const int production = (c->grain_model != 0) && (c->grain_sublayers_enabled != 0);
  if(!production) nsub = 1;
  else if(c->grain_sublayer_count > 0 && c->grain_sublayer_count < nsub)
    nsub = c->grain_sublayer_count;
  layers.n = nsub;

  /* Per-channel and per-layer particle-area scales (OFX grainParticleScaleR/G/B
   * and grainParticleScaleLayer0..2). A scale > 1 means larger particles, i.e.
   * fewer of them, so the particle count divides by it. */
  for(int ch = 0; ch < 3; ch++)
    for(int sl = 0; sl < nsub; sl++)
      layers.layer_npart[sl][ch] /=
          (double)c->grain_particle_scale[ch] * (double)c->grain_particle_scale_layer[sl];

  /* Per-channel density floor (OFX grainDensityMinR/G/B): scale the stock's
   * floor consistently in both the sub-layer draw and the zero-mean
   * subtraction that follows. */
  for(int ch = 0; ch < 3; ch++)
  {
    gdmin[ch] *= c->grain_density_min_rgb[ch];
    for(int sl = 0; sl < nsub; sl++) layers.layer_dmin[sl][ch] *= c->grain_density_min_rgb[ch];
  }

  /* Dye-cloud blur: an absolute um override wins over the stock-scaled value. */
  const float dye_base = c->grain_dye_cloud_um > 0.0f
                             ? c->grain_dye_cloud_um
                             : SP_GRAIN_DYE_BLUR_UM * c->grain_dye_cloud;

  /* Per-sub-layer dye-cloud sigma: constant for the whole image at a given
   * (channel, sub-layer), so compute it once. */
  float dye_sigma[3][SF_GRAIN_MAX_SUBLAYERS];
  for(int ch = 0; ch < 3; ch++)
    for(int sl = 0; sl < nsub; sl++)
    {
      const float npart_c = (float)layers.layer_npart[sl][ch] * npart_scale;
      const float od_particle = (float)layers.layer_dmax[sl][ch] / fmaxf(npart_c, 1e-6f);
      dye_sigma[ch][sl] =
          dye_base * (c->grain_micro_structure_scale <= 0.0f ? 1.0f : c->grain_micro_structure_scale) * sqrtf(fmaxf(od_particle, 0.0f));
    }

  float *raw[SF_GRAIN_MAX_SUBLAYERS] = { 0 };
  int ok = 1;
  for(int sl = 0; sl < nsub; sl++)
  {
    raw[sl] = malloc(npix * sizeof(float));
    if(!raw[sl]) ok = 0;
  }
  /* A failed allocation skips the grain draw but (exactly as upstream) still
   * runs the clump blur and acutance recovery on the unmodified density, with
   * a zeroed delta: only the sampling loop is skipped. */
  if(!ok) memset(gbuf, 0, npix * 3 * sizeof(float));

  const int n_out_ch = mono ? 1 : 3;
  for(int oc = 0; ok && oc < n_out_ch; oc++)
  {
    const int channel_idx = mono ? 1 : oc; /* mono draws achromatically from ch 1 */
    const int seed_ch = mono ? 0 : oc;
    /* Per-channel uniformity multiplier (OFX grainUniformityR/G/B). */
    float unif_ch = gunif[channel_idx] * c->grain_uniformity_rgb[channel_idx];
    if(unif_ch > 0.999f) unif_ch = 0.999f;
    else if(unif_ch < 0.0f) unif_ch = 0.0f;

    sp_grain_ctx g;
    g.layers = &layers;
    g.plane = plane;
    g.raw = raw;
    g.nsub = nsub;
    g.channel_idx = channel_idx;
    g.seed_ch = seed_ch ^ (uint32_t)(c->grain_seed * 0x9e3779b9u);
    g.mono = mono;
    g.w = w;
    g.roi_x = roi_x;
    g.roi_y = roi_y;
    g.unif_ch = unif_ch;
    g.npart_scale = npart_scale;
    sp_run_ranges(range_grain_sample, &g, npix);

    for(int sl = 0; sl < nsub; sl++)
      sf_blur_plane1(raw[sl], w, h, dye_sigma[channel_idx][sl], NULL, scratch);

    for(size_t k = 0; k < npix; k++)
    {
      float total = 0.0f;
      for(int sl = 0; sl < nsub; sl++) total += raw[sl][k];
      const float gdens = total - gdmin[channel_idx];
      const float density = mono ? (plane[k * 3 + 0] + plane[k * 3 + 1] + plane[k * 3 + 2]) / 3.0f
                                 : plane[k * 3 + channel_idx];
      const float delta = (gdens - density) * amount;
      if(mono)
        gbuf[k * 3 + 0] = gbuf[k * 3 + 1] = gbuf[k * 3 + 2] = delta;
      else
        gbuf[k * 3 + oc] = delta;
    }
  }
  for(int sl = 0; sl < nsub; sl++) free(raw[sl]);

  /* Grain saturation: scale the chromaticity of the grain noise (OFX
   * spektra_apply_grain_controls). 1.0 = full colour (no change), 0 = neutral
   * luminance grain. Applied per-pixel to the whole delta so the per-channel
   * blur/unsharp passes below still see the (now luminance) noise. */
  if(c->grain_saturation != 1.0f)
  {
    const float sat = c->grain_saturation;
    for(size_t i = 0; i < npix; i++)
    {
      const float d0 = gbuf[i * 3 + 0], d1 = gbuf[i * 3 + 1], d2 = gbuf[i * 3 + 2];
      const float mn = (d0 + d1 + d2) / 3.0f;
      gbuf[i * 3 + 0] = mn + (d0 - mn) * sat;
      gbuf[i * 3 + 1] = mn + (d1 - mn) * sat;
      gbuf[i * 3 + 2] = mn + (d2 - mn) * sat;
    }
  }
  /* Add the still-unblurred (zero-mean) delta onto the clean density, then
   * clump-blur the grained absolute density and recover acutance. */
  for(size_t i = 0; i < npix * 3; i++) plane[i] += gbuf[i];
  /* Clump blur: an absolute um override (OFX grainFinalBlurUm) wins over the
   * stock-scaled GrainParams.blur. */
  const float sigma = c->grain_final_blur_um > 0.0f
                          ? c->grain_final_blur_um / fmaxf(pixel_um, 1e-3f)
                          : SP_GRAIN_BLUR_BASE * fmaxf(c->grain_blur, SP_GRAIN_BLUR_MIN);
  sf_blur_plane3(plane, w, h, sigma, scratch);
  if(c->grain_usm_sigma > 0.0f && c->grain_usm_amount > 0.0f)
    sf_multiplicative_unsharp_mask3(plane, w, h, c->grain_usm_sigma, c->grain_usm_amount,
                                    gdmin, gbuf, scratch);
  return 0;
}

int sp_chain_render(sp_chain_t *c,
                    const float *in, float *out,
                    int w, int h, int nch)
{
  if(!c || !c->sim || w <= 0 || h <= 0) return 1;
  const size_t npix = (size_t)w * h;
  if((size_t)nch < 3) return 1;

  float *raw = malloc(npix * 3 * sizeof(float));
  float *corr = malloc(npix * 3 * sizeof(float));
  float *cmy = malloc(npix * 3 * sizeof(float));
  float *lraw = malloc(npix * 3 * sizeof(float));
  float *pcmy = NULL;
  if(!c->scan_film) pcmy = malloc(npix * 3 * sizeof(float));

  /* Decode the host's (possibly gamma-encoded) samples to scene-linear light
   * up front; the engine stages always run on linear light. */
  float *lin = NULL;
  if(c->transfer != SP_TRANSFER_LINEAR) lin = malloc(npix * 3 * sizeof(float));

  /* scratch for the coupler diffusion: one w*h plane for sf_blur_plane3_wide
   * and one interleaved working copy of the correction field. The mix
   * accumulator reuses cmy (free until develop() writes it). */
  float *w1 = malloc(npix * sizeof(float));
  float *blur = malloc(npix * 3 * sizeof(float));
  if(!raw || !corr || !cmy || !lraw || (!c->scan_film && !pcmy) || !w1 || !blur ||
     (c->transfer != SP_TRANSFER_LINEAR && !lin))
  {
    free(raw); free(corr); free(cmy); free(lraw); free(pcmy);
    free(lin); free(w1); free(blur);
    return 2;
  }

  /* per-pixel chain, exact spectral or table path — all engine calls,
   * full-image batches. Each batch is element-wise and runs through the host
   * parallel hook when one is installed (see sp_chain.h). Between stages the
   * optional progress hook runs; if it asks to abort we unwind without doing
   * any further work (the caller discards the frame). */
  int rc = 0;
  sp_range_ctx_t st;
  st.sim = c->sim;
  st.aux = NULL;

  const float *src = in;
  int src_nch = nch;
  if(lin)
  {
    sp_xfer_ctx_t x;
    x.transfer = c->transfer;
    x.in = in;
    x.out = lin;
    x.nch_in = nch;
    x.nch_out = 3;
    sp_run_ranges(range_decode, &x, npix);
    src = lin;
    src_nch = 3;
  }
  if(sp_progress(c, 5)) { rc = 3; goto done; }

  st.in = src; st.out = raw; st.nch_in = src_nch; st.nch_out = 3;
  sp_run_ranges(range_expose, &st, npix);
  if(sp_progress(c, 12)) { rc = 3; goto done; }

  /* Pre-film spatial look stages, on linear exposure in the reference's order:
   * highlight boost -> camera diffusion -> halation/scatter. All are no-ops at
   * the defaults, so the Phase-1 slice is unchanged. */
  if(c->boost_ev > 0.0f)
    sf_boost_highlights(raw, w, h, c->boost_ev, c->boost_range, c->protect_ev);
  if(c->diffusion_on && c->diffusion_strength > 0.0f)
    sf_diffusion_filter(raw, w, h, c->pixel_um, c->diffusion_family, c->diffusion_strength,
                        c->diffusion_scale, c->diffusion_warmth, c->diffusion_core_intensity,
                        c->diffusion_core_size, c->diffusion_halo_intensity, c->diffusion_halo_size,
                        c->diffusion_bloom_intensity, c->diffusion_bloom_size);
  if(c->halation_on && (c->scatter_amount > 0.0f || c->halation_amount > 0.0f))
  {
    double hal_strength[3], hal_sigma_um[3];
    sf_sim_halation_params(c->sim, hal_strength, hal_sigma_um);
    /* Per-channel return weight and first-bounce radius overrides (locked v1
     * scope + per-channel first sigma): < 0 keeps the stock's own value from
     * the pack. */
    for(int ch = 0; ch < 3; ch++)
    {
      if(c->hal_strength_override[ch] >= 0.0) hal_strength[ch] = c->hal_strength_override[ch];
      if(c->hal_sigma_override[ch] >= 0.0) hal_sigma_um[ch] = c->hal_sigma_override[ch];
      /* clamp so a future pack entry larger than the chosen ceiling cannot
       * under-pad the halo (whole-frame render: padding is unbounded) */
      {
        const double cap_um = (c->hal_sigma_max_um >= 0.0) ? c->hal_sigma_max_um
                                                           : SP_HALATION_FIRST_SIGMA_UM;
        hal_sigma_um[ch] = fmin(hal_sigma_um[ch], cap_um);
      }
    }
    double sc_core[3], sc_tail[3], sc_w[3];
    sf_sim_scatter_params(c->sim, sc_core, sc_tail, sc_w);
    for(int ch = 0; ch < 3; ch++)
    {
      sc_core[ch] = fmin(sc_core[ch], SP_SCATTER_CORE_CLAMP_UM);
      sc_tail[ch] = fmin(sc_tail[ch], SP_SCATTER_TAIL_CLAMP_UM);
    }
    sf_halation(raw, w, h, c->pixel_um, sc_core, sc_tail, sc_w, c->scatter_amount,
                c->scatter_scale, c->halation_amount, c->halation_scale, hal_strength,
                hal_sigma_um);
  }
  if(sp_progress(c, 16)) { rc = 3; goto done; }

  memcpy(lraw, raw, npix * 3 * sizeof(float));

  st.in = lraw; st.out = lraw; st.nch_in = 3; st.nch_out = 3;
  sp_run_ranges(range_lograw, &st, npix);
  /* Experimental film Push/Pull: per-pixel log-raw warp + density gain. */
  if(c->push_pull_mode == 1 && c->film_push_pull_stops != 0.0)
    _sf_expfilmwarp_inplace(lraw, corr, npix, c->film_push_pull_stops);
  if(sp_progress(c, 22)) { rc = 3; goto done; }

  st.in = lraw; st.out = corr; st.nch_in = 3; st.nch_out = 3;
  sp_run_ranges(range_develop_corr, &st, npix);
  if(sp_progress(c, 30)) { rc = 3; goto done; }

  if(c->couplers)
  {
    double cdiff_um = 0.0, ctail_um = 0.0, ctail_w = 0.0;
    sf_sim_coupler_diffusion(c->sim, &cdiff_um, &ctail_um, &ctail_w);
    if(c->coupler_um >= 0.0) cdiff_um = c->coupler_um; /* explicit override */
    /* `cmy` is free until develop() writes it, so reuse it as the mix buffer.
     * The DIR blur is a spatial stage (separable but global), so it stays
     * serial: only the element-wise batches are threaded. */
    sp_coupler_diffuse(corr, blur, cmy, w1, w, h, (float)c->pixel_um,
                       cdiff_um, ctail_um, ctail_w);
    st.in = lraw; st.aux = corr; st.out = cmy; st.nch_in = 3; st.nch_out = 3;
    sp_run_ranges(range_develop, &st, npix);
  }
  else
  {
    st.in = lraw; st.aux = NULL; st.out = cmy; st.nch_in = 3; st.nch_out = 3;
    sp_run_ranges(range_develop, &st, npix);
  }
  if(sp_progress(c, 55)) { rc = 3; goto done; }

  if(c->push_pull_mode == 1 && c->film_push_pull_stops != 0.0)
    for(size_t k = 0; k < npix; k++)
      for(int ch = 0; ch < 3; ch++)
        cmy[k*3+ch] *= corr[k*3+ch];

  /* Grain runs on the developed CMY density before the print. `corr` holds the
   * coupler field develop() already consumed, so it is free to reuse as the
   * grain delta buffer. */
  if(c->grain_on && c->grain_amount > 0.0f)
    sp_grain_stage(c, cmy, corr, w1, w, h, npix);
  if(sp_progress(c, 60)) { rc = 3; goto done; }

  /* Negative bleach bypass (OFX negativeBleachBypassAmount +
   * negativeLeucoCyanCoupling): mix developed film dye density toward the
   * neutral black density driven by the retained silver fraction and subtract
   * the leuco-cyan density loss from the cyan channel. No-op at defaults. */
  if(c->neg_bleach_bypass > 0.0f || c->neg_leuco_cyan != 0.0f)
  {
    for(size_t k = 0; k < npix; k++)
    {
      float *v = cmy + k * 3;
      float r = v[0], g = v[1], b = v[2];
      /* retained silver image (negative: silver layers = max(c,0)) */
      float sl0 = r > 0 ? r : 0;
      float sl1 = g > 0 ? g : 0;
      float sl2 = b > 0 ? b : 0;
      float retained = ((sl0/(sl0+0.85f)) + (sl1/(sl1+0.85f)) + (sl2/(sl2+0.85f))) / 3.0f;
      float amt = c->neg_bleach_bypass;
      if(amt < 0) amt = 0;
      if(amt > 1) amt = 1;
      float blackImgAmt = amt * retained;
      if(blackImgAmt < 0) blackImgAmt = 0;
      if(blackImgAmt > 1) blackImgAmt = 1;
      float bd = r; if(g > bd) bd = g; if(b > bd) bd = b;
      float br = r + (bd - r) * blackImgAmt;
      float bg = g + (bd - g) * blackImgAmt;
      float bb = b + (bd - b) * blackImgAmt;
      if(amt > 0)
      {
        float coupling = c->neg_leuco_cyan;
        if(coupling < 0) coupling = 0;
        if(coupling > 2) coupling = 2;
        float drive = r; if(drive < 0) drive = 0; if(drive > 1) drive = 1;
        float leu = amt * coupling * 0.30f * drive;
        if(leu > 0.30f * coupling) leu = 0.30f * coupling;
        br = br - leu; if(br < 0) br = 0;
      }
      corr[k] = retained * amt * 0.22f;
      v[0] = br; v[1] = bg; v[2] = bb;
    }
  }

  if(c->scan_film)
  {
    st.in = cmy; st.aux = NULL; st.out = out; st.nch_in = 3; st.nch_out = nch;
    sp_run_ranges(range_scan, &st, npix);
    if(c->scan_negative_invert)
      for(size_t k = 0; k < npix; k++)
        for(int ch = 0; ch < 3 && ch < nch; ch++)
        {
          float v = out[k * (size_t)nch + ch];
          if(v < 0) v = 0;
          if(v > 1) v = 1;
          out[k * (size_t)nch + ch] = 1.0f - v;
        }
  }
  else
  {
    st.in = cmy; st.aux = NULL; st.out = lraw; st.nch_in = 3; st.nch_out = 3;
    sp_run_ranges(range_print_expose, &st, npix);
    /* Negative bleach retained silver: flat spectral density term threaded back as a
       per-pixel offset to the film's print-expose log raw (Silver = therm) */
    if(c->neg_bleach_bypass > 0.0f)
      for(size_t k = 0; k < npix; k++)
        for(int ch = 0; ch < 3; ch++)
          lraw[k*3+ch] -= corr[k];

    /* Printer lights / APD print timing: the OFX applies an enlarger-exposure
       scale exp2((internalAPDCalibration + trim)/12). The APD calibration table
       (licensed ST 2065-2 data) is unavailable in this port, so internal = 0.
       Stub: reserve a per-film/paper offset hook below; when data is available,
       populate internal from that table (not persisted; computed at sim build).
       For now it remains zero and the trim alone affects exposure. */
    if(c->print_timing == 1)
    {
      /* reserved internal offset placeholder (zeros until pack ships table) */
      float delta[3] =
        { c->printer_lights_r, c->printer_lights_g, c->printer_lights_b };
      if(c->printer_lights_gang)
      {
        float m = (c->printer_lights_r + c->printer_lights_g + c->printer_lights_b) / 3.0f;
        delta[0] = delta[1] = delta[2] = m;
      }
      const float log10_2 = 0.3010299956639812f;
      for(int ch = 0; ch < 3; ch++)
        if(delta[ch] != 0.0f)
          for(size_t k = 0; k < npix; k++)
            lraw[k * 3 + ch] += (delta[ch] / 12.0f) * log10_2;
    }

    /* print diffusion, in linear light between expose and develop (the
     * reference converts log10 density -> linear -> diffuse -> back) */
    if(c->print_diffusion_on && c->print_diffusion_strength > 0.0f)
    {
      for(size_t k = 0; k < npix * 3; k++)
        lraw[k] = sp_exp10f(lraw[k]);
      sf_diffusion_filter(lraw, w, h, c->pixel_um, c->print_diffusion_family,
                          c->print_diffusion_strength, c->print_diffusion_scale,
                          c->print_diffusion_warmth, c->print_diffusion_core_intensity,
                          c->print_diffusion_core_size, c->print_diffusion_halo_intensity,
                          c->print_diffusion_halo_size, c->print_diffusion_bloom_intensity,
                          c->print_diffusion_bloom_size);
      for(size_t k = 0; k < npix * 3; k++)
        lraw[k] = sp_log10f(fmaxf(lraw[k], 0.0f) + 1e-10f);
    }
    if(sp_progress(c, 65)) { rc = 3; goto done; }

    st.in = lraw; st.out = pcmy; st.nch_in = 3; st.nch_out = 3;
    sp_run_ranges(range_print_develop, &st, npix);

    /* Print bleach bypass (OFX printBleachBypassAmount): mix the developed print
     * density toward neutral and subtract the retained-silver contribution would
     * need the scan path internals; the dye-density mix is applied here (control
     * no-op at default). */
    if(c->print_bleach_bypass > 0.0f)
    {
      for(size_t k = 0; k < npix; k++)
      {
        float *v = pcmy + k * 3;
        float r = v[0], g = v[1], b = v[2];
        float sl0 = r > 0 ? r : 0;
        float sl1 = g > 0 ? g : 0;
        float sl2 = b > 0 ? b : 0;
        float retained = ((sl0/(sl0+0.65f)) + (sl1/(sl1+0.65f)) + (sl2/(sl2+0.65f))) / 3.0f;
        float amt = c->print_bleach_bypass;
        if(amt < 0) amt = 0;
        if(amt > 1) amt = 1;
        float blackImgAmt = amt * retained;
        if(blackImgAmt < 0) blackImgAmt = 0;
        if(blackImgAmt > 1) blackImgAmt = 1;
        float bd = r; if(g > bd) bd = g; if(b > bd) bd = b;
        v[0] = r + (bd - r) * blackImgAmt;
        v[1] = g + (bd - g) * blackImgAmt;
        v[2] = b + (bd - b) * blackImgAmt;
      }
    }

    st.in = pcmy; st.out = out; st.nch_in = 3; st.nch_out = nch;
    sp_run_ranges(range_scan, &st, npix);
  }

  /* Scanner optics + viewing glare on the scanned RGB, over the whole frame so
   * no edge artifact enters. `sf_blur_plane3`/`sf_unsharp_mask3`/`sf_glare`
   * need a contiguous 3-channel buffer; `out` carries `nch` channels (alpha),
   * so when an effect is active and nch != 3 we stage through `lraw` (free
   * again after the scan). */
  if(c->scan_blur > 0.0f || (c->scan_usm_sigma > 0.0f && c->scan_usm_amount > 0.0f) ||
     (!c->scan_film && c->glare_percent > 0.0f))
  {
    float *s3 = out;
    const int contiguous = (nch == 3);
    if(!contiguous)
    {
      for(size_t k = 0; k < npix; k++)
      {
        lraw[k * 3 + 0] = out[k * nch + 0];
        lraw[k * 3 + 1] = out[k * nch + 1];
        lraw[k * 3 + 2] = out[k * nch + 2];
      }
      s3 = lraw;
    }
    if(c->scan_blur > 0.0f)
      sf_blur_plane3(s3, w, h, c->scan_blur, w1);
    if(c->scan_usm_sigma > 0.0f && c->scan_usm_amount > 0.0f)
      sf_unsharp_mask3(s3, w, h, c->scan_usm_sigma, c->scan_usm_amount, corr, w1);
    if(!c->scan_film && c->glare_percent > 0.0f)
      sf_glare(s3, w, h, c->glare_percent, c->scan_glare_roughness, c->scan_glare_blur, 0, 0, w1);
    if(!contiguous)
    {
      for(size_t k = 0; k < npix; k++)
      {
        out[k * nch + 0] = s3[k * 3 + 0];
        out[k * nch + 1] = s3[k * 3 + 1];
        out[k * nch + 2] = s3[k * 3 + 2];
      }
    }
  }
  if(sp_progress(c, 85)) { rc = 3; goto done; }

  /* Encode the scene-linear result back to the host's sample space (in place;
   * all reads of the input are long done, so `out` may alias `in`). */
  if(lin)
  {
    sp_xfer_ctx_t x;
    x.transfer = c->transfer;
    x.in = NULL;
    x.out = out;
    x.nch_in = 0;
    x.nch_out = nch;
    sp_run_ranges(range_encode, &x, npix);
  }
  sp_progress(c, 100);

done:
  free(raw); free(corr); free(cmy); free(lraw); free(pcmy); free(lin);
  free(w1); free(blur);
  return rc;
}

const sf_sim_params_t *sp_chain_params(const sp_chain_t *c)
{
  return c ? &c->params : NULL;
}