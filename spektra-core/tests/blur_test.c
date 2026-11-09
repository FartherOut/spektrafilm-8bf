/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Isolate spatial blowup: step image -> sf_blur_plane3 -> manual unsharp fma. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spektra_core.h"

int main(void)
{
  const int w = 64, h = 64;
  static float buf[64 * 64 * 3], work[64 * 64], out[64 * 64 * 3];
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++)
    {
      const float v = (x < 24) ? 0.10f : 0.60f;
      for(int c = 0; c < 3; c++) buf[(y * w + x) * 3 + c] = v;
      out[(y * w + x) * 3] = buf[(y * w + x) * 3];
    }

  printf("-- blur input row y=32: ");
  for(int x = 10; x <= 40; x++) printf("%4.2f ", buf[(32 * w + x) * 3]);
  printf("\n");

  sf_blur_plane3(buf, w, h, 2.0f, work);

  printf("-- blurred      row y=32: ");
  for(int x = 10; x <= 40; x++) printf("%4.2f ", buf[(32 * w + x) * 3]);
  printf("\n");

  float mn = 1e9f, mx = -1e9f;
  for(int i = 0; i < w * h * 3; i++)
  {
    const float v = buf[i];
    if(v < mn) mn = v;
    if(v > mx) mx = v;
  }
  printf("blurred min %g max %g\n", mn, mx);
  return 0;
}