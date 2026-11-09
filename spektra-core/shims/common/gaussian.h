/* SPDX-License-Identifier: GPL-3.0-or-later */
/* spektra-core extraction spike — shim for darktable's "common/gaussian.h".
 *
 * Real darktable gaussian.h includes "common/opencl.h"; here we declare only
 * the two entry points the spatial core (spektra_core.c) calls. The matching
 * implementations in dt_stubs.c are copied verbatim from darktable's
 * src/common/gaussian.c and must stay byte-identical for GPU parity.
 */
#pragma once

/* Young–van Vliet–Verbeek IIR gaussian coefficients.
 * out[0] = b sum term, out[1..3] = forward feedback coeffs b1/b0 b2/b0 b3/b0. */
void dt_gaussian_yvv_coeffs(const float sigma, float out[4]);

/* Direct truncated gaussian kernel. Returns the integer radius used; writes
 * kernel[0 .. 2*radius] (normalised, sum == 1). */
int dt_gaussian_kernel_1d(const float sigma, float *const kernel, const int max_radius);