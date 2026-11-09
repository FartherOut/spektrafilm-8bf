/* SPDX-License-Identifier: GPL-3.0-or-later */
/* refrender — Linux reference renderer for the Phase 1 vertical slice.
 *
 * Runs the SAME portable core + shims the plugin uses (sp_chain over
 * spektra_sim.c/spektra_core.c with glib/json-glib replaced by the embedded
 * mini shims) against the real data pack on disk. Output is the golden
 * reference the plugin must match:
 *
 *   refrender [params.json] [in.ppm] [out.ppm]
 *
 * PPM I/O only (P6, 8-bit); the test generator produces PPM + PNG pairs.
 * The params.json shape is the same flat key set the adapter uses
 * (see sp_params.c).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_json.h"
#include "sp_chain.h"
#include "sp_params.h"

/* ------------------------------------------------------------- PPM I/O -- */
static int ppm_read(const char *path, int *w, int *h, unsigned char **px)
{
  FILE *f = fopen(path, "rb");
  if(!f) return -1;
  if(fgetc(f) != 'P' || fgetc(f) != '6') /* P6 only */
  {
    fclose(f);
    return -3;
  }
  int v[3] = { 0, 0, 0 };
  int vi = 0, num = -1;
  long data_at = 0;
  int c = 0;
  for(;;)
  {
    c = fgetc(f);
    if(c == EOF) { fclose(f); return -2; }
    if(c == '#') { while((c = fgetc(f)) != EOF && c != '\n') {} continue; }
    if(c >= '0' && c <= '9')
    {
      if(num < 0) num = 0;
      num = num * 10 + (c - '0');
    }
    else
    {
      if(num >= 0)
      {
        v[vi++] = num;
        num = -1;
      }
      if(vi == 3)
      {
        /* `c` is the single whitespace that ends the ASCII header */
        data_at = ftell(f); /* next byte is pixel data */
        break;
      }
    }
  }
  const int W = v[0], H = v[1], maxv = v[2];
  if(W <= 0 || H <= 0 || maxv <= 0 || maxv > 255)
  {
    fclose(f);
    return -3;
  }
  fseek(f, data_at, SEEK_SET);
  const size_t need = (size_t)W * H * 3;
  unsigned char *buf = malloc(need);
  if(!buf)
  {
    fclose(f);
    return -4;
  }
  if(fread(buf, 1, need, f) != need)
  {
    free(buf);
    fclose(f);
    return -5;
  }
  fclose(f);
  *w = W;
  *h = H;
  *px = buf;
  return 0;
}

static int ppm_write(const char *path, int w, int h, const unsigned char *px)
{
  FILE *f = fopen(path, "wb");
  if(!f) return -1;
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  fwrite(px, 1, (size_t)w * h * 3, f);
  fclose(f);
  return 0;
}

/* --------------------------------------------------------------- main -- */
static int run_anchor(void)
{
  /* mirror spike_main's 8 colours and its parameterisation (input sRGB,
   * exact spectral, couplers on, development_min 0) at full float precision */
  sp_chain_params_t cp;
  sp_params_defaults(&cp);
  cp.pack_dir = "data/pack";
  cp.lut_steps = 0;
  cp.couplers_active = 1;
  cp.coupler_diffusion_um = -1.0;
  cp.input_transfer = SP_TRANSFER_LINEAR; /* the literals below are linear */

  char *err = NULL;
  sp_chain_t *chain = sp_chain_create(&cp, &err);
  if(!chain)
  {
    fprintf(stderr, "refrender anchor: %s\n", err ? err : "chain create failed");
    free(err);
    return 1;
  }
  free(err);

  const float tests[8][3] = {
    { 0.18f, 0.18f, 0.18f },
    { 0.5f, 0.35f, 0.25f },
    { 0.8f, 0.2f, 0.2f },
    { 0.2f, 0.8f, 0.2f },
    { 0.2f, 0.2f, 0.8f },
    { 1.0f, 0.95f, 0.9f },
    { 0.02f, 0.02f, 0.02f },
    { 1.0f, 0.5f, 0.0f },
  };
  float out[3] = { 0, 0, 0 };
  for(int t = 0; t < 8; t++)
  {
    const int rc = sp_chain_render(chain, tests[t], out, 1, 1, 3);
    if(rc != 0)
    {
      fprintf(stderr, "refrender anchor: render failed (%d)\n", rc);
      return 1;
    }
    printf("anchor %d: out %7.4f %7.4f %7.4f\n", t, out[0], out[1], out[2]);
  }
  sp_chain_destroy(chain);
  return 0;
}

int main(int argc, char **argv)
{
  if(argc > 1 && strcmp(argv[1], "--anchor") == 0) return run_anchor();

  const char *params_path = argc > 1 ? argv[1] : "params.json";
  const char *in_path = argc > 2 ? argv[2] : "in.ppm";
  const char *out_path = argc > 3 ? argv[3] : "out.ppm";

  /* params */
  FILE *pf = fopen(params_path, "rb");
  if(!pf)
  {
    fprintf(stderr, "refrender: cannot open %s\n", params_path);
    return 1;
  }
  fseek(pf, 0, SEEK_END);
  long plen = ftell(pf);
  fseek(pf, 0, SEEK_SET);
  char *pjson = malloc((size_t)plen + 1);
  if(!pjson || fread(pjson, 1, (size_t)plen, pf) != (size_t)plen)
  {
    fprintf(stderr, "refrender: cannot read %s\n", params_path);
    return 1;
  }
  pjson[plen] = 0;
  fclose(pf);

  sp_chain_params_t cp;
  sp_params_defaults(&cp);
  if(sp_params_parse(pjson, (int)plen, &cp) != 0)
  {
    fprintf(stderr, "refrender: params parse failed\n");
    return 1;
  }

  /* input */
  int w = 0, h = 0;
  unsigned char *px = NULL;
  if(ppm_read(in_path, &w, &h, &px) != 0)
  {
    fprintf(stderr, "refrender: bad PPM %s\n", in_path);
    return 1;
  }

  /* Film format resolves against the input's long edge (see sp_chain.h). */
  cp.image_long_edge_px = (w > h) ? (double)w : (double)h;

  char *err = NULL;
  sp_chain_t *chain = sp_chain_create(&cp, &err);
  if(!chain)
  {
    fprintf(stderr, "refrender: %s\n", err ? err : "chain create failed");
    free(err);
    return 1;
  }
  free(err);

  const size_t npix = (size_t)w * h;
  float *fin = malloc(npix * 3 * sizeof(float));
  float *fout = malloc(npix * 3 * sizeof(float));
  for(size_t p = 0; p < npix; p++)
    for(int c = 0; c < 3; c++)
      fin[p * 3 + c] = px[p * 3 + c] / 255.0f;

  const int rc = sp_chain_render(chain, fin, fout, w, h, 3);
  if(rc != 0)
  {
    fprintf(stderr, "refrender: sp_chain_render failed (%d)\n", rc);
    return 1;
  }

  for(size_t p = 0; p < npix; p++)
    for(int c = 0; c < 3; c++)
    {
      float v = fout[p * 3 + c];
      if(v < 0.0f) v = 0.0f;
      if(v > 1.0f) v = 1.0f;
      px[p * 3 + c] = (unsigned char)(v * 255.0f + 0.5f);
    }

  if(ppm_write(out_path, w, h, px) != 0)
  {
    fprintf(stderr, "refrender: cannot write %s\n", out_path);
    return 1;
  }

  printf("refrender: %s/%s/%s %dx%d lut_steps=%d couplers=%d scan=%d ev=%.3f print=%.3f -> %s\n",
         cp.film, cp.paper, cp.scan_film ? "scan" : "neg-print",
         w, h, cp.lut_steps, cp.couplers_active, cp.scan_film,
         cp.exposure_ev, cp.print_exposure, out_path);

  free(fin);
  free(fout);
  free(px);
  free(pjson);
  sp_chain_destroy(chain);
  return 0;
}