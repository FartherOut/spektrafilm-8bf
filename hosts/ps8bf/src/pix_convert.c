/* SPDX-License-Identifier: GPL-3.0-or-later */
/* pix_convert.c — implementation (see pix_convert.h). */
#include <math.h>
#include <string.h>

#include "pix_convert.h"

/* ------------------------------------------------------------ transfer -- */
/* The host byte <-> linear transfer is applied inside sp_chain_render (see
 * sp_chain_params_t.input_transfer): these functions only scale raw samples to
 * 0..1 and back. Keeping the transfer out of the adapter is what lets the
 * preview, the plugin and refrender all share one decode/encode. */

/* --------------------------------------------------------------- decode -- */
int sf_pixels_to_float(const void *in, int in_row_bytes, int w, int h,
                       int planes, int depth, float *out)
{
  int y, x;
  if(!in || !out || w <= 0 || h <= 0 || planes < 3) return 1;
  if(depth != 8 && depth != 16) return 2;

  if(depth == 8)
  {
    const unsigned char *base = (const unsigned char *)in;
    for(y = 0; y < h; y++)
    {
      const unsigned char *row = base + (size_t)y * (size_t)in_row_bytes;
      float *o = out + (size_t)y * w * 3;
      for(x = 0; x < w; x++)
      {
        const unsigned char *p = row + (size_t)x * planes;
        /* Divide, do not multiply by (1.0f / 255.0f): that reciprocal is
         * itself rounded, so it lands 1 ulp off for 126 of the 256 codes and
         * the render stops matching tools/refrender, which divides. 16-bit is
         * unaffected either way because SF_SAMPLE_MAX_16 is a power of two. */
        o[0] = p[0] / 255.0f;
        o[1] = p[1] / 255.0f;
        o[2] = p[2] / 255.0f;
        o += 3;
      }
    }
  }
  else
  {
    const unsigned short *base = (const unsigned short *)in;
    for(y = 0; y < h; y++)
    {
      const unsigned short *row =
          (const unsigned short *)((const unsigned char *)base +
                                   (size_t)y * (size_t)in_row_bytes);
      float *o = out + (size_t)y * w * 3;
      for(x = 0; x < w; x++)
      {
        const unsigned short *p = row + (size_t)x * planes;
        o[0] = p[0] / SF_SAMPLE_MAX_16;
        o[1] = p[1] / SF_SAMPLE_MAX_16;
        o[2] = p[2] / SF_SAMPLE_MAX_16;
        o += 3;
      }
    }
  }
  return 0;
}

/* --------------------------------------------------------------- encode -- */
#if SF_QUANT_DITHER
static const float SF_BAYER4[4][4] = {
  { 0.0f / 16.0f,  8.0f / 16.0f,  2.0f / 16.0f, 10.0f / 16.0f },
  { 12.0f / 16.0f, 4.0f / 16.0f, 14.0f / 16.0f,  6.0f / 16.0f },
  { 3.0f / 16.0f, 11.0f / 16.0f,  1.0f / 16.0f,  9.0f / 16.0f },
  { 15.0f / 16.0f, 7.0f / 16.0f, 13.0f / 16.0f,  5.0f / 16.0f },
};
#endif

static float sf_clamp01(float v)
{
  if(v < 0.0f) return 0.0f;
  if(v > 1.0f) return 1.0f;
  return v;
}

int sf_float_to_pixels(const float *in, void *out, int out_row_bytes, int w,
                       int h, int planes, int depth, int row)
{
  int y, x;
  if(!in || !out || w <= 0 || h <= 0 || planes < 3) return 1;
  if(depth != 8 && depth != 16) return 2;

  if(depth == 8)
  {
    unsigned char *base = (unsigned char *)out;
    for(y = 0; y < h; y++)
    {
      unsigned char *orow = base + (size_t)y * (size_t)out_row_bytes;
      const float *i = in + (size_t)y * w * 3;
      for(x = 0; x < w; x++)
      {
        float r = sf_clamp01(i[0]);
        float g = sf_clamp01(i[1]);
        float b = sf_clamp01(i[2]);
#if SF_QUANT_DITHER
        {
          float d = (SF_BAYER4[(row + y) & 3][x & 3] - 0.5f) * (1.0f / 255.0f);
          r += d; g += d; b += d;
        }
#endif
        orow[0] = (unsigned char)(sf_clamp01(r) * 255.0f + 0.5f);
        orow[1] = (unsigned char)(sf_clamp01(g) * 255.0f + 0.5f);
        orow[2] = (unsigned char)(sf_clamp01(b) * 255.0f + 0.5f);
        orow += planes;
        i += 3;
      }
    }
  }
  else
  {
    unsigned char *base = (unsigned char *)out;
    for(y = 0; y < h; y++)
    {
      unsigned short *orow =
          (unsigned short *)(base + (size_t)y * (size_t)out_row_bytes);
      const float *i = in + (size_t)y * w * 3;
      for(x = 0; x < w; x++)
      {
        float r = sf_clamp01(i[0]);
        float g = sf_clamp01(i[1]);
        float b = sf_clamp01(i[2]);
#if SF_QUANT_DITHER
        {
          float d = (SF_BAYER4[(row + y) & 3][x & 3] - 0.5f) * (1.0f / SF_SAMPLE_MAX_16);
          r += d; g += d; b += d;
        }
#endif
        orow[0] = (unsigned short)(sf_clamp01(r) * SF_SAMPLE_MAX_16 + 0.5f);
        orow[1] = (unsigned short)(sf_clamp01(g) * SF_SAMPLE_MAX_16 + 0.5f);
        orow[2] = (unsigned short)(sf_clamp01(b) * SF_SAMPLE_MAX_16 + 0.5f);
        orow += planes;
        i += 3;
      }
    }
  }
  return 0;
}

/* ---------------------------------------------------------- tile copy ---- */
void sf_copy_tile(const void *in, void *out, int in_row_bytes, int out_row_bytes,
                  int w, int h, int planes, int depth)
{
  int y;
  const int sample = (depth == 16) ? 2 : 1;
  size_t copy;
  if(in == out) return;
  copy = (size_t)w * (size_t)planes * (size_t)sample;
  if(copy > (size_t)in_row_bytes) copy = (size_t)in_row_bytes;
  if(copy > (size_t)out_row_bytes) copy = (size_t)out_row_bytes;
  for(y = 0; y < h; y++)
    memcpy((unsigned char *)out + (size_t)y * out_row_bytes,
           (const unsigned char *)in + (size_t)y * in_row_bytes, copy);
}