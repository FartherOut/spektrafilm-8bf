/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Phase 0 extraction spike — prove the darktable spektra engine runs
 * standalone, on CPU, outside darktable.
 *
 * Loads the 0.3.3 data pack + one film + one paper profile, builds the
 * runtime tables (sf_sim_build) and pushes a handful of colours through the
 * full negative -> develop -DIR-> print -> scan chain at EXACT spectral
 * resolution (lut_steps = 0, the reference's per-pixel path).
 *
 * Usage: spike [pack_dir] [film] [paper]
 *   defaults:  ../third_party/darktable-spektrafilm/packs/0.3.3
 *              kodak_portra_400  kodak_2383
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spektra_sim.h"
#include "spektra_core.h"

/* full chain, exact spectral: in (linear, input primaries) -> out (linear,
 * output primaries, gamut compressed) */
static void run_chain(const sf_sim_t *sim, const float *in, float *out)
{
  float raw[3], lograw[3], corr[3], cmy[3], lraw[3], pcmy[3];
  sf_sim_expose(sim, in, raw, 1, 3, 3);
  memcpy(lograw, raw, sizeof lograw);
  sf_sim_lograw(lograw, 1, 3);
  sf_sim_develop_corr(sim, lograw, corr, 1, 3);
  sf_sim_develop(sim, lograw, corr, cmy, 1, 3, 3);
  sf_sim_print_expose(sim, cmy, lraw, 1, 3, 3);
  sf_sim_print_develop(sim, lraw, pcmy, 1, 3, 3);
  sf_sim_scan(sim, pcmy, out, 1, 3, 3);
}

static void render_color(const sf_sim_t *sim, const float *rgb)
{
  float raw[3], lograw[3], corr[3], cmy[3], lraw[3], pcmy[3], out[3];
  sf_sim_expose(sim, rgb, raw, 1, 3, 3);
  memcpy(lograw, raw, sizeof lograw);
  sf_sim_lograw(lograw, 1, 3);
  sf_sim_develop_corr(sim, lograw, corr, 1, 3);
  sf_sim_develop(sim, lograw, corr, cmy, 1, 3, 3);
  sf_sim_print_expose(sim, cmy, lraw, 1, 3, 3);
  sf_sim_print_develop(sim, lraw, pcmy, 1, 3, 3);
  sf_sim_scan(sim, pcmy, out, 1, 3, 3);

  printf("in %6.3f %6.3f %6.3f | lograw %7.4f %7.4f %7.4f | filmD %7.4f %7.4f %7.4f | printD %7.4f %7.4f %7.4f | out %7.4f %7.4f %7.4f\n",
         rgb[0], rgb[1], rgb[2],
         lograw[0], lograw[1], lograw[2],
         cmy[0], cmy[1], cmy[2],
         pcmy[0], pcmy[1], pcmy[2],
         out[0], out[1], out[2]);
}

/* 96x64 sanity render: top half = grey ramp, bottom half = R | G | B ramps.
 * Written as linear values clipped to [0,1]; a rough PPM for eyeballing only. */
static void render_ppm(const sf_sim_t *sim, const char *path)
{
  const int W = 96, H = 64;
  unsigned char *px = malloc((size_t)W * H * 3);
  for(int y = 0; y < H; y++)
  {
    for(int x = 0; x < W; x++)
    {
      float in[3] = { 0 }, out[3] = { 0 };
      if(y < 32)
      {
        in[0] = in[1] = in[2] = (float)x / (W - 1);
      }
      else if(x < 34)
        in[0] = (float)x / 33.0f;
      else if(x < 67)
        in[1] = (float)(x - 34) / 32.0f;
      else
        in[2] = (float)(x - 67) / 28.0f;
      run_chain(sim, in, out);
      for(int c = 0; c < 3; c++)
      {
        float v = fminf(fmaxf(out[c], 0.0f), 1.0f);
        px[(y * W + x) * 3 + c] = (unsigned char)lrintf(v * 255.0f);
      }
    }
  }
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  fwrite(px, 1, (size_t)W * H * 3, f);
  fclose(f);
  free(px);
  printf("wrote %s\n", path);
}

/* Spatial-path smoke: sf_unsharp_mask3 on a sharp dark|light step. Exercises
 * the gaussian kernel, aligned allocations, and the OpenMP loops. Edge rows
 * must show overshoot (dark side dips, light side rises) after the pass. */
static void spatial_smoke(void)
{
  const int w = 64, h = 64;
  float *buf = malloc((size_t)w * h * 3 * sizeof(float));
  float *orig = malloc((size_t)w * h * 3 * sizeof(float));
  float *work = malloc((size_t)w * h * sizeof(float));
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++)
    {
      const float v = (x < 24) ? 0.10f : 0.60f;
      for(int c = 0; c < 3; c++) buf[(y * w + x) * 3 + c] = v;
    }
  const int r = 32;
  printf("\nspatial smoke (%dx%d step, sigma=2.0 amount=1.5):\n", w, h);
  printf("  before row r: ");
  for(int x = 18; x <= 30; x++) printf("%4.2f ", buf[(r * w + x) * 3]);
  printf("\n");
  sf_unsharp_mask3(buf, w, h, 2.0f, 1.5f, orig, work);
  float omin = 1e9f, omax = -1e9f;
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++)
      for(int c = 0; c < 3; c++)
      {
        const float v = buf[(y * w + x) * 3 + c];
        if(v < omin) omin = v;
        if(v > omax) omax = v;
      }
  printf("  after  row r: ");
  for(int x = 18; x <= 30; x++) printf("%4.2f ", buf[(r * w + x) * 3]);
  printf("\n  after  global min %7.4f max %7.4f\n", omin, omax);
  const int ok = (omin < 0.05f) && (omax > 0.65f);
  printf("  spatial smoke: %s\n\n", ok ? "PASS (edge overshoot present)" : "FAIL");
  free(buf); free(orig); free(work);
}

int main(int argc, char **argv)
{
  const char *pack_dir = argc > 1 ? argv[1] : "data/pack";
  const char *film_name = argc > 2 ? argv[2] : "kodak_portra_400";
  const char *paper_name = argc > 3 ? argv[3] : "kodak_2383";

  char film_path[1024], paper_path[1024];
  snprintf(film_path, sizeof film_path, "%s/profiles/%s.json", pack_dir, film_name);
  snprintf(paper_path, sizeof paper_path, "%s/profiles/%s.json", pack_dir, paper_name);

  char *err = NULL;
  sf_pack_status_t status = SF_PACK_OK;
  sf_pack_t *pack = sf_pack_load(pack_dir, &err, &status);
  if(!pack)
  {
    fprintf(stderr, "pack load failed (status %d): %s\n", status, err ? err : "-");
    free(err);
    return 1;
  }
  free(err);

  /* development_min = 0.0 == darktable module default ("development time" 0..15) */
  sf_profile_t *film = sf_profile_load(film_path, 0.0f, &err);
  if(!film)
  {
    fprintf(stderr, "film load failed: %s\n", err ? err : "-");
    free(err);
    return 1;
  }
  free(err);
  sf_profile_t *paper = sf_profile_load(paper_path, 0.0f, &err);
  if(!paper)
  {
    fprintf(stderr, "paper load failed: %s\n", err ? err : "-");
    free(err);
    return 1;
  }
  free(err);

  sf_sim_params_t params;
  sf_sim_params_defaults(&params);
  sf_sim_params_set_input_srgb(&params);
  sf_sim_params_set_output_srgb(&params);
  params.lut_steps = 0; /* exact spectral per pixel */

  sf_sim_t *sim = sf_sim_build(pack, film, paper, &params, &err);
  if(!sim)
  {
    fprintf(stderr, "sim build failed: %s\n", err ? err : "-");
    free(err);
    return 1;
  }
  free(err);

  printf("pack    : %s\n", pack_dir);
  printf("film    : %s   paper: %s\n", film_name, paper_name);
  printf("mode    : negative -> print -> scan  (exact spectral, lut_steps=0)\n");
  printf("input   : sRGB linear (input primaries)\n");
  printf("output  : sRGB linear primaries, OkLCh gamut compression\n");
  printf("filmdmax: %g\n\n", sf_sim_film_dmax(sim, 0));

  const float tests[8][3] = {
    { 0.18f, 0.18f, 0.18f }, /* mid grey (+0 EV) */
    { 0.5f, 0.35f, 0.25f },  /* skin tone */
    { 0.8f, 0.2f, 0.2f },    /* red */
    { 0.2f, 0.8f, 0.2f },    /* green */
    { 0.2f, 0.2f, 0.8f },    /* blue */
    { 1.0f, 0.95f, 0.9f },   /* white */
    { 0.02f, 0.02f, 0.02f }, /* black */
    { 1.0f, 0.5f, 0.0f },    /* orange */
  };
  for(int i = 0; i < 8; i++) render_color(sim, tests[i]);

  render_ppm(sim, "spike_out.ppm");
  sf_sim_free(sim);
  sf_profile_free(film);
  sf_profile_free(paper);
  sf_pack_free(pack);

  spatial_smoke();
  return 0;
}