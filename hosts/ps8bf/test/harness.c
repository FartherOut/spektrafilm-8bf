/* SPDX-License-Identifier: GPL-3.0-or-later */
/* harness.c — Linux mock-host test for the ps8bf filter.
 *
 * Drives the *exact* portable selector bodies (sf_filter_start / _continue /
 * _finish) that the Windows DLL runs, through a mock FilterRecord. No Win32,
 * no Photoshop: the point is to exercise the code between the host and the
 * engine (params -> sp_chain, sample <-> float, the one-tile loop) and the
 * DIR-coupler spatial stage, all of which are invisible to the 1x1 anchor in
 * tools/refrender.
 *
 * Subcommands:
 *   selftest                       property/parity checks (nonzero exit on fail)
 *   render <in.ppm> <out.ppm>      8-bit mock-host round trip, for cmp against
 *                                  tools/refrender on the same input
 *
 * Run from a directory where the pack resolves under the relative name "pack"
 * (i.e. spektra-core/data) — see run.sh.
 */
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pifilter.h"
#include "spektra_core.h" /* sf_gauss_iir_passes + its cap, for the cascade test */
#include "params.h"
#include "preset.h"
#include "pack_list.h"
#include "filter_loop.h"
#include "pix_convert.h"
#include "sf_parallel.h"
#include "sf_preview.h"

static int g_fail;

static void check(int ok, const char *what)
{
  if(!ok)
  {
    printf("  FAIL: %s\n", what);
    g_fail++;
  }
}

/* ----------------------------------------------------------- mock handle -- */
static char *g_param_cell;
static void *mock_lock(Handle h, Boolean moveHigh) { (void)moveHigh; return *h; }
static void mock_unlock(Handle h) { (void)h; }
static int32 mock_getsize(Handle h) { (void)h; return (int32)sizeof(sf_params_block); }

static HandleProcs g_hp;
static void init_hp(void)
{
  memset(&g_hp, 0, sizeof g_hp);
  g_hp.handleProcsVersion = 1;
  g_hp.numHandleProcs = 9;
  g_hp.getSizeProc = mock_getsize;
  g_hp.lockProc = mock_lock;
  g_hp.unlockProc = mock_unlock;
}

/* -------------------------------------------------------------- PPM I/O --- */
static int ppm_read(const char *path, int *w, int *h, unsigned char **px)
{
  FILE *f = fopen(path, "rb");
  int W = 0, H = 0, maxv = 0;
  if(!f) return -1;
  if(fscanf(f, "P6 %d %d %d", &W, &H, &maxv) != 3) { fclose(f); return -2; }
  (void)maxv;
  fgetc(f); /* single whitespace after the maxval */
  *w = W;
  *h = H;
  *px = (unsigned char *)malloc((size_t)W * H * 3);
  if(!*px || fread(*px, 1, (size_t)W * H * 3, f) != (size_t)W * H * 3)
  {
    free(*px);
    fclose(f);
    return -3;
  }
  fclose(f);
  return 0;
}

static int ppm_write(const char *path, int w, int h, const unsigned char *px)
{
  FILE *f = fopen(path, "wb");
  if(!f) return -1;
  fprintf(f, "P6\n%d %d\n255\n", w, h); /* byte-identical to refrender's writer */
  fwrite(px, 1, (size_t)w * h * 3, f);
  fclose(f);
  return 0;
}

/* ------------------------------------------------------- mock host driver - */
/* Run one full invocation of the portable selector bodies on an interleaved
 * RGB tile. `ipx`/`opx` are raw host buffers sized w*h*planes*{1,2} bytes. */
static OSErr run_filter(const sf_params_block *pb, void *ipx, void *opx,
                        int w, int h, int planes, int depth, int mode)
{
  FilterRecord fr;
  sf_params_block *cell;
  OSErr err;
  const int sample = (depth == 16) ? 2 : 1;

  memset(&fr, 0, sizeof fr);
  init_hp();
  cell = (sf_params_block *)malloc(sizeof *cell);
  if(!cell) return filterBadParameters;
  memcpy(cell, pb, sizeof *cell);
  g_param_cell = (char *)cell;

  fr.planes = (int16)planes;
  fr.filterRect.top = 0;
  fr.filterRect.left = 0;
  fr.filterRect.bottom = (int16)h;
  fr.filterRect.right = (int16)w;
  fr.imageMode = (int16)mode;
  fr.depth = depth;
  fr.inData = ipx;
  fr.outData = opx;
  fr.inRowBytes = w * planes * sample;
  fr.outRowBytes = w * planes * sample;
  fr.handleProcs = &g_hp;
  fr.parameters = &g_param_cell;

  err = sf_filter_start(&fr);
  if(err == noErr) err = sf_filter_continue(&fr);
  sf_filter_finish(&fr);

  free(cell);
  g_param_cell = NULL;
  return err;
}


/* ------------------------------------------------- host-variant driver ---- */
/* Same invocation, but lets a test vary how the host advertises the selection
 * and hands over the buffers:
 *   SF_T_USE32         host carries a BigDocumentStruct; the plugin opts in.
 *   SF_T_LEGACY_EMPTY  the selection lives only in filterRect32.
 *   SF_T_NULL_IN       inData is NULL and the source has been pre-copied into
 *                      outData (the "host copies source to destination" case
 *                      our PiPL invites with PIFilterDontCopyToDestinationBit=0).
 * *rect32_after receives the 32-bit out rect left behind after the run. */
enum
{
  SF_T_USE32 = 1,
  SF_T_NULL_IN = 2,
  SF_T_LEGACY_EMPTY = 4
};

static OSErr run_filter_ex(const sf_params_block *pb, void *ipx, void *opx,
                           int w, int h, int planes, int depth, int mode,
                           unsigned flags, VRect *rect32_after)
{
  FilterRecord fr;
  BigDocumentStruct bd;
  sf_params_block *cell;
  OSErr err;
  const int sample = (depth == 16) ? 2 : 1;

  memset(&fr, 0, sizeof fr);
  memset(&bd, 0, sizeof bd);
  init_hp();
  cell = (sf_params_block *)malloc(sizeof *cell);
  if(!cell) return filterBadParameters;
  memcpy(cell, pb, sizeof *cell);
  g_param_cell = (char *)cell;

  fr.planes = (int16)planes;
  if(!(flags & SF_T_LEGACY_EMPTY))
  {
    fr.filterRect.top = 0;
    fr.filterRect.left = 0;
    fr.filterRect.bottom = (int16)h;
    fr.filterRect.right = (int16)w;
  }
  if(flags & SF_T_USE32)
  {
    bd.PluginUsing32BitCoordinates = 1;
    bd.filterRect32.top = 0;
    bd.filterRect32.left = 0;
    bd.filterRect32.bottom = h;
    bd.filterRect32.right = w;
    fr.bigDocumentData = &bd;
  }
  fr.imageMode = (int16)mode;
  fr.depth = depth;
  fr.inData = (flags & SF_T_NULL_IN) ? NULL : ipx;
  fr.outData = opx;
  fr.inRowBytes = w * planes * sample;
  fr.outRowBytes = w * planes * sample;
  fr.handleProcs = &g_hp;
  fr.parameters = &g_param_cell;

  err = sf_filter_start(&fr);
  if(err == noErr) err = sf_filter_continue(&fr);
  sf_filter_finish(&fr);

  if(rect32_after) *rect32_after = bd.outRect32;

  free(cell);
  g_param_cell = NULL;
  return err;
}

/* ------------------------------------------------ AdvanceState mock host --- */
/* Emulates the host shape that needs the pull model (Affinity Photo): the
 * Continue buffers are NULL and the plug-in must drive the transfer through
 * AdvanceStateProc. Each pulled tile is presented as a packed single-plane
 * buffer, matching inColumnBytes == sample size; output tiles are copied back
 * into the interleaved host image when the next tile is pulled (or at the end),
 * standing in for the host committing the buffer. */
static FilterRecord *g_adv_fr;
static unsigned char *g_adv_in, *g_adv_out;
static int g_adv_w, g_adv_h, g_adv_planes, g_adv_bytes;
static unsigned char *g_adv_in_tile, *g_adv_out_tile;
static size_t g_adv_in_cap, g_adv_out_cap;
static int g_adv_out_pending;
static VRect g_adv_out_rect;
static int g_adv_out_plane, g_adv_out_rb;

static void mock_get_in_rect(VRect *r)
{
  if(g_adv_fr->bigDocumentData && g_adv_fr->bigDocumentData->PluginUsing32BitCoordinates)
    *r = g_adv_fr->bigDocumentData->inRect32;
  else
  {
    r->top = g_adv_fr->inRect.top; r->left = g_adv_fr->inRect.left;
    r->bottom = g_adv_fr->inRect.bottom; r->right = g_adv_fr->inRect.right;
  }
}

static void mock_get_out_rect(VRect *r)
{
  if(g_adv_fr->bigDocumentData && g_adv_fr->bigDocumentData->PluginUsing32BitCoordinates)
    *r = g_adv_fr->bigDocumentData->outRect32;
  else
  {
    r->top = g_adv_fr->outRect.top; r->left = g_adv_fr->outRect.left;
    r->bottom = g_adv_fr->outRect.bottom; r->right = g_adv_fr->outRect.right;
  }
}

static unsigned char *mock_grow(unsigned char **buf, size_t *cap, size_t need)
{
  if(need > *cap)
  {
    unsigned char *n = (unsigned char *)realloc(*buf, need);
    if(!n) return NULL;
    *buf = n; *cap = need;
  }
  return *buf;
}

static void mock_commit_out(void)
{
  int rw, rh, yy, xx, b;
  if(!g_adv_out_pending) return;
  rw = (int)(g_adv_out_rect.right - g_adv_out_rect.left);
  rh = (int)(g_adv_out_rect.bottom - g_adv_out_rect.top);
  for(yy = 0; yy < rh; yy++)
    for(xx = 0; xx < rw; xx++)
      for(b = 0; b < g_adv_bytes; b++)
        g_adv_out[((size_t)(g_adv_out_rect.top + yy) * g_adv_w +
                   (g_adv_out_rect.left + xx)) * g_adv_planes * g_adv_bytes +
                  g_adv_out_plane * g_adv_bytes + b] =
            g_adv_out_tile[(size_t)yy * g_adv_out_rb + (size_t)xx * g_adv_bytes + b];
  g_adv_out_pending = 0;
}

static OSErr mock_advance_state(void)
{
  VRect r;

  mock_commit_out();

  mock_get_in_rect(&r);
  if(r.right > r.left && r.bottom > r.top)
  {
    int rw = (int)(r.right - r.left), rh = (int)(r.bottom - r.top);
    int p = (int)g_adv_fr->inLoPlane;
    unsigned char *s = mock_grow(&g_adv_in_tile, &g_adv_in_cap,
                                 (size_t)rw * rh * g_adv_bytes);
    int yy, xx, b;
    if(!s) return memFullErr;
    for(yy = 0; yy < rh; yy++)
      for(xx = 0; xx < rw; xx++)
        for(b = 0; b < g_adv_bytes; b++)
          s[(size_t)yy * rw * g_adv_bytes + (size_t)xx * g_adv_bytes + b] =
              g_adv_in[((size_t)(r.top + yy) * g_adv_w + (r.left + xx)) *
                       g_adv_planes * g_adv_bytes + p * g_adv_bytes + b];
    g_adv_fr->inData = s;
    g_adv_fr->inRowBytes = rw * g_adv_bytes;
  }
  else
  {
    g_adv_fr->inData = NULL;
    g_adv_fr->inRowBytes = 0;
  }

  mock_get_out_rect(&r);
  if(r.right > r.left && r.bottom > r.top)
  {
    int rw = (int)(r.right - r.left), rh = (int)(r.bottom - r.top);
    unsigned char *s = mock_grow(&g_adv_out_tile, &g_adv_out_cap,
                                 (size_t)rw * rh * g_adv_bytes);
    if(!s) return memFullErr;
    memset(s, 0, (size_t)rw * rh * g_adv_bytes);
    g_adv_out_pending = 1;
    g_adv_out_rect = r;
    g_adv_out_plane = (int)g_adv_fr->outLoPlane;
    g_adv_out_rb = rw * g_adv_bytes;
    g_adv_fr->outData = s;
    g_adv_fr->outRowBytes = rw * g_adv_bytes;
  }
  else
  {
    g_adv_fr->outData = NULL;
    g_adv_fr->outRowBytes = 0;
  }
  return noErr;
}

static OSErr run_filter_pull(const sf_params_block *pb, void *ipx, void *opx,
                             int w, int h, int planes, int depth, int mode,
                             int in_tw, int in_th, int out_tw, int out_th)
{
  FilterRecord fr;
  BigDocumentStruct bd;
  sf_params_block *cell;
  OSErr err;
  const int sample = (depth == 16) ? 2 : 1;

  memset(&fr, 0, sizeof fr);
  memset(&bd, 0, sizeof bd);
  init_hp();
  cell = (sf_params_block *)malloc(sizeof *cell);
  if(!cell) return filterBadParameters;
  memcpy(cell, pb, sizeof *cell);
  g_param_cell = (char *)cell;

  bd.filterRect32.top = 0;
  bd.filterRect32.left = 0;
  bd.filterRect32.bottom = h;
  bd.filterRect32.right = w;
  fr.bigDocumentData = &bd;

  fr.planes = (int16)planes;
  fr.imageMode = (int16)mode;
  fr.depth = depth;
  fr.inTileWidth = (int16)in_tw;
  fr.inTileHeight = (int16)in_th;
  fr.outTileWidth = (int16)out_tw;
  fr.outTileHeight = (int16)out_th;
  fr.handleProcs = &g_hp;
  fr.parameters = &g_param_cell;
  fr.advanceState = mock_advance_state;

  g_adv_fr = &fr;
  g_adv_in = (unsigned char *)ipx;
  g_adv_out = (unsigned char *)opx;
  g_adv_w = w; g_adv_h = h; g_adv_planes = planes; g_adv_bytes = sample;
  g_adv_out_pending = 0;

  err = sf_filter_start(&fr);
  if(err == noErr) err = sf_filter_continue(&fr);
  mock_commit_out(); /* the host commits the last tile */
  sf_filter_finish(&fr);

  g_adv_fr = NULL;
  free(cell);
  g_param_cell = NULL;
  return err;
}

/* ------------------------------------------------ coupler diffusion (A2/A3) */
/* A2: the blur is a per-channel operator on an interleaved buffer; a delta in
 *     R must not leak into G/B (the old code indexed the interleaved buffer
 *     contiguously, mixing channels and rows).
 * A3: the reference applies a 0.03 tail at 200 um on top of the 20 um core, so
 *     the delta response must reach well beyond the core's ~5 px sigma. */
static void test_coupler_diffuse(void)
{
  const int W = 128, H = 128;
  const float pixel_um = 4.0f;
  const double cdiff = 20.0, ctail = 200.0, ctail_w = 0.03;
  const size_t n3 = (size_t)W * H * 3;
  float *corr = (float *)calloc(n3, sizeof(float));
  float *work = (float *)malloc(n3 * sizeof(float));
  float *mix = (float *)malloc(n3 * sizeof(float));
  float *scr = (float *)malloc((size_t)W * H * sizeof(float));
  const size_t ctr = ((size_t)(H / 2) * W + W / 2) * 3;
  const size_t off40 = ((size_t)(H / 2) * W + W / 2 + 40) * 3;
  size_t p;
  double sum;
  float v40, v40_core, dc;
  int leak = 0;

  printf("[coupler] impulse response, core+tail\n");
  corr[ctr + 0] = 1.0f;
  sp_coupler_diffuse(corr, work, mix, scr, W, H, pixel_um, cdiff, ctail, ctail_w);

  for(p = 0; p < (size_t)W * H; p++)
    if(corr[p * 3 + 1] != 0.0f || corr[p * 3 + 2] != 0.0f) leak = 1;
  sum = 0.0;
  for(p = 0; p < (size_t)W * H; p++) sum += corr[p * 3 + 0];
  v40 = corr[off40 + 0];
  printf("  isolation: G,B %s | centre=%.4g  at+40px=%.4g  energy=%.4f\n",
         leak ? "LEAK" : "clean", corr[ctr + 0], v40, sum);
  check(!leak, "channels leak (blur ignored the interleaved stride)");
  check(corr[ctr + 0] > 0.0f, "no centre response");
  check(fabs(sum - 1.0) < 0.05, "energy not preserved");

  /* Core only: the tail is the only thing that can put flux this far out. The
   * 20 um core is sigma 5 px, so 40 px out is deep in the tail region. */
  printf("[coupler] impulse response, core only\n");
  memset(corr, 0, n3 * sizeof(float));
  corr[ctr + 0] = 1.0f;
  sp_coupler_diffuse(corr, work, mix, scr, W, H, pixel_um, cdiff, ctail, 0.0);
  v40_core = corr[off40 + 0];
  printf("  centre=%.4g  at+40px=%.4g  tail/core=%.2fx\n", corr[ctr + 0],
         v40_core, v40_core > 0.0f ? v40 / v40_core : 0.0);
  check(corr[ctr + 0] > 0.0f, "core-only: no centre response");
  check(v40 > 3.0f * v40_core, "tail absent: far response barely above core-only");

  /* A Gaussian blur of a constant is the constant: no stride/normalisation
   * bug can survive this, whatever the sigma. */
  printf("[coupler] constant-field preservation\n");
  for(p = 0; p < n3; p++) corr[p] = 0.3f;
  sp_coupler_diffuse(corr, work, mix, scr, W, H, pixel_um, cdiff, ctail, ctail_w);
  dc = 0.0f;
  for(p = 0; p < n3; p++) dc = fmaxf(dc, fabsf(corr[p] - 0.3f));
  printf("  max deviation from 0.3: %.4g\n", dc);
  check(dc < 1e-3f, "constant field not preserved");

  free(corr);
  free(work);
  free(mix);
  free(scr);
}

/* ------------------------------------------------ adapter parity (host side) */
/* The mock-host path (params -> sp_chain, sample<->float, one-tile loop) must
 * reproduce a direct chain render of the same decoded floats exactly. */
static void test_adapter_parity(void)
{
  enum { W = 37, H = 23 };
  unsigned char in[W * H * 3], out[W * H * 3], out2[W * H * 3];
  static float fin[W * H * 3], fout[W * H * 3];
  sf_params_block pb;
  sp_chain_params_t cp;
  OSErr e;
  int i;
  unsigned int mism = 0;
  char *cerr = NULL;
  sp_chain_t *chain;

  for(i = 0; i < W * H * 3; i++) in[i] = (unsigned char)((i * 37 + 11) & 0xff);

  printf("[adapter] 8-bit mock host vs direct sp_chain_render\n");
  sf_params_init(&pb);
  e = run_filter(&pb, in, out, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "mock host start/continue/finish");

  sf_pixels_to_float(in, W * 3, W, H, 3, 8, fin);
  sf_params_to_chain(&pb, &cp);
  chain = sp_chain_create(&cp, &cerr);
  if(!chain)
  {
    printf("  FAIL: direct chain create: %s\n", cerr ? cerr : "?");
    free(cerr);
    g_fail++;
    return;
  }
  free(cerr);
  sp_chain_render(chain, fin, fout, W, H, 3);
  sp_chain_destroy(chain);
  sf_float_to_pixels(fout, out2, W * 3, W, H, 3, 8, 0);

  for(i = 0; i < W * H * 3; i++)
    if(out[i] != out2[i]) mism++;
  printf("  mismatching bytes: %u / %d\n", mism, W * H * 3);
  check(mism == 0, "mock host output differs from a direct chain render");
}

/* ---------------------------------------------------- 16-bit domain (A4) -- */
static void test_16bit_domain(void)
{
  unsigned short buf[12];
  float f[12];
  unsigned short enc[12];
  int i;

  printf("[16-bit] sample domain\n");
  for(i = 0; i < 4; i++)
  {
    buf[i * 3 + 0] = 32768; /* -> 1.0 */
    buf[i * 3 + 1] = 16384; /* -> 0.5 */
    buf[i * 3 + 2] = 0;
  }
  sf_pixels_to_float(buf, 4 * 3 * 2, 4, 1, 3, 16, f);
  printf("  decode 32768->%.4g  16384->%.4g\n", f[0], f[1]);
  check(f[0] == 1.0f, "16-bit white did not decode to 1.0");
  check(f[1] == 0.5f, "16-bit 16384 did not decode to 0.5");

  for(i = 0; i < 4; i++)
  {
    f[i * 3 + 0] = 1.0f;
    f[i * 3 + 1] = 0.5f;
    f[i * 3 + 2] = 2.0f; /* over-range -> clamp */
  }
  sf_float_to_pixels(f, enc, 4 * 3 * 2, 4, 1, 3, 16, 0);
  printf("  encode 1.0->%u  0.5->%u  clamp(2.0)->%u\n", enc[0], enc[1], enc[2]);
  check(enc[0] == 32768, "16-bit white encode");
  check(enc[1] == 16384, "16-bit mid encode");
  check(enc[2] == 32768, "16-bit clamp encode");

  /* Full 16-bit mock-host invocation (depth 16, imageMode RGB48). */
  {
    enum { W = 16, H = 12 };
    unsigned short in[W * H * 3], out[W * H * 3];
    sf_params_block pb;
    OSErr e;
    int changed = 0, oor = 0;
    for(i = 0; i < W * H * 3; i++) in[i] = (unsigned short)((i * 977) % 32769);
    sf_params_init(&pb);
    e = run_filter(&pb, in, out, W, H, 3, 16, plugInModeRGB48);
    printf("  mock host RGB48: err=%d\n", (int)e);
    check(e == noErr, "16-bit mock host returned an error");
    for(i = 0; i < W * H * 3; i++)
    {
      if(out[i] > 32768) oor = 1;
      if(out[i] != in[i]) changed = 1;
    }
    check(changed, "16-bit render produced no change");
    check(!oor, "16-bit output exceeded 32768");
  }
}

/* -------------------------------------------------- input transfer (A5) -- */
/* The byte<->linear transfer is applied once inside sp_chain_render, selected
 * by sp_chain_params_t.input_transfer. The adapter only scales samples to 0..1,
 * so choosing a different transfer must change the render, and sRGB decode of a
 * mid-grey code value must differ from feeding the same value as linear. */
static void test_input_transfer(void)
{
  enum { W = 21, H = 15 };
  unsigned char in[W * H * 3], outs[W * H * 3], outl[W * H * 3];
  sf_params_block pb;
  OSErr e;
  int i, diff = 0;

  printf("[transfer] sRGB vs linear input\n");
  for(i = 0; i < W * H * 3; i++) in[i] = (unsigned char)((i * 53 + 7) & 0xff);

  sf_params_init(&pb);
  pb.input_transfer = SP_TRANSFER_SRGB;
  e = run_filter(&pb, in, outs, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "sRGB render failed");

  sf_params_init(&pb);
  pb.input_transfer = SP_TRANSFER_LINEAR;
  e = run_filter(&pb, in, outl, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "linear render failed");

  for(i = 0; i < W * H * 3; i++)
    if(outs[i] != outl[i]) diff++;
  printf("  differing bytes: %d / %d\n", diff, W * H * 3);
  check(diff > 0, "input_transfer had no effect on the render");

  /* sRGB and linear must also disagree at a single mid-grey input. */
  {
    sp_chain_params_t cp;
    char *cerr = NULL;
    sp_chain_t *chain;
    float g[3] = { 0.5f, 0.5f, 0.5f }, rs[3] = { 0, 0, 0 }, rl[3] = { 0, 0, 0 };

    sf_params_init(&pb);
    sf_params_to_chain(&pb, &cp);
    cp.input_transfer = SP_TRANSFER_SRGB;
    chain = sp_chain_create(&cp, &cerr);
    check(chain != NULL, "chain create (sRGB)");
    if(chain) { sp_chain_render(chain, g, rs, 1, 1, 3); sp_chain_destroy(chain); }
    free(cerr);
    cerr = NULL;

    cp.input_transfer = SP_TRANSFER_LINEAR;
    chain = sp_chain_create(&cp, &cerr);
    check(chain != NULL, "chain create (linear)");
    if(chain) { sp_chain_render(chain, g, rl, 1, 1, 3); sp_chain_destroy(chain); }
    free(cerr);

    printf("  mid-grey 0.5: sRGB->%.4f  linear->%.4f\n", rs[0], rl[0]);
    check(rs[0] != rl[0], "sRGB and linear disagree at mid-grey");
  }
}

/* ------------------------------------- input gamuts + transfer curves (v7) -- */
/* The input surface is now the full OFX set, split into gamut (primaries) and
 * transfer (curve), both indexed by the shared sp_color.h tables. The indices
 * are persisted, so pin the properties that must hold for every entry: each
 * curve is a monotonic decode whose encode inverts it, each gamut yields a
 * finite matrix, index 0 is the byte-identical sRGB default, and a wide gamut
 * actually reaches the render. */
static void test_color_surface(void)
{
  static const double xs[] = {0.0, 0.001, 0.01, 0.05, 0.18, 0.5, 1.0};
  const int nx = (int)(sizeof xs / sizeof xs[0]);
  int t, i;

  printf("[colour] input gamuts + transfer curves\n");
  for(t = 0; t < sp_transfer_count(); t++)
  {
    float prev = -1e30f;
    for(i = 0; i < nx; i++)
    {
      const float x = (float)xs[i];
      const float y = sp_transfer_encode(t, x);
      const float back = sp_transfer_decode(t, y);
      const float tol = 1e-3f + 1e-3f * fabsf(x);
      if(fabsf(back - x) > tol)
      {
        printf("  FAIL: %s round trip x=%.4f -> %.6f -> %.6f\n",
               sp_transfer_label(t), x, y, back);
        g_fail++;
      }
      if(sp_transfer_decode(t, x) < prev)
      {
        printf("  FAIL: %s decode not monotonic at x=%.4f\n",
               sp_transfer_label(t), x);
        g_fail++;
      }
      prev = sp_transfer_decode(t, x);
    }
  }

  {
    double m[9], w[2];
    sp_color_space_matrix(0, m, w);
    check(m[0] == 0.4124 && m[4] == 0.7152 && m[8] == 0.9505 &&
              w[0] == 0.3127 && w[1] == 0.3290,
          "gamut 0 is the Phase-1 sRGB matrix");
    for(i = 0; i < sp_color_space_count(); i++)
    {
      int k, finite = 1;
      sp_color_space_matrix(i, m, w);
      for(k = 0; k < 9; k++) if(!isfinite(m[k])) finite = 0;
      if(!isfinite(w[0]) || !isfinite(w[1])) finite = 0;
      check(finite, sp_color_space_label(i));
    }
    printf("  %d gamuts, %d transfer curves\n", sp_color_space_count(),
           sp_transfer_count());
    check(sp_color_space_count() >= 10 && sp_transfer_count() >= 10,
          "gamut/curve tables cover the OFX surface");
  }

  /* A wide gamut must actually change the render (ProPhoto, index 7). */
  {
    enum { W = 9, H = 7 };
    unsigned char in[W * H * 3], a[W * H * 3], b[W * H * 3];
    sf_params_block pb;
    int diff = 0;
    for(i = 0; i < W * H * 3; i++)
      in[i] = (unsigned char)(i % 3 == 0 ? 240 : (i % 3 == 1 ? 30 : 90));
    sf_params_init(&pb);
    pb.input_space = 0;
    check(run_filter(&pb, in, a, W, H, 3, 8, plugInModeRGBColor) == noErr,
          "sRGB gamut render");
    sf_params_init(&pb);
    pb.input_space = 7; /* ProPhoto RGB */
    check(run_filter(&pb, in, b, W, H, 3, 8, plugInModeRGBColor) == noErr,
          "ProPhoto gamut render");
    for(i = 0; i < W * H * 3; i++) if(a[i] != b[i]) diff++;
    printf("  gamut sRGB vs ProPhoto differing bytes: %d / %d\n", diff,
           W * H * 3);
    check(diff > 0, "input gamut had no effect on the render");
  }
}

/* ------------------------------------------------ film format (gauge) -- */
/* Film format turns a gauge in mm and the image's long edge into a film pixel
 * size (the OFX filmFormat / pixelSizeUmForRender); index 0 ("Custom") keeps the
 * manual pixel_um so the default render is unchanged. The chain is where the
 * derivation lands, driven by the full image long edge the host supplies. */
static void test_film_format(void)
{
  static const double mm[8] = {4.8, 5.79, 10.26, 12.52, 35.0, 24.89, 52.48,
                               70.41};
  int i;
  printf("[format] film gauge -> film pixel size\n");
  check(sp_film_format_count() == 9,
        "nine film-format entries (Custom + 8 gauges)");
  check(sp_film_format_mm(0) == 0.0, "index 0 is Custom (mm 0)");
  for(i = 0; i < 8; i++)
    check(sp_film_format_mm(i + 1) == mm[i], sp_film_format_label(i + 1));
  check(fabs(sp_pixel_um_for_format(35.0, 7000.0) - 5.0) < 1e-9,
        "35mm / 7000px -> 5um");
  check(sp_pixel_um_for_format(0.0, 7000.0) == 0.0, "no gauge -> no derivation");
  check(sp_pixel_um_for_format(35.0, 0.0) == 0.0, "no edge -> no derivation");
  check(fabs(sp_grain_format_scale(35.0) - 1.0) < 1e-12, "35mm grain scale is 1");
  check(fabs(sp_grain_format_scale(0.0) - 1.0) < 1e-12, "Custom grain scale is 1");

  /* A selected gauge overrides the manual pixel size and reaches the render. */
  {
    enum { W = 64, H = 64 };
    unsigned char *in = malloc(W * H * 3), *a = malloc(W * H * 3),
                  *b = malloc(W * H * 3);
    sf_params_block pb;
    int diff = 0;
    for(i = 0; i < W * H * 3; i++) in[i] = (unsigned char)((i * 37 + 11) & 0xff);
    sf_params_init(&pb);
    pb.film_format = 0; /* Custom */
    check(run_filter(&pb, in, a, W, H, 3, 8, plugInModeRGBColor) == noErr,
          "Custom film-format render");
    sf_params_init(&pb);
    pb.film_format = 5; /* Standard35 (OFX index 4 + 1) */
    check(run_filter(&pb, in, b, W, H, 3, 8, plugInModeRGBColor) == noErr,
          "35mm film-format render");
    for(i = 0; i < W * H * 3; i++) if(a[i] != b[i]) diff++;
    printf("  Custom vs 35mm differing bytes: %d / %d\n", diff, W * H * 3);
    check(diff > 0, "film format had no effect on the render");
    free(in);
    free(a);
    free(b);
  }
}

/* ---------------------------------------------------- Phase 2 spatial look -- */
/* Every stage defaults off, so a default block renders exactly the Phase-1
 * slice; enabling a stage must visibly change the render (proving it is wired
 * through the engine). With every stage on, the threaded path must stay
 * byte-identical to serial -- the guarantee the shipping .8bf relies on. */
static int count_diff(const unsigned char *a, const unsigned char *b, int n)
{
  int i, d = 0;
  for(i = 0; i < n; i++)
    if(a[i] != b[i]) d++;
  return d;
}

static void test_phase2_stages(void)
{
  enum { W = 48, H = 40, N = W * H * 3 };
  unsigned char in[N], base[N], out[N], ser[N], par[N];
  sf_params_block pb, all;
  OSErr e;
  int i;

  printf("[phase2] spatial look stages (default off, wired, thread-safe)\n");
  for(i = 0; i < N; i++) in[i] = (unsigned char)((i * 37 + 11) & 0xff);

  sf_params_init(&pb);
  e = run_filter(&pb, in, base, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "baseline render failed");

#define TRY_STAGE(label, setup)                                                   \
  do                                                                              \
  {                                                                               \
    int d;                                                                        \
    sf_params_init(&pb);                                                         \
    setup;                                                                        \
    e = run_filter(&pb, in, out, W, H, 3, 8, plugInModeRGBColor);                 \
    check(e == noErr, label " render failed");                                   \
    d = count_diff(out, base, N);                                                 \
    printf("  %-17s differing bytes: %d / %d\n", label, d, N);                    \
    check(d > 0, label " had no effect on the render");                          \
  } while(0)

  TRY_STAGE("boost", pb.boost_ev = 1.5f);
  TRY_STAGE("camera diffusion", pb.diffusion_on = 1; pb.diffusion_strength = 1.0f);
  TRY_STAGE("halation", pb.halation_on = 1);
  TRY_STAGE("grain", pb.grain_on = 1);
  TRY_STAGE("print diffusion", pb.print_diffusion_on = 1; pb.print_diffusion_strength = 1.0f);
  TRY_STAGE("scanner mtf", pb.scan_mtf50_lpmm = 60.0);
  TRY_STAGE("scanner usm", pb.scan_usm_radius_um = 5.0; pb.scan_usm_amount = 1.5f);
  TRY_STAGE("glare", pb.glare_percent = 3.0f);
#undef TRY_STAGE

  /* Every stage on: serial vs threaded must agree byte-for-byte. */
  sf_params_init(&all);
  all.boost_ev = 1.0f;
  all.diffusion_on = 1; all.diffusion_strength = 1.0f;
  all.halation_on = 1;
  all.grain_on = 1;
  all.print_diffusion_on = 1; all.print_diffusion_strength = 1.0f;
  all.scan_mtf50_lpmm = 60.0; all.scan_usm_radius_um = 5.0; all.scan_usm_amount = 1.5f;
  all.glare_percent = 3.0f;

  e = run_filter(&all, in, ser, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "all-stages serial render failed");
  sf_parallel_install();
  e = run_filter(&all, in, par, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "all-stages threaded render failed");
  printf("  all stages on: threaded vs serial differing bytes: %d / %d\n",
         count_diff(ser, par, N), N);
  check(count_diff(ser, par, N) == 0, "threaded Phase 2 render differs from serial");
}

/* Film/print chemistry (OFX filmPushPullStops / filmGamma / printGamma): each
 * control must change the render when set and be a no-op at its default. */
static void test_chem_stages(void)
{
  enum { W = 48, H = 40, N = W * H * 3 };
  unsigned char in[N], base[N], out[N];
  sf_params_block pb;
  OSErr e;
  int i;

  printf("[chem] film/print curve chemistry\n");
  for(i = 0; i < N; i++) in[i] = (unsigned char)((i * 37 + 11) & 0xff);

  sf_params_init(&pb);
  check(pb.film_push_pull_stops == 0.0 && pb.film_gamma == 1.0 &&
            pb.print_gamma == 1.0 && pb.print_push_pull_stops == 0.0 &&
            pb.print_shadow_shape == 0.0 && pb.print_highlight_shape == 0.0 &&
            pb.filter_c_shift == 0.0,
        "chemistry defaults are not neutral");
  e = run_filter(&pb, in, base, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "chemistry baseline render failed");

#define TRY_CHEM(label, setup)                                                    \
  do                                                                              \
  {                                                                               \
    int d;                                                                        \
    sf_params_init(&pb);                                                         \
    setup;                                                                        \
    check(sf_params_validate(&pb) == 0, label " block failed validation");         \
    e = run_filter(&pb, in, out, W, H, 3, 8, plugInModeRGBColor);                 \
    check(e == noErr, label " render failed");                                    \
    d = count_diff(base, out, N);                                                 \
    printf("  %-17s differing bytes: %d / %d\n", label, d, N);                    \
    check(d > 0, label " did not change the render");                             \
  } while(0)

  TRY_CHEM("push +1.5", pb.film_push_pull_stops = 1.5);
  TRY_CHEM("pull -1.5", pb.film_push_pull_stops = -1.5);
  TRY_CHEM("film gamma 1.3", pb.film_gamma = 1.3);
  TRY_CHEM("print gamma 0.8", pb.print_gamma = 0.8);
  TRY_CHEM("print push +1", pb.print_push_pull_stops = 1.0);
  TRY_CHEM("print push -1", pb.print_push_pull_stops = -1.0);
  TRY_CHEM("shadow shape -0.5", pb.print_shadow_shape = -0.5);
  TRY_CHEM("highlight shape 0.5", pb.print_highlight_shape = 0.5);
  TRY_CHEM("cyan filter 30", pb.filter_c_shift = 30.0);
#undef TRY_CHEM
}

/* Grain surface (OFX GrainParams): model/sublayer split, per-channel and
 * per-layer overrides, and the absolute um blur overrides. Every field must be
 * a no-op at its default and change the render when set. */
static void test_grain_surface(void)
{
  enum { W = 48, H = 40, N = W * H * 3 };
  unsigned char in[N], base[N], out[N];
  sf_params_block pb;
  OSErr e;
  int i;

  printf("[grain] surface: model, sublayers, per-channel/layer scales, um blur\n");
  for(i = 0; i < N; i++) in[i] = (unsigned char)((i * 29 + 5) & 0xff);

  sf_params_init(&pb);
  check(pb.grain_model == 1 && pb.grain_sublayers_enabled == 1 &&
            pb.grain_sublayer_count == 0 && pb.grain_final_blur_um < 0.0f &&
            pb.grain_dye_cloud_um < 0.0f && pb.grain_particle_scale[0] == 1.0f &&
            pb.grain_uniformity_rgb[2] == 1.0f && pb.grain_density_min_rgb[1] == 1.0f,
        "grain surface defaults are not neutral");

  /* With grain on, explicitly re-writing every surface default must reproduce
   * the stock render byte-for-byte (the no-op contract). */
  sf_params_init(&pb);
  pb.grain_on = 1;
  e = run_filter(&pb, in, base, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "grain baseline render failed");
  {
    sf_params_block q;
    sf_params_init(&q);
    q.grain_on = 1;
    q.grain_model = 1;
    q.grain_sublayers_enabled = 1;
    q.grain_sublayer_count = 0;
    q.grain_final_blur_um = -1.0f;
    q.grain_dye_cloud_um = -1.0f;
    q.grain_particle_scale[0] = q.grain_particle_scale[1] = q.grain_particle_scale[2] = 1.0f;
    q.grain_particle_scale_layer[0] = q.grain_particle_scale_layer[1] =
        q.grain_particle_scale_layer[2] = 1.0f;
    q.grain_uniformity_rgb[0] = q.grain_uniformity_rgb[1] = q.grain_uniformity_rgb[2] = 1.0f;
    q.grain_density_min_rgb[0] = q.grain_density_min_rgb[1] = q.grain_density_min_rgb[2] = 1.0f;
    check(sf_params_validate(&q) == 0, "grain explicit-default block failed validation");
    e = run_filter(&q, in, out, W, H, 3, 8, plugInModeRGBColor);
    check(e == noErr, "grain explicit-default render failed");
    check(count_diff(base, out, N) == 0, "grain explicit defaults changed the render");
  }

#define TRY_GRAIN(label, setup)                                                   \
  do                                                                              \
  {                                                                               \
    int d;                                                                        \
    sf_params_init(&pb);                                                          \
    pb.grain_on = 1;                                                              \
    setup;                                                                        \
    check(sf_params_validate(&pb) == 0, label " block failed validation");        \
    e = run_filter(&pb, in, out, W, H, 3, 8, plugInModeRGBColor);                  \
    check(e == noErr, label " render failed");                                    \
    d = count_diff(base, out, N);                                                  \
    printf("  %-18s differing bytes: %d / %d\n", label, d, N);                     \
    check(d > 0, label " did not change the render");                              \
  } while(0)

  TRY_GRAIN("model preview", pb.grain_model = 0);
  TRY_GRAIN("sublayers off", pb.grain_sublayers_enabled = 0);
  TRY_GRAIN("sublayer count 1", pb.grain_sublayer_count = 1);
  TRY_GRAIN("particle R", pb.grain_particle_scale[0] = 2.0f);
  TRY_GRAIN("particle layer 0", pb.grain_particle_scale_layer[0] = 2.0f);
  TRY_GRAIN("uniformity B", pb.grain_uniformity_rgb[2] = 0.5f);
  TRY_GRAIN("density min G", pb.grain_density_min_rgb[1] = 0.5f);
  TRY_GRAIN("final blur um", pb.grain_final_blur_um = 8.0f);
  TRY_GRAIN("dye cloud um", pb.grain_dye_cloud_um = 6.0f);
  TRY_GRAIN("seed 123", pb.grain_seed = 123);
  TRY_GRAIN("saturation 0", pb.grain_saturation = 0.0f);
#undef TRY_GRAIN
}

/* Per-channel halation first-bounce radius (halationFirstSigmaUmR/G/B in the
 * source): pin the defaults (stock = negative), the no-op contract, that each
 * channel moves the render independently, and the block <-> chain projection.
 * The blue channel needs a nonzero return weight to show up at all (the stock
 * defaults its blue halation weight to 0), so that case renders its own
 * single-variable baseline. */
static void test_halation_surface(void)
{
  enum { W = 48, H = 40, N = W * H * 3 };
  unsigned char in[N], base[N], out[N], blue_base[N];
  sf_params_block pb, q;
  sp_chain_params_t cp;
  OSErr e;
  int i;

  printf("[halation] per-channel first sigma\n");
  for(i = 0; i < N; i++) in[i] = (unsigned char)((i * 29 + 5) & 0xff);

  sf_params_init(&pb);
  check(pb.halation_first_sigma_um[0] < 0.0 && pb.halation_first_sigma_um[1] < 0.0 &&
            pb.halation_first_sigma_um[2] < 0.0,
        "halation first sigma defaults are not the stock's own");

  /* Stock halation render. */
  sf_params_init(&pb);
  pb.halation_on = 1;
  e = run_filter(&pb, in, base, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "halation baseline render failed");

  /* Explicitly re-writing each channel to -1 must reproduce it byte-for-byte. */
  sf_params_init(&q);
  q.halation_on = 1;
  q.halation_first_sigma_um[0] = q.halation_first_sigma_um[1] = q.halation_first_sigma_um[2] = -1.0;
  check(sf_params_validate(&q) == 0, "halation explicit-default block failed validation");
  e = run_filter(&q, in, out, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "halation explicit-default render failed");
  check(count_diff(base, out, N) == 0, "halation explicit defaults changed the render");

#define TRY_HAL(label, setup)                                                     \
  do                                                                              \
  {                                                                               \
    int d;                                                                        \
    sf_params_init(&pb);                                                          \
    pb.halation_on = 1;                                                           \
    setup;                                                                        \
    check(sf_params_validate(&pb) == 0, label " block failed validation");        \
    e = run_filter(&pb, in, out, W, H, 3, 8, plugInModeRGBColor);                  \
    check(e == noErr, label " render failed");                                    \
    d = count_diff(base, out, N);                                                  \
    printf("  %-18s differing bytes: %d / %d\n", label, d, N);                     \
    check(d > 0, label " did not change the render");                              \
  } while(0)

  TRY_HAL("first sigma R", pb.halation_first_sigma_um[0] = 32.0f);
  TRY_HAL("first sigma G", pb.halation_first_sigma_um[1] = 32.0f);
#undef TRY_HAL

  /* Blue: baseline with a nonzero blue return weight but the stock radius... */
  sf_params_init(&q);
  q.halation_on = 1;
  q.halation_strength[2] = 0.02;
  e = run_filter(&q, in, blue_base, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "halation blue baseline render failed");
  /* ... then the same weight with the blue radius overridden. */
  sf_params_init(&pb);
  pb.halation_on = 1;
  pb.halation_strength[2] = 0.02;
  pb.halation_first_sigma_um[2] = 32.0f;
  check(sf_params_validate(&pb) == 0, "blue first sigma block failed validation");
  e = run_filter(&pb, in, out, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "blue first sigma render failed");
  {
    int d = count_diff(blue_base, out, N);
    printf("  %-18s differing bytes: %d / %d\n", "blue first sigma", d, N);
    check(d > 0, "blue first sigma did not change the render");
  }

  /* Projection into the chain params, and the validation ceiling. */
  sf_params_init(&pb);
  pb.halation_first_sigma_um[0] = 40.0;
  pb.halation_first_sigma_um[1] = -1.0;
  pb.halation_first_sigma_um[2] = 25.0;
  sf_params_to_chain(&pb, &cp);
  check(cp.halation_first_sigma_um[0] == 40.0 && cp.halation_first_sigma_um[1] == -1.0 &&
            cp.halation_first_sigma_um[2] == 25.0,
        "halation first sigma not projected into chain params");
  pb.halation_first_sigma_um[0] = 100.0;
  check(sf_params_validate(&pb) == 40, "out-of-range halation first sigma accepted");

  /* Independent max-clamp ceiling: default is -1 (stock 65 um). */
  sf_params_init(&pb);
  check(pb.halation_sigma_max_um < 0.0, "halation sigma max not the stock sentinel by default");
  pb.halation_sigma_max_um = 40.0;
  pb.halation_first_sigma_um[0] = 50.0;
  check(sf_params_validate(&pb) == 40, "halation sigma above lowered max accepted");
  pb.halation_sigma_max_um = -1.0;
  pb.halation_first_sigma_um[0] = 200.0;
  check(sf_params_validate(&pb) == 40, "halation sigma above stock max accepted");
  sf_params_init(&pb);
  pb.halation_sigma_max_um = 200.0;
  pb.halation_first_sigma_um[0] = 100.0;
  check(sf_params_validate(&pb) == 0, "halation sigma within raised max rejected");
  pb.halation_sigma_max_um = 0.0;
  check(sf_params_validate(&pb) == 41, "zero halation max accepted");
  pb.halation_sigma_max_um = 5000.0;
  check(sf_params_validate(&pb) == 41, "out-of-range halation max accepted");

  /* Blur scale multipliers: the sliders' own range, because a preset read from
   * disk or the clipboard never passes a slider and an unbounded multiplier is
   * an unbounded blur (halation's cost is quadratic in the radius). */
#define TRY_SCALE(field)                                                          \
  do                                                                              \
  {                                                                               \
    sf_params_init(&pb);                                                          \
    pb.field = 4.0f;                                                              \
    check(sf_params_validate(&pb) == 0, "scale at the top of its range rejected"); \
    pb.field = 4.5f;                                                              \
    check(sf_params_validate(&pb) == 43, "over-range scale accepted");            \
    pb.field = -0.5f;                                                             \
    check(sf_params_validate(&pb) == 43, "negative scale accepted");              \
  } while(0)
  TRY_SCALE(halation_scale);
  TRY_SCALE(scatter_scale);
  TRY_SCALE(diffusion_scale);
  TRY_SCALE(print_diffusion_scale);
#undef TRY_SCALE
}

/* ------------------------------------------------- progress + abort (A6) -- */
static int g_prog_calls;
static int g_prog_last;
static int g_abort_after; /* abort once this many progress calls have happened */

static void PASCAL test_progress_proc(int32 done, int32 total)
{
  (void)total;
  g_prog_calls++;
  g_prog_last = (int)done;
}
static Boolean PASCAL test_abort_proc(void)
{
  return (g_abort_after > 0 && g_prog_calls >= g_abort_after) ? 1 : 0;
}

/* Like run_filter, but with the host progress/abort callbacks installed. */
static OSErr run_filter_cb(const sf_params_block *pb, void *ipx, void *opx,
                           int w, int h, int planes, int depth, int mode)
{
  FilterRecord fr;
  sf_params_block *cell;
  OSErr err;
  const int sample = (depth == 16) ? 2 : 1;

  memset(&fr, 0, sizeof fr);
  init_hp();
  cell = (sf_params_block *)malloc(sizeof *cell);
  if(!cell) return filterBadParameters;
  memcpy(cell, pb, sizeof *cell);
  g_param_cell = (char *)cell;

  fr.planes = (int16)planes;
  fr.filterRect.top = 0;
  fr.filterRect.left = 0;
  fr.filterRect.bottom = (int16)h;
  fr.filterRect.right = (int16)w;
  fr.imageMode = (int16)mode;
  fr.depth = depth;
  fr.inData = ipx;
  fr.outData = opx;
  fr.inRowBytes = w * planes * sample;
  fr.outRowBytes = w * planes * sample;
  fr.handleProcs = &g_hp;
  fr.parameters = &g_param_cell;
  fr.progressProc = test_progress_proc;
  fr.abortProc = test_abort_proc;

  err = sf_filter_start(&fr);
  if(err == noErr) err = sf_filter_continue(&fr);
  sf_filter_finish(&fr);

  free(cell);
  g_param_cell = NULL;
  return err;
}

/* The cascade that runs any blur wider than the recursive filter's coefficient
 * limit. It used to derive its pass count straight into an int, and past sigma
 * ~6.95e6 px that overflowed to a negative number: the pass loop then ran zero
 * times, so the stage handed back the unblurred plane and reported nothing.
 * The count now saturates, so check both halves of the promise: bounded work
 * for anything a caller can hand it, and the requested width for every radius
 * the parameter surface can actually ask for. */
static void test_iir_cascade(void)
{
  const float exact[] = { 1500.0f, 8660.0f, 13856.0f };
  const float wild[] = { 1e30f, FLT_MAX, INFINITY, -INFINITY, NAN };
  float ps;
  int n;
  size_t i;

  printf("[cascade] recursive Gaussian pass count saturates\n");

  /* At or below the coefficient limit: one pass, the requested sigma. */
  n = sf_gauss_iir_passes(0.3f, &ps);
  check(n == 1 && ps == 0.3f, "cascade is not one pass below the limit");
  n = sf_gauss_iir_passes(SF_GAUSS_MAX_IIR_SIGMA, &ps);
  check(n == 1 && ps == SF_GAUSS_MAX_IIR_SIGMA, "cascade is not one pass at the limit");

  /* Above it the cascade composes back to the request. The three are the widest
   * radii the sliders reach: diffusion's bloom at the finest pitch, and
   * halation at its ceiling with the scale multiplier at maximum. */
  for(i = 0; i < sizeof exact / sizeof exact[0]; i++)
  {
    n = sf_gauss_iir_passes(exact[i], &ps);
    printf("  sigma %9.0f px -> %6d passes of %6.1f (delivered %.1f)\n", exact[i], n, ps,
           sqrtf((float)n) * ps);
    check(n > 1 && fabsf(sqrtf((float)n) * ps - exact[i]) < 1e-2f * exact[i],
          "cascade does not deliver the requested sigma");
  }

  /* Past the cap the count saturates, and with it the delivered width. */
  n = sf_gauss_iir_passes(1e9f, &ps);
  printf("  sigma %9.0e px -> %6d passes of %6.1f (delivered %.0f)\n", 1e9, n, ps,
         sqrtf((float)n) * ps);
  check(n == SF_GAUSS_MAX_IIR_PASSES, "cascade does not saturate at the cap");
  check(ps == SF_GAUSS_MAX_IIR_SIGMA, "saturated cascade does not pass at the limit");
  check(fabsf(sqrtf((float)n) * ps - SF_GAUSS_MAX_IIR_SIGMA * sqrtf((float)SF_GAUSS_MAX_IIR_PASSES))
            < 1.0f,
        "saturated cascade does not deliver the cap's width");

  /* Totality: a count that always runs the stage, and a per-pass sigma the
   * coefficient builder will accept. A hand-made params block can bring NaN or
   * infinity here. */
  for(i = 0; i < sizeof wild / sizeof wild[0]; i++)
  {
    n = sf_gauss_iir_passes(wild[i], &ps);
    check(n >= 1 && n <= SF_GAUSS_MAX_IIR_PASSES, "cascade count out of range");
    check(!(ps > SF_GAUSS_MAX_IIR_SIGMA), "cascade passes above the limit");
  }
}

static void test_progress_abort(void)
{
  enum { W = 40, H = 30 };
  unsigned char in[W * H * 3], out[W * H * 3];
  sf_params_block pb;
  OSErr e;
  int i;

  printf("[progress] progress reported + host cancel honoured\n");
  for(i = 0; i < W * H * 3; i++) in[i] = (unsigned char)((i * 29 + 5) & 0xff);

  g_prog_calls = 0;
  g_prog_last = -1;
  g_abort_after = 0;
  sf_params_init(&pb);
  e = run_filter_cb(&pb, in, out, W, H, 3, 8, plugInModeRGBColor);
  printf("  complete run: err=%d progress=%d last=%d\n", (int)e, g_prog_calls,
         g_prog_last);
  check(e == noErr, "progress run failed");
  check(g_prog_calls > 1, "progress was not reported during the render");
  check(g_prog_last == 100, "progress did not reach 100");

  g_prog_calls = 0;
  g_prog_last = -1;
  g_abort_after = 1;
  sf_params_init(&pb);
  e = run_filter_cb(&pb, in, out, W, H, 3, 8, plugInModeRGBColor);
  printf("  abort at first stage: err=%d progress=%d\n", (int)e, g_prog_calls);
  check(e == userCanceledErr, "host cancel was not honoured");
}

/* ------------------------------------------------- host-variant handshake -- */
/* The Windows/Photoshop handshake has two shapes the loop must survive:
 *   - a modern host that keeps the selection in the 32-bit *Rect32 fields and
 *     reads the request back from them (the path gmic-8bf uses);
 *   - a host that leaves inData NULL and pre-copies the source into outData
 *     (the "copied source to destination" case our PiPL invites).
 * Both must produce byte-identical output to the plain legacy two-buffer path,
 * and the 32-bit request must be emptied afterwards so the host stops calling
 * Continue. */
static void test_host_paths(void)
{
  enum { W = 31, H = 19 };
  unsigned char in[W * H * 3], base[W * H * 3], a[W * H * 3], b[W * H * 3];
  sf_params_block pb;
  OSErr e;
  VRect after;
  int i, mism32 = 0, mism1 = 0;

  printf("[hosts] 32-bit coords + single pre-copied buffer\n");
  for(i = 0; i < W * H * 3; i++) in[i] = (unsigned char)((i * 131 + 7) & 0xff);
  sf_params_init(&pb);

  e = run_filter(&pb, in, base, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "baseline legacy run failed");

  after.top = after.left = after.bottom = after.right = -1;
  e = run_filter_ex(&pb, in, a, W, H, 3, 8, plugInModeRGBColor,
                    SF_T_USE32 | SF_T_LEGACY_EMPTY, &after);
  check(e == noErr, "32-bit-coordinate run failed");
  check(after.top == 0 && after.left == 0 && after.bottom == 0 &&
            after.right == 0,
        "32-bit request rects not emptied after render");
  for(i = 0; i < W * H * 3; i++)
    if(a[i] != base[i]) mism32++;
  check(mism32 == 0, "32-bit-coordinate output differs from the legacy path");

  memcpy(b, in, sizeof in); /* host pre-copied the source into outData */
  e = run_filter_ex(&pb, in, b, W, H, 3, 8, plugInModeRGBColor,
                    SF_T_NULL_IN, NULL);
  check(e == noErr, "single-buffer run failed");
  for(i = 0; i < W * H * 3; i++) if(b[i] != base[i]) mism1++;
  check(mism1 == 0, "single-buffer output differs from the two-buffer path");

  printf("  mismatching bytes: 32-bit=%d single-buffer=%d\n", mism32, mism1);
}

/* ----------------------------------------------- AdvanceState pull model -- */
/* Affinity Photo does not pre-fill inData/outData on Continue; it fills only
 * the tiles the plug-in pulls through AdvanceStateProc. The loop must detect
 * the missing buffers, gather/render/scatter via advanceState over the host's
 * own (here deliberately tiny) tile size, and still match the two-buffer host
 * byte for byte — 8-bit and 16-bit. */
static void test_pull_path(void)
{
  enum { W = 33, H = 21 };
  unsigned char in[W * H * 3], base[W * H * 3], out[W * H * 3];
  sf_params_block pb;
  OSErr e;
  int i, mism = 0;

  printf("[hosts] AdvanceState pull model (no pre-filled Continue buffers)\n");
  for(i = 0; i < W * H * 3; i++) in[i] = (unsigned char)((i * 173 + 29) & 0xff);
  sf_params_init(&pb);
  e = run_filter(&pb, in, base, W, H, 3, 8, plugInModeRGBColor);
  check(e == noErr, "baseline legacy run failed");

  /* Small, unequal tile sizes force the multi-tile gather/scatter loops. */
  e = run_filter_pull(&pb, in, out, W, H, 3, 8, plugInModeRGBColor, 7, 5, 11, 3);
  check(e == noErr, "pull-model run failed");
  for(i = 0; i < W * H * 3; i++) if(out[i] != base[i]) mism++;
  printf("  mismatching bytes vs two-buffer host: %d\n", mism);
  check(mism == 0, "pull-model output differs from the two-buffer path");

  {
    enum { W2 = 23, H2 = 17 };
    unsigned short in2[W2 * H2 * 3], base2[W2 * H2 * 3], out2[W2 * H2 * 3];
    int j, mism2 = 0;
    for(j = 0; j < W2 * H2 * 3; j++) in2[j] = (unsigned short)((j * 811) % 32769);
    e = run_filter(&pb, in2, base2, W2, H2, 3, 16, plugInModeRGB48);
    check(e == noErr, "baseline 16-bit run failed");
    e = run_filter_pull(&pb, in2, out2, W2, H2, 3, 16, plugInModeRGB48, 6, 4, 9, 8);
    check(e == noErr, "16-bit pull-model run failed");
    for(j = 0; j < W2 * H2 * 3; j++) if(out2[j] != base2[j]) mism2++;
    printf("  mismatching 16-bit samples: %d\n", mism2);
    check(mism2 == 0, "16-bit pull-model output differs from the two-buffer path");
  }
}

/* -------------------------------------------- DIR coupler controls (OFX) -- */
/* The amount/inhibition knobs are engine-applied (the spatial diffusion is the
 * caller's blur, tested above). These checks pin the params block <-> chain
 * projection and the validation ranges. */
static void test_coupler_controls(void)
{
  sf_params_block b;
  sp_chain_params_t cp;

  printf("[coupler] amount/inhibition params round trip\n");
  sf_params_init(&b);
  check(sf_params_validate(&b) == 0, "default block failed validation");
  check(b.coupler_amount == 1.0 && b.coupler_inhibition_samelayer == 1.0 &&
            b.coupler_inhibition_interlayer == 1.0,
        "default coupler amount/inhibition is not 1.0");

  sf_params_to_chain(&b, &cp);
  check(cp.coupler_amount == 1.0 &&
            cp.coupler_inhibition_samelayer == 1.0 &&
            cp.coupler_inhibition_interlayer == 1.0,
        "defaults not projected onto sp_chain_params_t");

  b.coupler_amount = 0.0; /* 0 is a legal value (effect off, not couplers off) */
  b.coupler_inhibition_samelayer = 0.0;
  b.coupler_inhibition_interlayer = 2.0;
  check(sf_params_validate(&b) == 0, "in-range coupler controls rejected");
  sf_params_to_chain(&b, &cp);
  check(cp.coupler_amount == 0.0 && cp.coupler_inhibition_samelayer == 0.0 &&
            cp.coupler_inhibition_interlayer == 2.0,
        "coupler controls not projected");

  b.coupler_amount = 1.0;
  b.coupler_inhibition_samelayer = -0.1;
  check(sf_params_validate(&b) != 0, "negative inhibition accepted");
}

/* ------------------------------------- locked v1 scope extras (OFX) ------ */
/* DIR chroma gammas, print filtration/preflash, halation colour weights and
 * the scanner black/white point: pin the block <-> chain projection and the
 * defaults (every entry must be a no-op at its initial value). */
static void test_scope_extras(void)
{
  sf_params_block b;
  sp_chain_params_t cp;
  int i;

  printf("[scope] locked v1 extras params round trip\n");
  sf_params_init(&b);
  check(sf_params_validate(&b) == 0, "default block failed validation");
  for(i = 0; i < 3; i++)
    check(b.dir_gamma_same[i] == 0.0 && b.halation_strength[i] == -1.0,
          "DIR chroma / halation defaults are not neutral");
  check(b.filter_m_shift == 0.0 && b.filter_y_shift == 0.0 &&
            b.preflash_exposure == 0.0 && b.preflash_m_shift == 0.0 &&
            b.preflash_y_shift == 0.0 && b.filter_c_shift == 0.0 &&
            b.print_push_pull_stops == 0.0 && b.print_shadow_shape == 0.0 &&
            b.print_highlight_shape == 0.0,
        "filtration / preflash / print chemistry defaults are not neutral");
  check(b.scan_black_correction == 1 && b.scan_white_correction == 1 &&
            b.scan_black_level == 0.01 && b.scan_white_level == 0.98,
        "scanner black/white defaults do not match the engine");
  check(b.film_push_pull_stops == 0.0 && b.film_gamma == 1.0 &&
            b.print_gamma == 1.0,
        "film/print chemistry defaults are not neutral");

  sf_params_to_chain(&b, &cp);
  check(cp.halation_strength[0] == -1.0 && cp.scan_black_correction == 1 &&
            cp.scan_black_level == 0.01 && cp.scan_white_level == 0.98 &&
            cp.dir_gamma_same[0] == 0.0,
        "defaults not projected onto sp_chain_params_t");

  /* A representative non-default set must survive the projection. */
  b.dir_gamma_same[0] = 0.42;
  b.dir_gamma_r_gb[1] = 0.31;
  b.dir_gamma_b_rg[0] = 0.19;
  b.filter_m_shift = 12.0;
  b.filter_y_shift = -5.0;
  b.filter_c_shift = 30.0;
  b.print_push_pull_stops = 1.0;
  b.print_shadow_shape = -0.5;
  b.print_highlight_shape = 0.4;
  b.preflash_exposure = 0.25;
  b.preflash_m_shift = 3.0;
  b.preflash_y_shift = -2.0;
  b.halation_strength[0] = 0.07;
  b.halation_strength[1] = 0.02;
  b.halation_strength[2] = 0.0;
  b.scan_black_correction = 0;
  b.scan_white_correction = 1;
  b.scan_black_level = 0.02;
  b.scan_white_level = 0.95;
  b.film_push_pull_stops = 1.5;
  b.film_gamma = 1.2;
  b.print_gamma = 0.8;
  check(sf_params_validate(&b) == 0, "in-range scope extras rejected");
  sf_params_to_chain(&b, &cp);
  check(cp.dir_gamma_same[0] == 0.42 && cp.dir_gamma_r_gb[1] == 0.31 &&
            cp.dir_gamma_b_rg[0] == 0.19 && cp.filter_m_shift == 12.0 &&
            cp.filter_y_shift == -5.0 && cp.filter_c_shift == 30.0 &&
            cp.print_push_pull_stops == 1.0 && cp.print_shadow_shape == -0.5 &&
            cp.print_highlight_shape == 0.4 && cp.preflash_exposure == 0.25 &&
            cp.preflash_m_shift == 3.0 && cp.preflash_y_shift == -2.0 &&
            cp.halation_strength[0] == 0.07 && cp.halation_strength[1] == 0.02 &&
            cp.halation_strength[2] == 0.0 && cp.scan_black_correction == 0 &&
            cp.scan_white_level == 0.95 && cp.film_push_pull_stops == 1.5 &&
            cp.film_gamma == 1.2 && cp.print_gamma == 0.8,
        "scope extras not projected");

  b.scan_black_correction = 2; /* flags are 0/1 only */
  check(sf_params_validate(&b) != 0, "bad correction flag accepted");

  /* Film/print chemistry bounds. */
  b.scan_black_correction = 1;
  b.film_push_pull_stops = 9.0;
  check(sf_params_validate(&b) != 0, "out-of-range push/pull accepted");
  b.film_push_pull_stops = 0.0;
  b.film_gamma = 0.1;
  check(sf_params_validate(&b) != 0, "out-of-range film gamma accepted");
  b.film_gamma = 1.0;
  b.print_gamma = 9.0;
  check(sf_params_validate(&b) != 0, "out-of-range print gamma accepted");
}

/* Fraction of preview output pixels with any channel non-zero. "Any single
 * non-zero byte" is not a sufficient liveness check: a blit that lands in the
 * wrong place can leave the frame almost entirely black yet still carry some
 * light (the 1:1 blit once collapsed every row onto a single column, 0.5% of
 * the frame). The test sources are bright gradients, so a correct render must
 * put light in nearly every pixel. */
static double preview_nonblack_frac(const unsigned char *rgb, int w, int h)
{
  size_t n = (size_t)w * (size_t)h, i, nz = 0;
  if(!rgb) return 0.0;
  for(i = 0; i < n; i++)
    if(rgb[i * 3] || rgb[i * 3 + 1] || rgb[i * 3 + 2]) nz++;
  return (double)nz / (double)n;
}

/* The real-image preview: a true 1:1 crop of an arbitrary frame, pushed
 * through the same chain as the render and blitted without resampling. Checks
 * that it renders, that it is deterministic, and that the caller's frame is
 * left untouched (the crop must be copied, not rendered in place). */
static void test_preview_image(void)
{
  const int W = 1300, H = 1100; /* > the pane size, so the crop path runs */
  const int OW = 206, OH = 190;
  const size_t n3 = (size_t)W * H * 3;
  float *src = (float *)malloc(n3 * sizeof(float));
  float *cpy = (float *)malloc(n3 * sizeof(float));
  unsigned char *a, *b;
  char *err = NULL;
  sf_params_block pb;
  size_t i;

  printf("[preview] real-image 1:1 centre crop\n");
  if(!src || !cpy) { free(src); free(cpy); check(0, "out of memory"); return; }

  for(i = 0; i < n3; i += 3)
  {
    const int x = (int)((i / 3) % (size_t)W);
    const int y = (int)((i / 3) / (size_t)W);
    src[i + 0] = (float)(x % 256) / 255.0f;
    src[i + 1] = (float)(y % 256) / 255.0f;
    src[i + 2] = (float)((x + y) % 256) / 255.0f;
  }
  memcpy(cpy, src, n3 * sizeof(float));

  sf_params_init(&pb);
  a = sf_preview_render_image(src, W, H, &pb, OW, OH, &err);
  check(a != NULL, "sf_preview_render_image returned NULL");
  if(err) { free(err); err = NULL; }

  b = sf_preview_render_image(src, W, H, &pb, OW, OH, &err);
  check(b != NULL, "second render returned NULL");
  if(a && b)
    check(memcmp(a, b, (size_t)OW * (size_t)OH * 3u) == 0,
          "preview not deterministic");
  if(err) { free(err); err = NULL; }

  if(a)
  {
    /* The 1:1 crop fills the whole pane (no letterbox), so a correct render
     * is ~100% lit; 0.90 fails the "one lit column" bug decisively. */
    double frac = preview_nonblack_frac(a, OW, OH);
    check(frac > 0.90, "preview mostly black");
    if(frac <= 0.90) printf("  nonblack=%.3f%%\n", frac * 100.0);
  }

  check(memcmp(src, cpy, n3 * sizeof(float)) == 0,
        "preview modified the caller's frame");

  check(sf_preview_render_image(NULL, W, H, &pb, 206, 190, NULL) == NULL,
        "NULL source accepted");
  check(sf_preview_render_image(src, W, H, &pb, 0, 190, NULL) == NULL,
        "zero-width target accepted");

  free(a); free(b);
  free(src); free(cpy);
}

/* The "fit" preview: the whole frame box-downsampled and rendered with a
 * proportionally scaled pixel pitch. Checks rendering, determinism, that the
 * caller's frame is untouched, and the argument guards. Also runs a portrait
 * and a tiny source through it, since the letterbox/aspect math must not depend
 * on the frame being landscape and larger than the target. */
static void test_preview_fit(void)
{
  const int W = 1300, H = 1100;
  const int OW = 206, OH = 190;
  const size_t n3 = (size_t)W * H * 3;
  float *src = (float *)malloc(n3 * sizeof(float));
  float *cpy = (float *)malloc(n3 * sizeof(float));
  unsigned char *a, *b;
  char *err = NULL;
  sf_params_block pb;
  size_t i;

  printf("[preview] whole-image fit\n");
  if(!src || !cpy) { free(src); free(cpy); check(0, "out of memory"); return; }

  for(i = 0; i < n3; i += 3)
  {
    const int x = (int)((i / 3) % (size_t)W);
    const int y = (int)((i / 3) / (size_t)W);
    src[i + 0] = (float)(x % 256) / 255.0f;
    src[i + 1] = (float)(y % 256) / 255.0f;
    src[i + 2] = (float)((x + y) % 256) / 255.0f;
  }
  memcpy(cpy, src, n3 * sizeof(float));

  sf_params_init(&pb);
  a = sf_preview_render_fit(src, W, H, &pb, OW, OH, &err);
  check(a != NULL, "sf_preview_render_fit returned NULL");
  if(err) { free(err); err = NULL; }

  b = sf_preview_render_fit(src, W, H, &pb, OW, OH, &err);
  check(b != NULL, "sf_preview_render_fit not repeatable");
  if(err) { free(err); err = NULL; }

  if(a && b)
  {
    double frac;
    check(memcmp(a, b, (size_t)OW * OH * 3u) == 0, "fit preview not deterministic");
    frac = preview_nonblack_frac(a, OW, OH);
    /* The fit view letterboxes (source aspect != pane), so its ceiling is
     * ~92% here; 0.85 keeps margin while still failing the "one lit column"
     * class of bug (0.5%). */
    check(frac > 0.85, "fit preview mostly black");
    if(frac <= 0.85) printf("  nonblack=%.3f%%\n", frac * 100.0);
  }
  check(memcmp(cpy, src, n3 * sizeof(float)) == 0,
        "fit preview modified the caller's frame");

  /* A tiny source (smaller than the control on both axes) must still render. */
  {
    float tiny[4 * 3];
    unsigned char *t;
    for(i = 0; i < 4 * 3; i++) tiny[i] = (float)(i % 5) / 4.0f;
    t = sf_preview_render_fit(tiny, 4, 1, &pb, OW, OH, &err);
    check(t != NULL, "fit preview failed on a sub-control source");
    if(err) { free(err); err = NULL; }
    free(t);
  }

  check(sf_preview_render_fit(NULL, W, H, &pb, OW, OH, NULL) == NULL,
        "fit preview accepted a NULL source");
  check(sf_preview_render_fit(src, W, H, &pb, 0, OH, NULL) == NULL,
        "fit preview accepted a zero-width target");

  free(a); free(b);
  free(src); free(cpy);
}

/* The preview's display transform (sf_preview_display): the chain writes
 * space-preserving samples, but the preview control is a plain bitmap that
 * nothing colour-manages, so it is converted to sRGB before quantising. Checks
 * that the sRGB default is a byte no-op and that a neutral survives a
 * white-point change (a correct chromatic adaptation maps white to white, so an
 * achromatic colour must stay achromatic). */
static void test_preview_display(void)
{
  sf_params_block pb;
  float px[3];
  int i, prophoto = -1;

  printf("[preview] display transform (document -> sRGB)\n");

  for(i = 0; i < sp_color_space_count(); i++)
    if(strcmp(sp_color_space_label(i), "ProPhoto RGB") == 0) { prophoto = i; break; }
  check(prophoto >= 0, "ProPhoto RGB missing from the colour-space table");

  /* sRGB document: the transform must not touch the samples. */
  sf_params_init(&pb);
  {
    const float r = 0.13f, g = 0.51f, b = 0.87f;
    px[0] = r; px[1] = g; px[2] = b;
    sf_preview_display(px, 1, &pb);
    check(px[0] == r && px[1] == g && px[2] == b,
          "display transform was not a no-op for the sRGB default");
  }

  /* ProPhoto (D50) neutral: the D50->D65 adaptation must keep it neutral. */
  pb.input_space = prophoto;
  pb.input_transfer = SP_TRANSFER_PROPHOTO;
  pb.input_custom_xyz_valid = 0;
  px[0] = px[1] = px[2] = 0.5f;
  sf_preview_display(px, 1, &pb);
  check(fabsf(px[0] - px[1]) < 2e-3f && fabsf(px[1] - px[2]) < 2e-3f,
        "ProPhoto neutral did not stay neutral through the display transform");
  check(px[0] > 0.05f && px[0] < 0.95f,
        "display transform produced an implausible level");

  /* A neutral under a custom (detected) matrix must also stay neutral. */
  sf_params_init(&pb);
  pb.input_custom_xyz_valid = 1;
  sp_color_space_matrix(prophoto, pb.input_custom_rgb_to_xyz,
                        pb.input_custom_white_xy);
  pb.input_transfer = SP_TRANSFER_PROPHOTO;
  px[0] = px[1] = px[2] = 0.4f;
  sf_preview_display(px, 1, &pb);
  check(fabsf(px[0] - px[1]) < 2e-3f && fabsf(px[1] - px[2]) < 2e-3f,
        "custom-matrix neutral did not stay neutral through the display transform");
}

/* Exercise filter_loop's dialog hook (sf_show_dialog): it must run exactly once
 * for interactive invocations, apply the edited settings to the render, stay
 * out of the way when not interactive, and propagate a cancel. */
static int g_hook_calls, g_hook_cancel;
static double g_hook_exposure;

static int test_dialog_hook(void *ctx, FilterRecordPtr fr, sf_params_block *b,
                            const float *src, int w, int h)
{
  (void)ctx; (void)fr; (void)src; (void)w; (void)h;
  g_hook_calls++;
  b->exposure_ev = g_hook_exposure;
  return g_hook_cancel ? 0 : 1;
}

static void test_dialog_hook_path(void)
{
  const int W = 48, H = 40;
  const size_t n = (size_t)W * H * 3;
  unsigned char *in = (unsigned char *)malloc(n);
  unsigned char *out = (unsigned char *)malloc(n);
  unsigned char *ref = (unsigned char *)malloc(n);
  unsigned char *base = (unsigned char *)malloc(n);
  sf_params_block pb, pb2;
  OSErr err;
  size_t i;

  printf("[dialog] hook gating, edits and cancel\n");
  if(!in || !out || !ref || !base)
  {
    free(in); free(out); free(ref); free(base);
    check(0, "out of memory");
    return;
  }
  for(i = 0; i < n; i++) in[i] = (unsigned char)((i * 7) & 0xff);

  sf_params_init(&pb);
  sf_filter_set_dialog_hook(test_dialog_hook, NULL);

  /* interactive: hook runs once and its edits reach the render */
  g_hook_calls = 0; g_hook_exposure = 1.5; g_hook_cancel = 0;
  sf_filter_mark_interactive();
  err = run_filter(&pb, in, out, W, H, 3, 8, plugInModeRGBColor);
  check(err == noErr, "interactive render failed");
  check(g_hook_calls == 1, "hook not called exactly once");

  pb2 = pb;
  pb2.exposure_ev = 1.5;
  err = run_filter(&pb2, in, ref, W, H, 3, 8, plugInModeRGBColor);
  check(err == noErr, "reference render failed");
  check(memcmp(out, ref, n) == 0, "hook edits did not reach the render");

  /* not interactive (Last Filter): hook stays out, output is unchanged */
  g_hook_calls = 0;
  err = run_filter(&pb, in, out, W, H, 3, 8, plugInModeRGBColor);
  check(err == noErr, "non-interactive render failed");
  check(g_hook_calls == 0, "hook ran for a non-interactive invocation");
  err = run_filter(&pb, in, base, W, H, 3, 8, plugInModeRGBColor);
  check(memcmp(out, base, n) == 0, "non-interactive output not stable");

  /* cancel: userCanceledErr comes back out */
  g_hook_calls = 0; g_hook_cancel = 1;
  sf_filter_mark_interactive();
  err = run_filter(&pb, in, out, W, H, 3, 8, plugInModeRGBColor);
  check(err == userCanceledErr, "cancel not propagated");
  check(g_hook_calls == 1, "hook not called on cancel");

  sf_filter_set_dialog_hook(NULL, NULL);
  free(in); free(out); free(ref); free(base);
}

/* --------------------------------------------------------------- presets -- */
/* Wide sample across every field type, every array and both pack stems. The
 * codec writes the whole block through its field table, so a bit-exact round
 * trip over a sample this broad is what catches a field that was added to
 * sf_params_block but forgotten in preset.c. */
static void preset_fill(sf_params_block *b)
{
  sf_params_init(b);

  b->film_index  = (SF_FILMS_COUNT > 1) ? 1 : 0;
  b->paper_index = (SF_PAPERS_COUNT > 1) ? 1 : 0;

  b->process_mode = 2;
  b->scan_film = 1;
  b->scan_negative_invert = 1;
  b->printer_lights_gang = 1;
  b->camera_ir_filter_enabled = 1;
  b->couplers_active = 1;
  b->lut_steps = 17;
  b->film_format = 3;
  b->rgb_to_raw_method = 1;
  b->input_space = 4;
  b->input_transfer = 3;
  b->auto_exposure = 1;
  b->auto_exposure_method = 1;
  b->diffusion_on = 1;
  b->halation_on = 1;
  b->grain_on = 1;
  b->grain_model = 1;
  b->grain_sublayers_enabled = 1;
  b->grain_sublayer_count = 4;
  b->grain_seed = 123456789;
  b->print_diffusion_on = 1;
  b->scan_black_correction = 1;

  b->exposure_ev = -0.65;
  b->print_exposure = 1.03125;
  b->coupler_diffusion_um = 4.75;
  b->coupler_amount = 1.375;
  b->coupler_inhibition_interlayer = 0.5;
  b->pixel_um = 6.5;
  b->halation_sigma_max_um = 42.0;
  b->film_push_pull_stops = 1.5;
  b->negative_leuco_cyan_coupling = 0.5;
  b->print_gamma = 1.125;
  b->print_shadow_shape = -0.25;
  b->scan_black_level = 0.015625;
  b->scan_usm_radius_um = 2.5;
  b->preflash_exposure = 0.03125;

  b->printer_lights_r = -3.5f;
  b->printer_lights_b = 2.25f;
  b->camera_uv_cut_nm = 405.0f;
  b->boost_ev = 0.25f;
  b->protect_ev = -0.75f;
  b->diffusion_warmth = 0.5f;
  b->halation_amount = 1.75f;
  b->grain_amount = 0.375f;
  b->grain_saturation = 0.125f;
  b->grain_particle_scale[0] = 1.25f;
  b->grain_particle_scale[2] = 0.75f;
  b->grain_particle_scale_layer[1] = 1.5f;
  b->grain_uniformity_rgb[1] = 1.5f;
  b->grain_density_min_rgb[2] = 0.125f;
  b->grain_final_blur_um = 0.5f;
  b->glare_percent = 12.5f;
  b->scan_glare_roughness = 0.375f;
  b->print_diffusion_core_size = 2.75f;

  b->dir_gamma_same[0] = 1.125;
  b->dir_gamma_same[2] = 0.875;
  b->dir_gamma_r_gb[1] = 2.5;
  b->dir_gamma_g_rb[0] = 0.0625;
  b->dir_gamma_b_rg[1] = 1.75;
  b->filter_m_shift = 0.03125;
  b->preflash_y_shift = -0.0625;
  b->halation_strength[1] = 0.625;
  b->halation_first_sigma_um[2] = 18.5;
}

static void test_presets(void)
{
  sf_params_block a, b;
  char json[SF_PRESET_JSON_MAX];
  int n, n2;

  /* 1. A block survives an encode/decode cycle bit-exactly, so loading a
   *    preset never nudges a value and the render stays byte-identical to
   *    what the dialog showed. */
  preset_fill(&a);
  n = sf_preset_write(&a, "round trip", json, sizeof json);
  check(n > 0 && (size_t)n < sizeof json, "preset encode failed");
  if(n <= 0) return;

  sf_params_init(&b);
  check(sf_preset_read(json, n, &b) == 0, "preset decode failed");
  check(memcmp(&a, &b, sizeof a) == 0, "preset round trip is not bit-exact");

  /* 2. Re-encoding what was just decoded reproduces the same text, so saving
   *    a loaded preset is a no-op. */
  {
    char again[SF_PRESET_JSON_MAX];
    n2 = sf_preset_write(&b, "round trip", again, sizeof again);
    check(n2 == n && memcmp(json, again, (size_t)n) == 0,
          "preset re-encode is not stable");
  }

  /* 3. Decimals are always '.'-separated, and film/paper are pack stems
   *    rather than indices so the preset survives the pack being reordered. */
  check(strstr(json, "\"exposure_ev\": -0.65") != NULL,
        "preset did not write a '.'-separated decimal");
  check(strstr(json, SF_FILMS[a.film_index]) != NULL,
        "film stem missing from the preset");
  check(strstr(json, SF_PAPERS[a.paper_index]) != NULL,
        "paper stem missing from the preset");

  /* 4. A partial preset overrides only what it names; everything else takes
   *    the engine default rather than whatever was in the block before. */
  {
    sf_params_block dflt, got;
    char part[512];
    int alt;

    sf_params_init(&dflt);
    alt = 0;
    if(alt == dflt.film_index) alt = SF_FILMS_COUNT - 1;
    snprintf(part, sizeof part,
             "{\"format\":\"%s\",\"version\":1,\"params\":"
             "{\"film\":\"%s\",\"exposure_ev\":0.25}}",
             SF_PRESET_FORMAT, SF_FILMS[alt]);

    sf_params_init(&got);
    check(sf_preset_read(part, (int)strlen(part), &got) == 0,
          "partial preset rejected");
    check(got.film_index == alt, "partial preset did not take its film");
    check(got.exposure_ev == 0.25, "partial preset did not take its exposure");
    check(got.paper_index == dflt.paper_index, "partial preset clobbered the paper");
    check(got.grain_amount == dflt.grain_amount, "partial preset clobbered grain");
    check(got.pixel_um == dflt.pixel_um, "partial preset clobbered the pixel size");
    check(sf_params_validate(&got) == 0, "decoded preset does not validate");
  }

  /* 5. A film that has since left the pack keeps the default instead of
   *    producing an out-of-range index. */
  {
    sf_params_block dflt, got;
    char txt[512];
    snprintf(txt, sizeof txt,
             "{\"format\":\"%s\",\"version\":1,\"params\":{\"film\":\"no_such_film\"}}",
             SF_PRESET_FORMAT);
    sf_params_init(&dflt);
    sf_params_init(&got);
    check(sf_preset_read(txt, (int)strlen(txt), &got) == 0,
          "unknown film stem rejected");
    check(got.film_index == dflt.film_index, "unknown film changed the index");
  }

  /* 6. Foreign or malformed input is refused and leaves the block untouched. */
  {
    static const char *const bad[] =
    {
      "",
      "not json at all",
      "[1,2,3]",
      "{\"format\":\"something-else\",\"version\":1,\"params\":{}}",
      "{\"version\":1,\"params\":{}}",
      "{\"format\":\"spektrafilm-preset\",\"params\":{}}",
      "{\"format\":\"spektrafilm-preset\",\"version\":99,\"params\":{}}",
      "{\"format\":\"spektrafilm-preset\",\"version\":0,\"params\":{}}",
      "{\"format\":\"spektrafilm-preset\",\"version\":1}",
      "{\"format\":\"spektrafilm-preset\",\"version\":1,\"params\":",
      "{\"format\":\"spektrafilm-preset\",\"version\":1,\"params\":{\"exposure_ev\":}}",
      "{\"format\":\"spektrafilm-preset\",\"version\":1,\"params\":{\"exposure_ev\""
    };
    size_t i;
    for(i = 0; i < sizeof bad / sizeof bad[0]; i++)
    {
      sf_params_block before, got;
      sf_params_init(&before);
      preset_fill(&before);
      got = before;
      check(sf_preset_read(bad[i], (int)strlen(bad[i]), &got) != 0,
            "malformed preset accepted");
      check(memcmp(&before, &got, sizeof before) == 0,
            "rejected preset still wrote to the block");
    }
  }

  /* 7. A value that cannot be represented keeps the default rather than
   *    inventing one, so a hand-edited preset cannot corrupt the block. */
  {
    sf_params_block dflt, got;
    char txt[512];
    snprintf(txt, sizeof txt,
             "{\"format\":\"%s\",\"version\":1,\"params\":{"
             "\"exposure_ev\":true,\"grain_amount\":1e400,\"lut_steps\":7.5,"
             "\"printer_lights_r\":[1,2,3],\"pixel_um\":\"wide\","
             "\"film\":42,\"paper\":{}}}",
             SF_PRESET_FORMAT);
    sf_params_init(&dflt);
    sf_params_init(&got);
    check(sf_preset_read(txt, (int)strlen(txt), &got) == 0, "tolerant read failed");
    check(got.exposure_ev == dflt.exposure_ev, "non-numeric exposure was taken");
    check(got.grain_amount == dflt.grain_amount, "overflowing grain was taken");
    check(got.lut_steps == dflt.lut_steps, "fractional lut_steps was taken");
    check(got.printer_lights_r == dflt.printer_lights_r, "array-for-scalar was taken");
    check(got.pixel_um == dflt.pixel_um, "string-for-number was taken");
    check(got.film_index == dflt.film_index, "number-for-stem was taken");
    check(got.paper_index == dflt.paper_index, "object-for-stem was taken");
  }

  /* 8. A file cut short is refused rather than half-applied. */
  {
    sf_params_block got;
    sf_params_init(&got);
    check(sf_preset_read(json, n / 2, &got) != 0, "truncated preset accepted");
  }

  /* 9. Unknown keys are ignored, so a preset written by a later build (more
   *    fields, extra metadata) still loads here. */
  {
    sf_params_block dflt, got;
    char txt[512];
    snprintf(txt, sizeof txt,
             "{\"format\":\"%s\",\"version\":1,\"future\":\"meta\","
             "\"params\":{\"exposure_ev\":0.75,\"future_knob\":[1,{\"x\":2}]}}",
             SF_PRESET_FORMAT);
    sf_params_init(&dflt);
    sf_params_init(&got);
    check(sf_preset_read(txt, (int)strlen(txt), &got) == 0,
          "preset with unknown keys rejected");
    check(got.exposure_ev == 0.75, "known key lost beside unknown ones");
    check(got.pixel_um == dflt.pixel_um, "unknown key changed a known field");
  }

  /* 10. Locale independence. A host app is free to have called setlocale();
   *     the output must not change and the file must still read back exactly.
   *     Skipped when the locale is not installed on this machine. */
  {
    static const char *const locs[] =
    { "de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8", "de_DE", "fr_FR" };
    char c_out[SF_PRESET_JSON_MAX];
    const int c_len = sf_preset_write(&a, "locale", c_out, sizeof c_out);
    size_t i;

    for(i = 0; i < sizeof locs / sizeof locs[0]; i++)
    {
      char l_out[SF_PRESET_JSON_MAX];
      int l_len;
      sf_params_block back;

      if(!setlocale(LC_ALL, locs[i])) continue;

      l_len = sf_preset_write(&a, "locale", l_out, sizeof l_out);
      check(l_len == c_len && memcmp(l_out, c_out, (size_t)c_len) == 0,
            "preset output changed under a non-C locale");

      sf_params_init(&back);
      check(sf_preset_read(c_out, c_len, &back) == 0,
            "preset unreadable under a non-C locale");
      check(memcmp(&a, &back, sizeof a) == 0,
            "preset misread under a non-C locale");

      setlocale(LC_ALL, "C");
      break;
    }
  }
}

static int run_selftest(void)
{
  init_hp();
  test_coupler_diffuse();
  test_adapter_parity();
  test_16bit_domain();
  test_input_transfer();
  test_color_surface();
  test_film_format();
  test_phase2_stages();  test_progress_abort();
  test_chem_stages();
  test_grain_surface();
  test_halation_surface();
  test_iir_cascade();
  test_host_paths();
  test_pull_path();
  test_coupler_controls();
  test_scope_extras();
  test_preview_image();
  test_preview_fit();
  test_preview_display();
  test_dialog_hook_path();
  test_presets();
  printf("\nselftest: %s\n", g_fail ? "FAILED" : "passed");
  return g_fail ? 1 : 0;
}

/* --------------------------------------------------------------- render -- */
/* method < 0 keeps the block default (Hanatos2026); otherwise it is an
 * sf_rgb_to_raw_method_t value, so the RGB->raw spectral-upsampling methods can
 * be compared against tools/refrender fed the same sp_params JSON key. */
static int run_render_method(const char *in_path, const char *out_path, int parallel,
                             int method)
{
  int w = 0, h = 0;
  unsigned char *in = NULL, *out = NULL;
  sf_params_block pb;
  OSErr err;

  if(ppm_read(in_path, &w, &h, &in) != 0)
  {
    fprintf(stderr, "harness: bad PPM %s\n", in_path);
    return 1;
  }
  out = (unsigned char *)malloc((size_t)w * h * 3);
  sf_params_init(&pb);
  if(method >= 0) pb.rgb_to_raw_method = method;
  if(parallel)
  {
    sf_parallel_install();
    printf("harness: threaded path, %d workers\n", sf_parallel_workers());
  }
  err = run_filter(&pb, in, out, w, h, 3, 8, plugInModeRGBColor);
  if(err != noErr)
  {
    fprintf(stderr, "harness: filter failed (%d): %s\n", (int)err, sf_filter_last_error());
    return 1;
  }
  if(ppm_write(out_path, w, h, out) != 0)
  {
    fprintf(stderr, "harness: cannot write %s\n", out_path);
    return 1;
  }
  printf("harness: %s -> %s (%dx%d, %s)\n", in_path, out_path, w, h,
         method < 0 ? "defaults" : "rgb_to_raw_method");
  free(in);
  free(out);
  return 0;
}

static int run_render(const char *in_path, const char *out_path, int parallel)
{
  return run_render_method(in_path, out_path, parallel, -1);
}

/* Render the dialog's built-in proxy with defaults and write it, so the
 * sf_preview path is exercised on the desktop build too. */
static int run_preview(const char *out_path)
{
  sf_params_block pb;
  unsigned char *px;
  char *err = NULL;

  sf_params_init(&pb);
  px = sf_preview_render(&pb, 220, 76, &err);
  if(!px)
  {
    fprintf(stderr, "harness: preview failed: %s\n", err ? err : "(no message)");
    free(err);
    return 1;
  }
  free(err);
  if(ppm_write(out_path, 220, 76, px) != 0)
  {
    fprintf(stderr, "harness: cannot write %s\n", out_path);
    free(px);
    return 1;
  }
  printf("harness: preview -> %s (220x76)\n", out_path);
  free(px);
  return 0;
}

/* Render the real-image preview (a 1:1 centre crop) for a PPM, so the Windows
 * path's sizing/scaling is exercised on the desktop build too. */
static int run_preview_image(const char *in_path, const char *out_path)
{
  const int OW = 206, OH = 190;
  int w = 0, h = 0;
  unsigned char *in = NULL, *px;
  float *fl;
  sf_params_block pb;
  char *err = NULL;

  if(ppm_read(in_path, &w, &h, &in) != 0)
  {
    fprintf(stderr, "harness: bad PPM %s\n", in_path);
    return 1;
  }
  fl = (float *)malloc((size_t)w * h * 3 * sizeof(float));
  if(!fl || sf_pixels_to_float(in, w * 3, w, h, 3, 8, fl) != 0)
  {
    fprintf(stderr, "harness: cannot convert %s\n", in_path);
    free(in); free(fl);
    return 1;
  }
  free(in);

  sf_params_init(&pb);
  px = sf_preview_render_image(fl, w, h, &pb, OW, OH, &err);
  free(fl);
  if(!px)
  {
    fprintf(stderr, "harness: preview-image failed: %s\n",
            err ? err : "(no message)");
    free(err);
    return 1;
  }
  free(err);
  if(ppm_write(out_path, OW, OH, px) != 0)
  {
    fprintf(stderr, "harness: cannot write %s\n", out_path);
    free(px);
    return 1;
  }
  printf("harness: preview-image %s -> %s (%dx%d)\n", in_path, out_path, OW, OH);
  free(px);
  return 0;
}

/* Render the whole-image "fit" preview of a PPM, so its downscale/pixel-pitch
 * math is exercised on the desktop build too. */
static int run_preview_fit(const char *in_path, const char *out_path)
{
  const int OW = 206, OH = 190;
  int w = 0, h = 0;
  unsigned char *in = NULL, *px;
  float *fl;
  sf_params_block pb;
  char *err = NULL;

  if(ppm_read(in_path, &w, &h, &in) != 0)
  {
    fprintf(stderr, "harness: bad PPM %s\n", in_path);
    return 1;
  }
  fl = (float *)malloc((size_t)w * h * 3 * sizeof(float));
  if(!fl || sf_pixels_to_float(in, w * 3, w, h, 3, 8, fl) != 0)
  {
    fprintf(stderr, "harness: cannot convert %s\n", in_path);
    free(in); free(fl);
    return 1;
  }
  free(in);

  sf_params_init(&pb);
  px = sf_preview_render_fit(fl, w, h, &pb, OW, OH, &err);
  free(fl);
  if(!px)
  {
    fprintf(stderr, "harness: preview-fit failed: %s\n",
            err ? err : "(no message)");
    free(err);
    return 1;
  }
  free(err);
  if(ppm_write(out_path, OW, OH, px) != 0)
  {
    fprintf(stderr, "harness: cannot write %s\n", out_path);
    free(px);
    return 1;
  }
  printf("harness: preview-fit %s -> %s (%dx%d)\n", in_path, out_path, OW, OH);
  free(px);
  return 0;
}

int main(int argc, char **argv)
{
  init_hp();
  if(argc >= 2 && strcmp(argv[1], "selftest") == 0) return run_selftest();
  if(argc >= 4 && strcmp(argv[1], "render") == 0) return run_render(argv[2], argv[3], 0);
  if(argc >= 5 && strcmp(argv[1], "render-method") == 0)
    return run_render_method(argv[2], argv[3], 0, atoi(argv[4]));
  if(argc >= 4 && strcmp(argv[1], "render-parallel") == 0)
    return run_render(argv[2], argv[3], 1);
  if(argc >= 3 && strcmp(argv[1], "preview") == 0) return run_preview(argv[2]);
  if(argc >= 4 && strcmp(argv[1], "preview-image") == 0)
    return run_preview_image(argv[2], argv[3]);
  if(argc >= 4 && strcmp(argv[1], "preview-fit") == 0)
    return run_preview_fit(argv[2], argv[3]);
  fprintf(stderr,
          "usage: %s selftest | render <in.ppm> <out.ppm> | "
          "render-parallel <in.ppm> <out.ppm> | render-method <in.ppm> <out.ppm> <method> | "
          "preview <out.ppm> | "
          "preview-image <in.ppm> <out.ppm> | preview-fit <in.ppm> <out.ppm>\n",
          argv[0]);
  return 2;
}
