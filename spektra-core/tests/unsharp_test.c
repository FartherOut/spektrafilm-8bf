/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Reproduce the spike's spatial_smoke exactly, then bisect blur vs fma. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spektra_core.h"
#include "common/imagebuf.h"

int main(void)
{
  const int w = 64, h = 64, r = 32;
  float *buf = malloc((size_t)w * h * 3 * sizeof(float));
  float *orig = malloc((size_t)w * h * 3 * sizeof(float));
  float *work = malloc((size_t)w * h * sizeof(float));
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++)
    {
      const float v = (x < 24) ? 0.10f : 0.60f;
      for(int c = 0; c < 3; c++) buf[(y * w + x) * 3 + c] = v;
    }

  dt_iop_image_copy(orig, buf, (size_t)w * h * 3);
  sf_blur_plane3(buf, w, h, 2.0f, work);
  printf("-- blurred row y=32 (in-path): ");
  for(int x = 18; x <= 30; x++) printf("%4.2f ", buf[(r * w + x) * 3]);
  printf("\n");
  float mn = 1e9f, mx = -1e9f;
  for(int i = 0; i < w * h; i++) { if(work[i] < mn) mn = work[i]; if(work[i] > mx) mx = work[i]; }
  printf("-- blurred plane min/max: %g %g\n", mn, mx);

  const size_t i0 = (size_t)(r * w + 18) * 3; /* x=18, row 32, ch 0 */
  printf("-- pre-fma  orig[%zu..]=%g %g %g %g %g\n", i0, orig[i0], orig[i0+1], orig[i0+2], orig[i0+3], orig[i0+4]);
  printf("-- pre-fma  buf [%zu..]=%g %g %g %g %g\n", i0, buf[i0], buf[i0+1], buf[i0+2], buf[i0+3], buf[i0+4]);

  for(int i = 0; i < w * h * 3; i++) buf[i] = fmaf(1.5f, orig[i] - buf[i], orig[i]);
  printf("-- after manual fma  row y=32: ");
  for(int x = 18; x <= 30; x++) printf("%4.2f ", buf[(r * w + x) * 3]);
  printf("\n");
  printf("-- orig[0..4]=%g %g %g %g %g\n", orig[0], orig[1], orig[2], orig[3], orig[4]);
  free(buf); free(orig); free(work);
  return 0;
}