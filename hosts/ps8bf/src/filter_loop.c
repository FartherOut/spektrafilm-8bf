/* SPDX-License-Identifier: GPL-3.0-or-later */
/* filter_loop.c — implementation (see filter_loop.h).
 *
 * The vertical slice processes the entire selection as ONE tile. The engine's
 * DIR-coupler blur is a full-frame spatial pass, so tiling it without padding
 * would seam; requesting the whole rect keeps the result reference-identical.
 * ROI/tiled rendering with blur-radius padding is a later pass (ROADMAP §6),
 * and for that the host's bufferSpace64 is the budget to check.
 *
 * Alpha and any extra planes ride through untouched: we memcpy the tile in->
 * out and only rewrite the three colour planes.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filter_loop.h"
#include "params.h"
#include "icc_profile.h"
#include <math.h>
static float sf_measure_auto_exposure_ev(const float *src, int w, int h, int transfer, int method);
#include "pix_convert.h"
#include "sp_chain.h"
#include "sp_color.h"

typedef struct sf_loop_ctx
{
  int active;
  int done; /* the current invocation's tile has been rendered */
  sp_chain_t *chain;
} sf_loop_ctx;

static sf_loop_ctx g_ctx;
static char g_err[192];

/* Map the chain's stage-boundary progress to the host's progressProc and let
 * the host's abortProc cancel a long render. Runs between pipeline stages (see
 * sp_chain_set_progress); returns non-zero to abort. */
static int sf_progress_bridge(void *ctx, int done, int total)
{
  FilterRecordPtr fr = (FilterRecordPtr)ctx;
  if(!fr) return 0;
  if(fr->progressProc) fr->progressProc(done, total);
  if(fr->abortProc && fr->abortProc()) return 1;
  return 0;
}

/* Render through the shared chain with the host's progress/abort wired in.
 * Returns 0 on success, the chain's code otherwise (3 = host aborted). */
static int sf_rf_render(FilterRecordPtr fr, float *fl, int w, int h)
{
  int rc;
  sp_chain_set_progress(g_ctx.chain, sf_progress_bridge, fr);
  rc = sp_chain_render(g_ctx.chain, fl, fl, w, h, 3);
  sp_chain_set_progress(g_ctx.chain, NULL, NULL);
  return rc;
}

/* Optional parameter dialog (see filter_loop.h). The Linux harness leaves it
 * NULL, so the selector bodies render straight through and stay byte-identical
 * to the refrender oracle. */
static sf_dialog_hook g_dialog;
static void *g_dialog_ctx;
static int g_dialog_shown;
/* Set when the host sent filterSelectorParameters (the interactive entry point).
 * Deliberately NOT cleared by sf_filter_reset: Start calls that reset, and we
 * still need the flag when Continue arrives. Consumed by sf_show_dialog. */
static int g_dialog_wanted;

void sf_filter_set_dialog_hook(sf_dialog_hook fn, void *ctx)
{
  g_dialog = fn;
  g_dialog_ctx = ctx;
}

void sf_filter_mark_interactive(void) { g_dialog_wanted = 1; }

const char *sf_filter_last_error(void) { return g_err; }

static void sf_set_err(const char *msg)
{
  size_t n;
  if(!msg) msg = "";
  n = strlen(msg);
  if(n >= sizeof g_err) n = sizeof g_err - 1;
  memcpy(g_err, msg, n);
  g_err[n] = 0;
}

/* ------------------------------------------------------------ diagnostics -- */
/* A short transcript of the host handshake, written to a temp file. Used to
 * pin down what a given host passes in FilterRecord when it refuses to hand
 * over pixel buffers (the Affinity Photo path, whose buffer behaviour could
 * not be reproduced locally). plugin_main installs the path; the Linux mock
 * harness leaves it unset, so nothing is written there. */
static char g_log_path[260];
static FILE *g_log;

void sf_filter_set_log_path(const char *path)
{
  size_t n;
  if(!path) { g_log_path[0] = 0; return; }
  n = strlen(path);
  if(n >= sizeof g_log_path) n = sizeof g_log_path - 1;
  memcpy(g_log_path, path, n);
  g_log_path[n] = 0;
}

static void sf_log_close(void)
{
  if(g_log) { fclose(g_log); g_log = NULL; }
}

/* Append to the transcript, opening it on first use. The whole handshake of an
 * invocation is kept in one file; the file is not truncated so a user can send
 * back the tail of several attempts. */
static int sf_log_open(void)
{
  if(!g_log && g_log_path[0]) g_log = fopen(g_log_path, "a");
  return g_log != NULL;
}

void sf_filter_log_begin(void) { (void)sf_log_open(); }

void sf_filter_log_end(void) { sf_log_close(); }

static void sf_vlog(const char *fmt, va_list ap)
{
  if(!sf_log_open()) return;
  vfprintf(g_log, fmt, ap);
  fputc('\n', g_log);
  fflush(g_log);
}

static void sf_log(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  sf_vlog(fmt, ap);
  va_end(ap);
}

void sf_filter_log_write(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  sf_vlog(fmt, ap);
  va_end(ap);
}

/* ------------------------------------------------------- coordinate rects -- */
/* Modern hosts want the *Rect32 fields once the plug-in opts in; the legacy
 * int16 rects are the fallback for a host whose BigDocumentStruct is absent.
 * We write BOTH families and zero BOTH, so whichever one the host's loop
 * reads, it sees the same selection and then the same "finished" marker. */
static int sf_use32(FilterRecordPtr fr)
{
  return fr->bigDocumentData && fr->bigDocumentData->PluginUsing32BitCoordinates;
}

static void sf_get_in_rect(FilterRecordPtr fr, VRect *r)
{
  if(sf_use32(fr))
  {
    r->top    = fr->bigDocumentData->inRect32.top;
    r->left   = fr->bigDocumentData->inRect32.left;
    r->bottom = fr->bigDocumentData->inRect32.bottom;
    r->right  = fr->bigDocumentData->inRect32.right;
  }
  else
  {
    r->top    = fr->inRect.top;
    r->left   = fr->inRect.left;
    r->bottom = fr->inRect.bottom;
    r->right  = fr->inRect.right;
  }
}

static void sf_get_out_rect(FilterRecordPtr fr, VRect *r)
{
  if(sf_use32(fr))
  {
    r->top    = fr->bigDocumentData->outRect32.top;
    r->left   = fr->bigDocumentData->outRect32.left;
    r->bottom = fr->bigDocumentData->outRect32.bottom;
    r->right  = fr->bigDocumentData->outRect32.right;
  }
  else
  {
    r->top    = fr->outRect.top;
    r->left   = fr->outRect.left;
    r->bottom = fr->outRect.bottom;
    r->right  = fr->outRect.right;
  }
}

/* Always populate both families. The legacy int16 fields are clamped; the
 * slice is scoped to ordinary-size stills, where they are exact. */
static void sf_set_in_rect(FilterRecordPtr fr, const VRect *r)
{
  fr->inRect.top    = (int16)r->top;
  fr->inRect.left   = (int16)r->left;
  fr->inRect.bottom = (int16)r->bottom;
  fr->inRect.right  = (int16)r->right;
  if(fr->bigDocumentData)
  {
    fr->bigDocumentData->inRect32.top    = r->top;
    fr->bigDocumentData->inRect32.left   = r->left;
    fr->bigDocumentData->inRect32.bottom = r->bottom;
    fr->bigDocumentData->inRect32.right  = r->right;
  }
}

static void sf_set_out_rect(FilterRecordPtr fr, const VRect *r)
{
  fr->outRect.top    = (int16)r->top;
  fr->outRect.left   = (int16)r->left;
  fr->outRect.bottom = (int16)r->bottom;
  fr->outRect.right  = (int16)r->right;
  if(fr->bigDocumentData)
  {
    fr->bigDocumentData->outRect32.top    = r->top;
    fr->bigDocumentData->outRect32.left   = r->left;
    fr->bigDocumentData->outRect32.bottom = r->bottom;
    fr->bigDocumentData->outRect32.right  = r->right;
  }
}

/* The selection bounds: prefer the 32-bit copy when the host filled it, else
 * fall back to the legacy int16 rect. Shared with plugin_main's Prepare, which
 * must size the buffer request from whichever rect family the host uses. */
void sf_filter_selection_rect(FilterRecordPtr fr, VRect *r)
{
  if(fr->bigDocumentData)
  {
    const VRect *b = &fr->bigDocumentData->filterRect32;
    if(b->right > b->left && b->bottom > b->top) { *r = *b; return; }
  }
  r->top    = fr->filterRect.top;
  r->left   = fr->filterRect.left;
  r->bottom = fr->filterRect.bottom;
  r->right  = fr->filterRect.right;
}

/* The film-format stage needs the full image's long edge to turn a gauge in mm
 * into a film pixel size; this one-tile filter processes the whole selection at
 * once, so the selection rect is the image. */
static void sf_set_long_edge(FilterRecordPtr fr, sp_chain_params_t *cp)
{
  VRect sel;
  double w, h;
  sf_filter_selection_rect(fr, &sel);
  w = (double)(sel.right - sel.left);
  h = (double)(sel.bottom - sel.top);
  cp->image_long_edge_px = (w > h) ? w : h;
  if(!(cp->image_long_edge_px > 0.0)) cp->image_long_edge_px = 0.0;
}

/* Empty both request rects so the host stops calling Continue. */
static void sf_finish_tile(FilterRecordPtr fr)
{
  VRect empty = {0, 0, 0, 0};
  sf_set_in_rect(fr, &empty);
  sf_set_out_rect(fr, &empty);
  g_ctx.done = 1;
}

void sf_filter_reset(void)
{
  if(g_ctx.chain) sp_chain_destroy(g_ctx.chain);
  g_ctx.chain = NULL;
  g_ctx.active = 0;
  g_ctx.done = 0;
  g_dialog_shown = 0;
}

/* -------------------------------------------------------- params handle -- */
static int sf_fr_read_params(FilterRecordPtr fr, sf_params_block *out)
{
  HandleProcs *hp = fr->handleProcs;
  Handle h = fr->parameters;
  int ok = 0;

  sf_params_init(out); /* baseline first, so we always have something valid */
  if(!h || !hp || !hp->lockProc) return 0;

  /* Grow a stale, smaller block (from an older build) before reading it, so
   * the memcpy below stays inside the host's allocation. */
  if(hp->getSizeProc && hp->setSizeProc &&
     hp->getSizeProc(h) < (int32)sizeof(sf_params_block))
    hp->setSizeProc(h, (int32)sizeof(sf_params_block));

  {
    sf_params_block *b = (sf_params_block *)hp->lockProc(h, /*moveHigh*/ FALSE);
    if(b && sf_params_validate(b) == 0)
    {
      memcpy(out, b, sizeof *out);
      ok = 1;
    }
    if(hp->unlockProc) hp->unlockProc(h);
  }
  return ok;
}

/* Show the (optional) dialog once per invocation, now that the frame is in
 * hand. The host's Win32 side wires this to the modal parameter dialog and
 * writes any edits back into the parameters handle; the harness leaves it NULL
 * and renders straight through. If the dialog returns OK the settings may have
 * changed, so the chain is rebuilt before the render. */
static OSErr sf_show_dialog(FilterRecordPtr fr, const float *src, int w, int h)
{
  sf_params_block pb;
  sp_chain_params_t cp;
  char *err = NULL;

  if(g_dialog_shown || !g_dialog) return noErr;
  g_dialog_shown = 1;

  /* Only when the host invoked the filter interactively (it sent
   * filterSelectorParameters): a "Last Filter" replay skips Parameters and must
   * apply the stored settings without a dialog. */
  if(!g_dialog_wanted) return noErr;
  g_dialog_wanted = 0;

  sf_fr_read_params(fr, &pb);
  /* Meter into the dedicated auto-exposure correction (sp_chain.c applies it),
   * rather than folding it into exposure_ev: on OK the dialog copies this block
   * back to the host, so bumping exposure_ev would compound every invocation
   * (drift) and would also be persisted as if the user had set it. */
  pb.auto_exposure_ev = 0.0;
  if(pb.auto_exposure)
  {
    const float _ae = sf_measure_auto_exposure_ev(src, w, h, pb.input_transfer, pb.auto_exposure_method);
    pb.auto_exposure_ev = (double)_ae;
  }
  if(!g_dialog(g_dialog_ctx, fr, &pb, src, w, h))
    return userCanceledErr;

  if(g_ctx.chain) { sp_chain_destroy(g_ctx.chain); g_ctx.chain = NULL; }
  sf_params_to_chain(&pb, &cp);
  sf_set_long_edge(fr, &cp);
  g_ctx.chain = sp_chain_create(&cp, &err);
  if(!g_ctx.chain)
  {
    sf_set_err(err ? err : "spektrafilm: could not build the render chain.");
    free(err);
    return filterBadParameters;
  }
  free(err);
  return noErr;
}

/* ------------------------------------------------- pull-model pixel I/O -- */
/* Not every host pre-fills inData/outData before filterSelectorContinue.
 * Affinity Photo does not: it only fills the tile the plug-in asks for through
 * AdvanceStateProc. gmic-8bf, a known-good filter there, drives its entire
 * read/write through advanceState one plane at a time and never touches the
 * Continue buffers. If the host hands us neither buffer, fall back to that
 * model: gather the selection plane by plane, render the whole frame once (the
 * DIR-coupler blur seams if tiled internally), then scatter it back the same
 * way. Both directions run over the host's own tile size, which is what the
 * host will actually deliver. */
#define SF_FIXED_ONE 65536u /* Fixed 1.0 (16.16) */

static int sf_tile_dim(int16 v) { return v > 0 ? (int)v : 1024; }

static int sf_gather_input(FilterRecordPtr fr, const VRect *sel,
                           unsigned char *raw, int depth)
{
  const int bytes = (depth == 16) ? 2 : 1;
  const int tw = sf_tile_dim(fr->inTileWidth);
  const int th = sf_tile_dim(fr->inTileHeight);
  const int col_bytes = (int)(sel->right - sel->left) * 3 * bytes;
  VRect empty;
  int p;

  if(!fr->advanceState) return -1;

  empty.top = empty.left = empty.bottom = empty.right = 0;
  sf_set_out_rect(fr, &empty); /* ask for input only during the gather */

  /* One plane at a time, chunky: inColumnBytes == sample size, no scaling. */
  fr->inPlaneBytes = (int32)bytes;
  fr->inColumnBytes = (int32)bytes;
  fr->inputRate = SF_FIXED_ONE;

  for(p = 0; p < 3; p++)
  {
    int ty;
    fr->inLoPlane = fr->inHiPlane = (int16)p;
    for(ty = 0; ty < (int)(sel->bottom - sel->top); ty += th)
    {
      int y0 = sel->top + ty;
      int y1 = y0 + th; if(y1 > sel->bottom) y1 = sel->bottom;
      int tx;
      for(tx = 0; tx < (int)(sel->right - sel->left); tx += tw)
      {
        int x0 = sel->left + tx;
        int x1 = x0 + tw; if(x1 > sel->right) x1 = sel->right;
        int rw, rh, yy, xx;
        VRect r;
        r.top = y0; r.left = x0; r.bottom = y1; r.right = x1;
        sf_set_in_rect(fr, &r);
        if(fr->advanceState() != noErr) return -2;
        if(!fr->inData) return -3;
        rw = x1 - x0; rh = y1 - y0;
        for(yy = 0; yy < rh; yy++)
        {
          const unsigned char *srow =
              (const unsigned char *)fr->inData + (size_t)yy * (size_t)fr->inRowBytes;
          unsigned char *drow = raw + (size_t)(ty + yy) * (size_t)col_bytes
                                    + (size_t)tx * 3 * bytes + (size_t)p * bytes;
          for(xx = 0; xx < rw; xx++)
            memcpy(drow + (size_t)xx * 3 * bytes, srow + (size_t)xx * bytes,
                   (size_t)bytes);
        }
      }
    }
  }
  return 0;
}

static int sf_scatter_output(FilterRecordPtr fr, const VRect *sel,
                             const unsigned char *raw, int depth)
{
  const int bytes = (depth == 16) ? 2 : 1;
  const int tw = sf_tile_dim(fr->outTileWidth);
  const int th = sf_tile_dim(fr->outTileHeight);
  const int col_bytes = (int)(sel->right - sel->left) * 3 * bytes;
  VRect empty;
  int p;

  if(!fr->advanceState) return -1;

  empty.top = empty.left = empty.bottom = empty.right = 0;
  sf_set_in_rect(fr, &empty); /* ask for output only during the scatter */

  for(p = 0; p < 3; p++)
  {
    int ty;
    fr->outLoPlane = fr->outHiPlane = (int16)p;
    for(ty = 0; ty < (int)(sel->bottom - sel->top); ty += th)
    {
      int y0 = sel->top + ty;
      int y1 = y0 + th; if(y1 > sel->bottom) y1 = sel->bottom;
      int tx;
      for(tx = 0; tx < (int)(sel->right - sel->left); tx += tw)
      {
        int x0 = sel->left + tx;
        int x1 = x0 + tw; if(x1 > sel->right) x1 = sel->right;
        int rw, rh, yy, xx;
        VRect r;
        r.top = y0; r.left = x0; r.bottom = y1; r.right = x1;
        sf_set_out_rect(fr, &r);
        if(fr->advanceState() != noErr) return -2;
        if(!fr->outData) return -3;
        rw = x1 - x0; rh = y1 - y0;
        for(yy = 0; yy < rh; yy++)
        {
          unsigned char *drow =
              (unsigned char *)fr->outData + (size_t)yy * (size_t)fr->outRowBytes;
          const unsigned char *srow = raw + (size_t)(ty + yy) * (size_t)col_bytes
                                          + (size_t)tx * 3 * bytes + (size_t)p * bytes;
          for(xx = 0; xx < rw; xx++)
            memcpy(drow + (size_t)xx * bytes, srow + (size_t)xx * 3 * bytes,
                   (size_t)bytes);
        }
      }
    }
  }
  return 0;
}


static int _sf_cmp_double(const void *a, const void *b)
{
  const double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}

static float sf_measure_auto_exposure_ev(const float *src, int w, int h, int transfer, int method)
{
  int pw = w, ph = h;
  const int longEdge = (w > h ? w : h);
  if(longEdge > 256 && longEdge > 0)
  {
    const double scale = 256.0 / (double)longEdge;
    pw = (int)(w * scale); if(pw < 1) pw = 1;
    ph = (int)(h * scale); if(ph < 1) ph = 1;
  }
  const size_t count = (size_t)pw * ph;
  double *ys = (double *)malloc(count * sizeof(double));
  if(!ys) return 0.0f;
  for(int y = 0; y < ph; y++)
  for(int x = 0; x < pw; x++)
  {
    const int sx = (int)((long long)x * w / pw);
    const int sy = (int)((long long)y * h / ph);
    const float *p = src + ((size_t)sy * (size_t)w + (size_t)sx) * 3;
    float r = p[0], g = p[1], b = p[2];
    r = sp_transfer_decode(transfer, r);
    g = sp_transfer_decode(transfer, g);
    b = sp_transfer_decode(transfer, b);
    const float yv = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    ys[y * pw + x] = yv;
  }
  double meteredY = 0.0;
  if(method == 1)
  {
    qsort(ys, count, sizeof(double), _sf_cmp_double);
    if(count % 2u == 1u)
      meteredY = ys[count / 2u];
    else
      meteredY = 0.5 * (ys[count / 2u - 1u] + ys[count / 2u]);
  }
  if(method == 0)
  {
    double normX = (double)pw / (double)(pw > ph ? pw : ph);
    double normY = (double)ph / (double)(pw > ph ? pw : ph);
    double weightedSum = 0.0, weightSum = 0.0;
    size_t index = 0;
    for(int y = 0; y < ph; y++)
    {
      const double yf = ((double)y / (double)ph - 0.5) * normY;
      for(int x = 0; x < pw; x++, index++)
      {
        const double xf = ((double)x / (double)pw - 0.5) * normX;
        const double weight = exp(-(xf*xf + yf*yf) / (2.0 * 0.2*0.2));
        weightedSum += ys[index] * weight;
        weightSum += weight;
      }
    }
    meteredY = (weightSum > 1e-30 ? weightedSum / weightSum : 0.0);
  }
  free(ys);
  if(!(meteredY > 0.0)) return 0.0f;
  double exposure = meteredY / 0.184;
  if(!(exposure > 0.0) || !isfinite(exposure)) return 0.0f;
  double ev = -log2(exposure);
  if(ev < -8.0) ev=-8.0;
  if(ev > 8.0) ev=8.0;
  return (float)(isfinite(ev) ? ev : 0.0);
}

/* Drive a whole invocation through AdvanceStateProc when the host supplied no
 * pixel buffer on Continue (see the block comment above). */
static OSErr sf_pull_run(FilterRecordPtr fr)
{
  VRect sel;
  int w, h, depth, bytes, row_bytes, e;
  unsigned char *raw;
  float *fl;

  sf_filter_selection_rect(fr, &sel);
  w = (int)(sel.right - sel.left);
  h = (int)(sel.bottom - sel.top);
  if(w <= 0 || h <= 0) { sf_finish_tile(fr); return noErr; }

  depth = fr->depth;
  bytes = (depth == 16) ? 2 : 1;
  row_bytes = w * 3 * bytes;

  sf_log("PULL sel=(%d,%d,%d,%d) depth=%d inTile=%dx%d outTile=%dx%d",
         sel.top, sel.left, sel.bottom, sel.right, depth,
         (int)fr->inTileWidth, (int)fr->inTileHeight,
         (int)fr->outTileWidth, (int)fr->outTileHeight);

  raw = (unsigned char *)malloc((size_t)row_bytes * (size_t)h);
  fl = (float *)malloc((size_t)w * (size_t)h * 3 * sizeof(float));
  if(!raw || !fl)
  {
    free(raw); free(fl);
    sf_set_err("spektrafilm: out of memory.");
    return memFullErr;
  }

  e = sf_gather_input(fr, &sel, raw, depth);
  if(e != 0)
  {
    sf_log("PULL gather failed at %d", e);
    free(raw); free(fl);
    sf_set_err("spektrafilm: host refused to supply image data.");
    return filterBadParameters;
  }

  if(fr->progressProc) fr->progressProc(0, 100);

  if(sf_pixels_to_float(raw, row_bytes, w, h, 3, depth, fl) != 0)
  {
    free(raw); free(fl);
    sf_set_err("spektrafilm: unsupported channel depth.");
    return filterBadParameters;
  }

  /* Auto exposure: measure the (still encoded) frame before the GUI so the
     interactive and replay paths share one chain even when no dialog renders. */
  {
    sf_params_block pb = {0};
    sf_fr_read_params(fr, &pb);
    pb.auto_exposure_ev = 0.0; /* recomputed below (and each invocation) */
    if(pb.auto_exposure)
    {
      const float _ae = sf_measure_auto_exposure_ev(fl, w, h, pb.input_transfer, pb.auto_exposure_method);
      pb.auto_exposure_ev = (double)_ae;
      if(g_ctx.chain) { sp_chain_destroy(g_ctx.chain); g_ctx.chain = NULL; }
      sp_chain_params_t cp;
      char *err = NULL;
      sf_params_to_chain(&pb, &cp);
      sf_set_long_edge(fr, &cp);
      g_ctx.chain = sp_chain_create(&cp, &err);
      if(!g_ctx.chain)
      {
        sf_set_err(err ? err : "spektrafilm: could not build the render chain.");
        if(err) free(err);
        free(raw); free(fl);
        sf_finish_tile(fr);
        return filterBadParameters;
      }
      if(err) free(err);
    }
  }

  e = sf_show_dialog(fr, fl, w, h);
  if(e != noErr)
  {
    free(raw); free(fl);
    sf_finish_tile(fr);
    return e;
  }

  {
    const int rrc = sf_rf_render(fr, fl, w, h);
    if(rrc != 0)
    {
      free(raw); free(fl);
      sf_finish_tile(fr);
      if(rrc == 3) return userCanceledErr;
      sf_set_err("spektrafilm: render failed.");
      return filterBadParameters;
    }
  }

  sf_float_to_pixels(fl, raw, row_bytes, w, h, 3, depth, sel.top);

  e = sf_scatter_output(fr, &sel, raw, depth);
  free(raw); free(fl);
  if(e != 0)
  {
    sf_log("PULL scatter failed at %d", e);
    sf_set_err("spektrafilm: host refused the rendered image data.");
    return filterBadParameters;
  }

  if(fr->progressProc) fr->progressProc(100, 100);
  sf_finish_tile(fr);
  sf_log("DONE pull");
  return noErr;
}

/* --------------------------------------------------------------- start -- */
OSErr sf_filter_start(FilterRecordPtr fr)
{
  sf_params_block pb;
  sp_chain_params_t cp;
  int planes;
  char *err = NULL;

  sf_set_err("");
  sf_filter_reset();

  if(!fr) return filterBadParameters;

  sf_log_open();
  sf_log("START mode=%d depth=%d planes=%d bigdoc=%d adv=%d case=%d",
         fr->imageMode, fr->depth, fr->planes,
         fr->bigDocumentData ? 1 : 0, fr->advanceState ? 1 : 0, fr->filterCase);
  sf_log("  rect16=(%d,%d,%d,%d) rect32=(%d,%d,%d,%d)",
         fr->filterRect.top, fr->filterRect.left,
         fr->filterRect.bottom, fr->filterRect.right,
         fr->bigDocumentData ? fr->bigDocumentData->filterRect32.top : 0,
         fr->bigDocumentData ? fr->bigDocumentData->filterRect32.left : 0,
         fr->bigDocumentData ? fr->bigDocumentData->filterRect32.bottom : 0,
         fr->bigDocumentData ? fr->bigDocumentData->filterRect32.right : 0);
  sf_log("  entry in=(%d,%d,%d,%d) out=(%d,%d,%d,%d) inData=%p outData=%p inRB=%d outRB=%d",
         fr->inRect.top, fr->inRect.left, fr->inRect.bottom, fr->inRect.right,
         fr->outRect.top, fr->outRect.left, fr->outRect.bottom, fr->outRect.right,
         (const void *)fr->inData, fr->outData, fr->inRowBytes, fr->outRowBytes);

  /* Only RGB 8/16-bit is in scope for the slice. (16-bit is `depth`; the
   * imageMode for 8-bit RGB is 3, for RGB48 it is 11.) */
  if(!(fr->imageMode == plugInModeRGBColor ||
       fr->imageMode == plugInModeRGB48))
  {
    sf_set_err("spektrafilm: only RGB 8-bit and 16-bit images are supported.");
    return filterBadMode;
  }
  if(fr->depth != 8 && fr->depth != 16)
  {
    sf_set_err("spektrafilm: only 8- and 16-bit-per-channel images are supported.");
    return filterBadMode;
  }

  planes = fr->planes;
  if(planes < 3) planes = 3;

  sf_fr_read_params(fr, &pb);

  if(pb.input_profile_auto)
  {
    const void *pdata = NULL;
    int psize = 0;
    char **hh = NULL;
    if(fr->iCCWorkingProfileSize > 0 && fr->iCCWorkingProfileData)
    {
      hh = (char **)fr->iCCWorkingProfileData;
      pdata = (const void *)(hh ? *hh : NULL);
      psize = (int)fr->iCCWorkingProfileSize;
    }
    else if(fr->iCCprofileSize > 0 && fr->iCCprofileData)
    {
      hh = (char **)fr->iCCprofileData;
      pdata = (const void *)(hh ? *hh : NULL);
      psize = (int)fr->iCCprofileSize;
    }
    pb.input_custom_xyz_valid = 0;
    if(pdata && psize > 0)
    {
      double m[9], w[2];
      int tr = (int)SP_TRANSFER_SRGB;
      if(sf_icc_parse_profile(pdata, psize, m, w, &tr) == 0)
      {
        pb.input_custom_xyz_valid = 1;
        for(int i = 0; i < 9; i++) pb.input_custom_rgb_to_xyz[i] = m[i];
        pb.input_custom_white_xy[0] = w[0];
        pb.input_custom_white_xy[1] = w[1];
        if(tr >= 0 && tr < SP_TRANSFER_COUNT) pb.input_transfer = tr;
      }
    }
  }
  sf_params_to_chain(&pb, &cp);
  sf_set_long_edge(fr, &cp);

  /* Building the chain loads the pack and bakes the LUTs (tens of ms); honour
   * a queued cancel before that work rather than after. */
  if(fr->abortProc && fr->abortProc()) return userCanceledErr;

  g_ctx.chain = sp_chain_create(&cp, &err);
  if(!g_ctx.chain)
  {
    sf_set_err(err ? err : "spektrafilm: could not build the render chain.");
    if(err) free(err);
    return filterBadParameters;
  }
  if(err) free(err);

  /* One tile: the entire selection. Opt into 32-bit coordinates when the host
   * offers them (gmic-8bf, a known-good 8bf plug-in, does the same) and write
   * BOTH rect families so the request is unambiguous whichever the host
   * reads. */
  if(fr->bigDocumentData) fr->bigDocumentData->PluginUsing32BitCoordinates = 1;

  fr->inLoPlane = 0;
  fr->inHiPlane = (int16)(planes - 1);
  fr->outLoPlane = 0;
  fr->outHiPlane = (int16)(planes - 1);

  {
    VRect sel;
    sf_filter_selection_rect(fr, &sel);
    sf_set_in_rect(fr, &sel);
    sf_set_out_rect(fr, &sel);
    sf_log("REQ rect=(%d,%d,%d,%d) planes=%d..%d",
           sel.top, sel.left, sel.bottom, sel.right,
           (int)fr->inLoPlane, (int)fr->inHiPlane);
  }

  g_ctx.active = 1;
  return noErr;
}

/* ------------------------------------------------------------ continue -- */
OSErr sf_filter_continue(FilterRecordPtr fr)
{
  int w, h, planes, depth;
  float *fl;
  VRect ir, orr;
  const void *src;
  void *dst;
  int src_rb, dst_rb;

  if(!fr || !g_ctx.active || g_ctx.done) return noErr;

  sf_get_in_rect(fr, &ir);
  sf_get_out_rect(fr, &orr);
  w = orr.right - orr.left;
  h = orr.bottom - orr.top;

  sf_log("CONT in=(%d,%d,%d,%d) out=(%d,%d,%d,%d) 32=%d inData=%p outData=%p inRB=%d outRB=%d",
         ir.top, ir.left, ir.bottom, ir.right,
         orr.top, orr.left, orr.bottom, orr.right, sf_use32(fr),
         fr->inData, fr->outData, fr->inRowBytes, fr->outRowBytes);

  if(w <= 0 || h <= 0)
  {
    /* nothing requested: tell the host we're finished */
    sf_finish_tile(fr);
    return noErr;
  }

  /* A host that does not pre-fill the Continue buffers (Affinity Photo) hands
   * us neither inData nor outData; it only fills tiles we pull through
   * AdvanceStateProc. Drive the whole transfer ourselves in that case. */
  if(!fr->inData && !fr->outData && fr->advanceState)
    return sf_pull_run(fr);

  /* The slice requests (and later indexes with) inRect and outRect as the same
   * rect. If the host hands back different rects, the delivered inData/outData
   * sizes no longer match the rects we stride with — refuse rather than read
   * or write past the buffer. Prepare() declares the working set precisely so
   * the host can grant the full selection in one tile. */
  if(ir.left != orr.left || ir.top != orr.top ||
     ir.right != orr.right || ir.bottom != orr.bottom)
  {
    sf_set_err("spektrafilm: host returned a mismatched tile.");
    return filterBadParameters;
  }

  planes = fr->inHiPlane - fr->inLoPlane + 1;
  depth = fr->depth;

  /* Some hosts implement the "copy source to destination" case (our PiPL
   * sets PIFilterDontCopyToDestinationBit = 0, i.e. they may) by leaving
   * inData NULL and pre-copying the source into outData. Read the source from
   * wherever it actually is: inData normally, outData when that is all the
   * host provided. The same fallback lets a host that only exposes one buffer
   * still work (source and sink coincide, the pass-through copy is a no-op). */
  src = fr->inData ? fr->inData : fr->outData;
  dst = fr->outData ? fr->outData : fr->inData;
  src_rb = fr->inData ? fr->inRowBytes : fr->outRowBytes;
  dst_rb = fr->outData ? fr->outRowBytes : fr->inRowBytes;

  if(!src || !dst)
  {
    sf_set_err("spektrafilm: host supplied no pixel buffer.");
    return filterBadParameters;
  }

  /* The render is one atomic full-frame pass (the coupler blur seams if cut),
   * so a cancel can only land before it starts or at a stage boundary we report
   * through sf_progress_bridge. */
  if(fr->abortProc && fr->abortProc())
  {
    sf_finish_tile(fr);
    return userCanceledErr;
  }

  fl = (float *)malloc((size_t)w * (size_t)h * 3 * sizeof(float));
  if(!fl)
  {
    sf_set_err("spektrafilm: out of memory.");
    return filterBadParameters;
  }

  if(fr->progressProc) fr->progressProc(0, 100);

  /* carry alpha/extra planes through verbatim, then rewrite colour */
  sf_copy_tile(src, dst, src_rb, dst_rb, w, h, planes, depth);

  if(sf_pixels_to_float(src, src_rb, w, h, planes, depth, fl) != 0)
  {
    free(fl);
    sf_set_err("spektrafilm: unsupported channel depth.");
    return filterBadParameters;
  }

  {
    OSErr derr = sf_show_dialog(fr, fl, w, h);
    if(derr != noErr)
    {
      free(fl);
      sf_finish_tile(fr);
      return derr;
    }
  }

  {
    const int rrc = sf_rf_render(fr, fl, w, h);
    if(rrc != 0)
    {
      free(fl);
      sf_finish_tile(fr);
      if(rrc == 3) return userCanceledErr;
      sf_set_err("spektrafilm: render failed.");
      return filterBadParameters;
    }
  }

  sf_float_to_pixels(fl, dst, dst_rb, w, h, planes, depth, fr->filterRect.top);
  free(fl);

  if(fr->progressProc) fr->progressProc(100, 100);

  /* done: empty the request so the host stops calling Continue */
  sf_finish_tile(fr);
  sf_log("DONE");
  return noErr;
}

/* -------------------------------------------------------------- finish -- */
OSErr sf_filter_finish(FilterRecordPtr fr)
{
  (void)fr;
  sf_filter_reset();
  g_dialog_wanted = 0; /* end of this invocation */
  return noErr;
}