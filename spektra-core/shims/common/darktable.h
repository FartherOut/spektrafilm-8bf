/* SPDX-License-Identifier: GPL-3.0-or-later */
/* spektra-core extraction spike — minimal darktable-compat shim.
 *
 * Provides just enough of darktable's "common/darktable.h" that the portable
 * C sources (spektra_core.c, spektra_sim.c) need. The real darktable header
 * drags in the whole host: OpenCL, GTK, image I/O, memory pools. None of that
 * belongs in a CPU-only film-simulation core.
 *
 * These shims are deliberately trivial for the spike. Where bit-exactness with
 * the GPU kernel or with darktable matters (the gaussian kernels), the code is
 * copied verbatim from darktable's src/common/gaussian.c — do not "improve"
 * it. Parity hardening passes later verify that.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- dt macros used by the portable sources ---- */
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef ABS
#define ABS(a) (((a) >= 0) ? (a) : -(a))
#endif
#ifndef CLAMPF
#define CLAMPF(a, b, c) MIN(MAX(a, b), c)
#endif

/* OpenMP loop helper. Build with -fopenmp to enable threading; with the
 * macros empty (no -fopenmp) the pragmas vanish and the code stays
 * single-threaded and deterministic — which is what the parity harness wants. */
#define DT_OMP_FOR(...)
#define DT_OMP_DECLARE_SIMD(...)
#define DT_OMP_DECLARE_SIMD_ALIGNED(...)

/* dt_print debug categories: only the value used by spektra_sim.c is
 * defined; the macro below maps any category through the same stub. */
#define DT_DEBUG_PIPE 0
#define DT_DEBUG_CONTROL 0
#define DT_DEBUG_OPENCL 0

/* ---- aligned float allocation (darktable: dt_alloc_align_float) ---- */
float *dt_alloc_align_float(const size_t n);
void dt_free_align(void *p);

/* ---- logging (darktable: dt_print in common/log.h) ---- */
void dt_print(int dbgtype, const char *format, ...);