/* SPDX-License-Identifier: GPL-3.0-or-later */
/* spektra-core extraction spike — darktable-coupling stubs.
 *
 * dt_gaussian_kernel_1d / dt_gaussian_yvv_coeffs are copied VERBATIM from
 * darktable src/common/gaussian.c (master @ 8c64bf7ca9a8). They stay © the
 * darktable developers under GPL-3.0-or-later, the licence their own notices
 * give; the shim around them is this project's, under the same licence.
 * Everything else here is a trivial shim for the spike; the real core replaces
 * alloc with a platform-aligned malloc (MSVC _aligned_malloc / C11
 * aligned_alloc) and drops the logging shim.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <malloc.h> /* _aligned_malloc / _aligned_free (MSVC: <malloc.h>) */
#endif

#include "common/darktable.h"

/* MSVC has no C99 `restrict` keyword in C mode (only the __restrict extension),
 * so spell the qualifier portably. */
#if defined(_MSC_VER)
#define SF_RESTRICT __restrict
#else
#define SF_RESTRICT restrict
#endif

/* ------------------------------------------------------------------ alloc */
float *dt_alloc_align_float(const size_t n)
{
  void *p = NULL;
#if defined(_WIN32)
  p = _aligned_malloc(n * sizeof(float), 64);
#else
  if(posix_memalign(&p, 64, n * sizeof(float)) != 0) p = NULL;
#endif
  return (float *)p;
}

void dt_free_align(void *p)
{
#if defined(_WIN32)
  _aligned_free(p);
#else
  free(p);
#endif
}

/* ------------------------------------------------------------------ copy */
/* darktable's dt_iop_image_copy takes an ELEMENT (float) count, not bytes. */
void dt_iop_image_copy(void *const SF_RESTRICT out, const void *const SF_RESTRICT in, const size_t size)
{
  memcpy(out, in, size * sizeof(float));
}

/* ------------------------------------------------------------------ print */
void dt_print(int dbgtype, const char *format, ...)
{
  (void)dbgtype;
#if defined(__wasi__)
  /* The guest build must keep zero imports: there is no host stream to write
   * to. Diagnostics are dropped here (the native builds keep stderr). */
  (void)format;
#else
  va_list ap;
  va_start(ap, format);
  fprintf(stderr, "[spektra] ");
  vfprintf(stderr, format, ap);
  fputc('\n', stderr);
  va_end(ap);
#endif
}

/* ------------------------------------------- gaussian (verbatim, gaussian.c) */
void dt_gaussian_yvv_coeffs(const float sigma, float out[4])
{
  const double s = (double)sigma;
  const double q = (s >= 2.5) ? (0.98711 * s - 0.96330)
                              : (3.97156 - 4.14554 * sqrt(1.0 - 0.26891 * s));
  const double q2 = q * q, q3 = q2 * q;
  const double b0 = 1.57825 + 2.44413 * q + 1.4281 * q2 + 0.422205 * q3;
  const double b1 = 2.44413 * q + 2.85619 * q2 + 1.26661 * q3;
  const double b2 = -(1.4281 * q2 + 1.26661 * q3);
  const double b3 = 0.422205 * q3;
  out[1] = (float)(b1 / b0);
  out[2] = (float)(b2 / b0);
  out[3] = (float)(b3 / b0);
  out[0] = (float)(1.0 - (b1 + b2 + b3) / b0);
}

int dt_gaussian_kernel_1d(const float sigma, float *const kernel, const int max_radius)
{
  int radius = (int)(3.0f * sigma + 0.5f);
  if(radius < 1) radius = 1;
  if(radius > max_radius) radius = max_radius;
  double sum = 0.0;
  const double inv2s2 = 1.0 / (2.0 * (double)sigma * (double)sigma);
  for(int i = -radius; i <= radius; i++)
  {
    const double v = exp(-(double)(i * i) * inv2s2);
    kernel[i + radius] = (float)v;
    sum += v;
  }
  const float invsum = (float)(1.0 / sum);
  for(int i = 0; i < 2 * radius + 1; i++) kernel[i] *= invsum;
  return radius;
}