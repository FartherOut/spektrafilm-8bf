/* SPDX-License-Identifier: GPL-3.0-or-later */
/* sf_preview.c — the parameter dialog's preview (see sf_preview.h).
 *
 * sf_preview_render_image() pushes the pane-sized 1:1 region of the user's
 * actual image (and sf_preview_render_fit() a supersampled whole-frame view)
 * through the same sp_chain the final render uses, so film/paper/process/
 * exposure (and, once they land, the spatial stages) behave exactly as they
 * will on the full frame. sf_preview_render() is the older built-in reference
 * scene, kept as the fallback when no pixels are available (and exercised by
 * the Linux harness). Every path finishes with sf_preview_display(), which
 * converts the space-preserving (document-space) result to sRGB for the plain,
 * unmanaged preview control. No host pixels, no Win32 here (the control wiring
 * lives in the GUI layer, sf_gui_imgui.cpp).
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sp_chain.h"
#include "sp_color.h"
#include "params.h"
#include "sf_preview.h"

/* Pan of the 1:1 crop, as a fraction of the available slack in [-1, 1]. Set by
 * the dialog while the user drags the preview; read by sf_preview_render_image. */
static double g_pan_x;
static double g_pan_y;

void sf_preview_set_pan(double fx, double fy)
{
  if(fx < -1.0) fx = -1.0;
  if(fx > 1.0) fx = 1.0;
  if(fy < -1.0) fy = -1.0;
  if(fy > 1.0) fy = 1.0;
  g_pan_x = fx;
  g_pan_y = fy;
}

double sf_preview_pan_x(void) { return g_pan_x; }
double sf_preview_pan_y(void) { return g_pan_y; }

/* ------------------------------------------------------- display transform --
 * The chain writes space-preserving output: samples stay in the document's own
 * primaries/white/transfer. The preview is a plain Win32 bitmap that neither the
 * host nor Windows colour-manages, so present it the way a colour-managed viewer
 * would show the document:
 *
 *   document-encoded -> linear document -> XYZ (document white)
 *     -> Bradford CAT to D65 -> linear sRGB -> sRGB-encoded
 *
 * For the sRGB default this is the identity and the bytes are left untouched. */

static void mat3_mul(const double a[9], const double b[9], double o[9])
{
  double t[9];
  int r, c;
  for(r = 0; r < 3; r++)
    for(c = 0; c < 3; c++)
      t[r * 3 + c] = a[r * 3 + 0] * b[c] + a[r * 3 + 1] * b[3 + c] +
                     a[r * 3 + 2] * b[6 + c];
  memcpy(o, t, sizeof t);
}

static int mat3_inv(const double m[9], double o[9])
{
  const double a = m[0], b = m[1], c = m[2];
  const double d = m[3], e = m[4], f = m[5];
  const double g = m[6], h = m[7], i = m[8];
  const double A = e * i - f * h, B = f * g - d * i, C = d * h - e * g;
  const double det = a * A + b * B + c * C;
  const double id = 1.0 / det;
  if(det == 0.0 || !isfinite(det)) return 0;
  o[0] = A * id; o[1] = (c * h - b * i) * id; o[2] = (b * f - c * e) * id;
  o[3] = B * id; o[4] = (a * i - c * g) * id; o[5] = (c * d - a * f) * id;
  o[6] = C * id; o[7] = (b * g - a * h) * id; o[8] = (a * e - b * d) * id;
  return 1;
}

static void xy_to_xyz(const double xy[2], double xyz[3])
{
  xyz[0] = xy[0] / xy[1];
  xyz[1] = 1.0;
  xyz[2] = (1.0 - xy[0] - xy[1]) / xy[1];
}

static void mat3_mulv(const double m[9], const double v[3], double o[3])
{
  o[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
  o[1] = m[3] * v[0] + m[4] * v[1] + m[5] * v[2];
  o[2] = m[6] * v[0] + m[7] * v[1] + m[8] * v[2];
}

/* Bradford chromatic-adaptation matrix, src white (xy) -> dst white (xy). The
 * cone-response matrix and its inverse are the published Bradford values. */
static int bradford(const double src_xy[2], const double dst_xy[2], double o[9])
{
  static const double MB[9] = { 0.8951,  0.2664, -0.1614,
                               -0.7502,  1.7135,  0.0367,
                                0.0389, -0.0685,  1.0296 };
  static const double MBI[9] = {  0.9869929, -0.1470543, 0.1599627,
                                  0.4323053,  0.5183603, 0.0492912,
                                 -0.0085287,  0.0400428, 0.9684867 };
  double ws[3], wd[3], cs[3], cd[3], d[9], t[9];
  xy_to_xyz(src_xy, ws);
  xy_to_xyz(dst_xy, wd);
  mat3_mulv(MB, ws, cs);
  mat3_mulv(MB, wd, cd);
  if(cs[0] == 0.0 || cs[1] == 0.0 || cs[2] == 0.0) return 0;
  memset(d, 0, sizeof d);
  d[0] = cd[0] / cs[0];
  d[4] = cd[1] / cs[1];
  d[8] = cd[2] / cs[2];
  mat3_mul(MBI, d, t);  /* MB^-1 * D         */
  mat3_mul(t, MB, o);   /* MB^-1 * D * MB    */
  return 1;
}

void sf_preview_display(float *rgb, size_t npix, const sf_params_block *b)
{
  double md[9], wd[2], ms[9], ws[2], msi[9], cat[9], t[9], m[9];
  size_t i;

  if(!rgb || !b || npix == 0) return;
  /* The document is already sRGB/sRGB: the chain output is display-ready. */
  if(!b->input_custom_xyz_valid && b->input_space == 0 &&
     b->input_transfer == SP_TRANSFER_SRGB)
    return;

  if(b->input_custom_xyz_valid)
  {
    for(i = 0; i < 9; i++) md[i] = b->input_custom_rgb_to_xyz[i];
    wd[0] = b->input_custom_white_xy[0];
    wd[1] = b->input_custom_white_xy[1];
  }
  else sp_color_space_matrix(b->input_space, md, wd);

  sp_color_space_matrix(0, ms, ws); /* sRGB display space (D65) */
  if(!mat3_inv(ms, msi)) return;
  if(!bradford(wd, ws, cat)) return;
  mat3_mul(cat, md, t); /* document linear -> XYZ (D65) */
  mat3_mul(msi, t, m);  /* XYZ (D65) -> linear sRGB      */

  for(i = 0; i < npix; i++)
  {
    double lin[3], srgb[3];
    lin[0] = sp_transfer_decode(b->input_transfer, rgb[i * 3 + 0]);
    lin[1] = sp_transfer_decode(b->input_transfer, rgb[i * 3 + 1]);
    lin[2] = sp_transfer_decode(b->input_transfer, rgb[i * 3 + 2]);
    mat3_mulv(m, lin, srgb);
    rgb[i * 3 + 0] = sp_transfer_encode(SP_TRANSFER_SRGB, (float)srgb[0]);
    rgb[i * 3 + 1] = sp_transfer_encode(SP_TRANSFER_SRGB, (float)srgb[1]);
    rgb[i * 3 + 2] = sp_transfer_encode(SP_TRANSFER_SRGB, (float)srgb[2]);
  }
}

/* ------------------------------------------------------------------ hsv -- */
static void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b)
{
  float hh = h * 6.0f;
  int i = (int)floorf(hh) % 6;
  if(i < 0) i += 6;
  const float f = hh - floorf(hh);
  const float p = v * (1.0f - s);
  const float q = v * (1.0f - s * f);
  const float t = v * (1.0f - s * (1.0f - f));
  switch(i)
  {
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
  }
}

/* The reference scene, in the same plain 0..1 domain the host feeds the engine
 * (see pix_convert.h: the slice deliberately does no transfer curve, so the
 * preview and the saved render agree). Three horizontal bands:
 *   top    - a neutral gray ramp (shows contrast / the film's toe & shoulder)
 *   middle - a saturated hue sweep (shows colour behaviour)
 *   bottom - skin / sky / foliage / highlight patches
 */
static void proxy_fill(float *img, int w, int h)
{
  int x, y;
  for(y = 0; y < h; y++)
  {
    const float fy = (h > 1) ? (float)y / (float)(h - 1) : 0.0f;
    for(x = 0; x < w; x++)
    {
      const float fx = (w > 1) ? (float)x / (float)(w - 1) : 0.0f;
      float r, g, b;

      if(fy < 0.34f)
      {
        /* smooth gray ramp through a few notable steps */
        float v = 0.02f + 0.96f * fx;
        v += 0.02f * (0.5f - fy); /* a touch of vertical shading */
        r = g = b = v;
      }
      else if(fy < 0.67f)
      {
        hsv_to_rgb(fx, 0.65f, 0.88f, &r, &g, &b);
      }
      else
      {
        /* four patches, each shaded slightly top-to-bottom */
        const int band = (int)(fx * 4.0f);
        const float shade = 0.92f + 0.08f * fy;
        switch(band < 0 ? 0 : (band > 3 ? 3 : band))
        {
          case 0: r = 0.86f; g = 0.66f; b = 0.53f; break; /* skin    */
          case 1: r = 0.34f; g = 0.55f; b = 0.85f; break; /* sky     */
          case 2: r = 0.20f; g = 0.45f; b = 0.18f; break; /* foliage */
          default: r = g = b = 0.95f; break;              /* highlight */
        }
        r *= shade; g *= shade; b *= shade;
      }

      img[((size_t)y * w + x) * 3 + 0] = r;
      img[(size_t)(y * w + x) * 3 + 1] = g;
      img[(size_t)(y * w + x) * 3 + 2] = b;
    }
  }
}

unsigned char *sf_preview_render(const sf_params_block *b, int w, int h,
                                 char **err)
{
  size_t npix, i;
  float *in;
  float *out;
  unsigned char *rgb;
  char *e = NULL;

  if(err) *err = NULL;
  if(!b) return NULL;
  if(w < 8) w = 8;
  if(h < 8) h = 8;

  npix = (size_t)w * (size_t)h;
  in = (float *)malloc(npix * 3u * sizeof(float));
  out = (float *)malloc(npix * 3u * sizeof(float));
  rgb = (unsigned char *)malloc(npix * 3u);
  if(!in || !out || !rgb)
  {
    free(in); free(out); free(rgb);
    return NULL;
  }

  proxy_fill(in, w, h);

  sp_chain_params_t cp;
  sf_params_to_chain(b, &cp);
  cp.image_long_edge_px = (w > h) ? (double)w : (double)h;
  sp_chain_t *c = sp_chain_create(&cp, &e);
  if(!c)
  {
    if(err) *err = e; else free(e);
    free(in); free(out); free(rgb);
    return NULL;
  }

  const int rc = sp_chain_render(c, in, out, w, h, 3);
  sp_chain_destroy(c);
  if(rc != 0)
  {
    free(in); free(out); free(rgb);
    if(err)
    {
      char *m = (char *)malloc(32);
      if(m) strcpy(m, "preview: render failed");
      *err = m;
    }
    return NULL;
  }

  sf_preview_display(out, npix, b);

  const size_t nbytes = (size_t)w * (size_t)h * 3u;
  for(i = 0; i < nbytes; i++)
  {
    float v = out[i];
    if(!(v > 0.0f)) v = 0.0f;
    if(v > 1.0f) v = 1.0f;
    rgb[i] = (unsigned char)(v * 255.0f + 0.5f);
  }

  free(in);
  free(out);
  return rgb;
}

/* ---------------------------------------------------------- real image -- */

static unsigned char preview_clip8(float v)
{
  if(!(v > 0.0f)) v = 0.0f;
  if(v > 1.0f) v = 1.0f;
  return (unsigned char)(v * 255.0f + 0.5f);
}

/* Box-average src (sw x sh packed floats) into dst (dw x dh packed floats):
 * an exact-size resample. (int64_t) index maths avoids overflow on huge
 * sources. */
static void preview_box(const float *src, int sw, int sh,
                        float *dst, int dw, int dh)
{
  int x, y;
  for(y = 0; y < dh; y++)
  {
    int sy0 = (int)((int64_t)y * sh / dh);
    int sy1 = (int)((int64_t)(y + 1) * sh / dh);
    if(sy1 <= sy0) sy1 = sy0 + 1;
    if(sy1 > sh) sy1 = sh;
    for(x = 0; x < dw; x++)
    {
      int sx0 = (int)((int64_t)x * sw / dw);
      int sx1 = (int)((int64_t)(x + 1) * sw / dw);
      float r = 0.0f, g = 0.0f, b = 0.0f;
      int cnt = 0, yy, xx;
      float *d;

      if(sx1 <= sx0) sx1 = sx0 + 1;
      if(sx1 > sw) sx1 = sw;

      for(yy = sy0; yy < sy1; yy++)
        for(xx = sx0; xx < sx1; xx++)
        {
          const float *p = src + ((size_t)yy * (size_t)sw + (size_t)xx) * 3u;
          r += p[0]; g += p[1]; b += p[2];
          cnt++;
        }
      if(cnt < 1) cnt = 1;

      d = dst + ((size_t)y * (size_t)dw + (size_t)x) * 3u;
      d[0] = r / (float)cnt;
      d[1] = g / (float)cnt;
      d[2] = b / (float)cnt;
    }
  }
}

unsigned char *sf_preview_render_image(const float *src, int sw, int sh,
                                       const sf_params_block *b, int out_w,
                                       int out_h, char **err)
{
  int cw, ch, x0, y0, ox, oy, x, y;
  size_t npix;
  float *img = NULL;
  float *out = NULL;
  unsigned char *rgb = NULL;
  sp_chain_params_t cp;
  sp_chain_t *c = NULL;
  char *e = NULL;

  if(err) *err = NULL;
  if(!src || !b || sw <= 0 || sh <= 0 || out_w <= 0 || out_h <= 0) return NULL;

  /* True 1:1: render exactly the pane-sized region at native scale, positioned
   * by the pan slack; the blit below does no resampling, so every preview
   * pixel matches the full-resolution render at 100% zoom. */
  cw = (sw < out_w) ? sw : out_w;
  ch = (sh < out_h) ? sh : out_h;
  x0 = (sw - cw) / 2 + (int)(sf_preview_pan_x() * (sw - cw) / 2.0);
  y0 = (sh - ch) / 2 + (int)(sf_preview_pan_y() * (sh - ch) / 2.0);
  if(x0 < 0) x0 = 0;
  if(y0 < 0) y0 = 0;
  if(x0 > sw - cw) x0 = sw - cw;
  if(y0 > sh - ch) y0 = sh - ch;

  npix = (size_t)cw * (size_t)ch;
  img = (float *)malloc(npix * 3u * sizeof(float));
  out = (float *)malloc(npix * 3u * sizeof(float));
  rgb = (unsigned char *)malloc((size_t)out_w * (size_t)out_h * 3u);
  if(!img || !out || !rgb)
  {
    free(img); free(out); free(rgb);
    return NULL;
  }

  /* Copy the crop out first: the source belongs to the caller (the frame being
   * rendered) and must not be touched. */
  for(x = 0; x < ch; x++)
  {
    const float *s = src + ((size_t)(y0 + x) * (size_t)sw + (size_t)x0) * 3u;
    memcpy(img + (size_t)x * (size_t)cw * 3u, s, (size_t)cw * 3u * sizeof(float));
  }

  sf_params_to_chain(b, &cp);
  /* Film format resolves against the *full* source's long edge, so the crop
   * carries the same film pixel size as the final render. */
  cp.image_long_edge_px = (sw > sh) ? (double)sw : (double)sh;
  c = sp_chain_create(&cp, &e);
  if(!c)
  {
    if(err) *err = e; else free(e);
    free(img); free(out); free(rgb);
    return NULL;
  }

  if(sp_chain_render(c, img, out, cw, ch, 3) != 0)
  {
    sp_chain_destroy(c);
    free(img); free(out); free(rgb);
    if(err)
    {
      char *m = (char *)malloc(32);
      if(m) strcpy(m, "preview: render failed");
      *err = m;
    }
    return NULL;
  }
  sp_chain_destroy(c);
  free(img);

  sf_preview_display(out, npix, b);

  /* Blit 1:1, centred with dark bars when the image is smaller than the pane. */
  ox = (out_w - cw) / 2;
  oy = (out_h - ch) / 2;
  memset(rgb, 0, (size_t)out_w * (size_t)out_h * 3u);
  for(y = 0; y < ch; y++)
  {
    const float *s = out + ((size_t)y * (size_t)cw * 3u);
    unsigned char *d = rgb + ((size_t)(oy + y) * (size_t)out_w + (size_t)ox) * 3u;
    for(x = 0; x < cw; x++, d += 3u)
    {
      d[0] = preview_clip8(s[x * 3u + 0]);
      d[1] = preview_clip8(s[x * 3u + 1]);
      d[2] = preview_clip8(s[x * 3u + 2]);
    }
  }
  free(out);
  return rgb;
}

/* Whole-frame preview, supersampled: the frame is box-downsampled to *twice*
 * the display size (never below native), pushed through the same chain as the
 * final render, and only the *rendered result* is shrunk to the pane. Halation
 * sources, fine texture and grain therefore still exist when the spatial
 * stages act on them, so the result much closer matches the full-resolution
 * render than a pane-size pre-downsample would. */
unsigned char *sf_preview_render_fit(const float *src, int sw, int sh,
                                     const sf_params_block *b, int out_w,
                                     int out_h, char **err)
{
  int fw, fh, rw, rh, ox, oy, x, y;
  double sc, rs;
  float *small_in = NULL;
  float *small_out = NULL;
  float *fit_out = NULL;
  unsigned char *rgb = NULL;
  sf_params_block bcopy;
  sp_chain_params_t cp;
  sp_chain_t *c = NULL;
  char *e = NULL;

  if(err) *err = NULL;
  if(!src || !b || sw <= 0 || sh <= 0 || out_w <= 0 || out_h <= 0) return NULL;

  /* Display size: the whole frame scaled to fit the control. */
  sc = (double)out_w / (double)sw;
  if((double)out_h / (double)sh < sc) sc = (double)out_h / (double)sh;
  fw = (int)(sc * (double)sw + 0.5);
  fh = (int)(sc * (double)sh + 0.5);
  if(fw < 1) fw = 1;
  if(fw > out_w) fw = out_w;
  if(fh < 1) fh = 1;
  if(fh > out_h) fh = out_h;
  ox = (out_w - fw) / 2;
  oy = (out_h - fh) / 2;

  /* Render size: up to 2x the display size, never beyond native resolution. */
  rs = 2.0 * sc;
  if(rs > 1.0) rs = 1.0;
  rw = (int)(rs * (double)sw + 0.5);
  rh = (int)(rs * (double)sh + 0.5);
  if(rw < 1) rw = 1;
  if(rw > sw) rw = sw;
  if(rh < 1) rh = 1;
  if(rh > sh) rh = sh;

  small_in = (float *)malloc((size_t)rw * (size_t)rh * 3u * sizeof(float));
  small_out = (float *)malloc((size_t)rw * (size_t)rh * 3u * sizeof(float));
  fit_out = (float *)malloc((size_t)fw * (size_t)fh * 3u * sizeof(float));
  rgb = (unsigned char *)malloc((size_t)out_w * (size_t)out_h * 3u);
  if(!small_in || !small_out || !fit_out || !rgb)
  {
    free(small_in); free(small_out); free(fit_out); free(rgb);
    return NULL;
  }

  preview_box(src, sw, sh, small_in, rw, rh);

  /* Rendered at reduced resolution, so scale the micron pixel pitch by the same
   * factor. Spatial stages then cover proportionally more pixels here and match
   * the full-resolution render's relative look. */
  bcopy = *b;
  bcopy.pixel_um = b->pixel_um / (float)rs;

  sf_params_to_chain(&bcopy, &cp);
  /* The fit preview is rendered at reduced resolution, so resolve the film
   * format against the *rendered* long edge: the derived film pixel size then
   * scales by the same factor as the manual pixel_um above. */
  cp.image_long_edge_px = (double)(rw > rh ? rw : rh);
  if(!(cp.image_long_edge_px > 0.0))
    cp.image_long_edge_px = (sw > sh) ? (double)sw : (double)sh;
  c = sp_chain_create(&cp, &e);
  if(!c)
  {
    if(err) *err = e; else free(e);
    free(small_in); free(small_out); free(fit_out); free(rgb);
    return NULL;
  }

  if(sp_chain_render(c, small_in, small_out, rw, rh, 3) != 0)
  {
    sp_chain_destroy(c);
    free(small_in); free(small_out); free(fit_out); free(rgb);
    if(err)
    {
      char *m = (char *)malloc(32);
      if(m) strcpy(m, "preview: render failed");
      *err = m;
    }
    return NULL;
  }
  sp_chain_destroy(c);
  free(small_in);

  sf_preview_display(small_out, (size_t)rw * (size_t)rh, &bcopy);

  /* Shrink the rendered result (not the source) to the display size. */
  preview_box(small_out, rw, rh, fit_out, fw, fh);
  free(small_out);

  memset(rgb, 0, (size_t)out_w * (size_t)out_h * 3u); /* dark bars */
  for(y = 0; y < fh; y++)
    for(x = 0; x < fw; x++)
    {
      const float *p = fit_out + ((size_t)y * (size_t)fw + (size_t)x) * 3u;
      unsigned char *d =
          rgb + ((size_t)(oy + y) * (size_t)out_w + (size_t)(ox + x)) * 3u;
      d[0] = preview_clip8(p[0]);
      d[1] = preview_clip8(p[1]);
      d[2] = preview_clip8(p[2]);
    }
  free(fit_out);
  return rgb;
}
