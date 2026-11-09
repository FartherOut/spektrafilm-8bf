/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
/* Scalar math compiled BOTH here and by data/kernels/spektrafilm.cl, which
   must agree bit-for-bit. Lives in data/kernels/ because that is the only
   directory on the OpenCL compiler's include path, at test-compile time
   (data/kernels/CMakeLists.txt) and at runtime (src/common/opencl.c).
   Any .c that includes this header needs the -ffp-contract=off treatment
   src/CMakeLists.txt applies to the spektra sources. */
#include "grain.h"

/* The whole interface is C, including when a C++ host reads it: the .8bf's
   ImGui dialog calls into these stages. Without this the C++ side mangles the
   names and the link fails. */
#ifdef __cplusplus
extern "C" {
#endif

#ifndef GRAIN_INLINE
#define GRAIN_INLINE static inline
#endif

/* Spatial effects implemented in spektra_core.c (they need dt_alloc_align_float
   and OpenMP linkage; everything else in this header is inline). */
void sf_blur_plane3(float *buf,
                    int w,
                    int h,
                    float sigma,
                    float *plane);
void sf_blur_plane3_fast(float *buf,
                         int w,
                         int h,
                         float sigma,
                         float *plane);

/* Wide Gaussian by decimation, for sigmas where even the recursive filter
   is too imprecise in float32 to give the same result on two devices: its
   input weight B falls below 1e-4 by sigma 16, so a one-ulp difference in
   the input moves the output by around 1e-3 at sigma 60-130. The field is
   box-averaged down by k = floor(sigma / SF_WIDE_BLUR_LOW_SIGMA), blurred
   with the exact kernel at the reduced sigma (below 16, so at most 48 taps
   on k^2 fewer pixels), and bilinearly interpolated back. The low-resolution sigma is reduced by the variance the
   box ((k^2 - 1) / 12) and the interpolation (k^2 / 6) add, so the total is
   the requested sigma.

   The block grid is anchored to absolute image coordinates (roi_x, roi_y),
   so tiles of one image share it. sf_wide_blur_plan() decides whether this
   applies and is shared with the OpenCL host code, which runs the same
   arithmetic in spektrafilm_wide_down / spektrafilm_wide_up. */
#define SF_WIDE_BLUR_MIN_SIGMA 16.0f
#define SF_WIDE_BLUR_LOW_SIGMA 8.0f
/* Returns 1 and fills k, sigma_low, and the low-resolution size and grid
   offset for a w x h buffer at (roi_x, roi_y) when sigma is wide enough to
   decimate; returns 0 otherwise. */
int sf_wide_blur_plan(float sigma,
                      int w,
                      int h,
                      int roi_x,
                      int roi_y,
                      int *k,
                      float *sigma_low,
                      int *lw,
                      int *lh,
                      int *ox,
                      int *oy);
/* inv[i] = 1.0f / i for i = 1..k, the divisors of the block averages. Built
   on the host for both paths so the device never divides. */
void sf_wide_blur_inv_table(int k,
                            float *inv);
/* In place on an interleaved 3-channel w*h buffer. Falls back to
   sf_blur_plane3_fast below SF_WIDE_BLUR_MIN_SIGMA. `plane` is a w*h
   scratch as for sf_blur_plane3_fast. */
void sf_blur_plane3_wide(float *buf,
                         int w,
                         int h,
                         int roi_x,
                         int roi_y,
                         float sigma,
                         float *plane);

/* ---- optional host loop parallelism --------------------------------------
 * The spatial stages below (the Gaussian blur's row/transpose passes and the
 * wide-blur down/up passes) iterate over independent rows or flat indices.
 * When the engine is built without OpenMP (the MSVC .8bf build, which must not
 * import vcomp140.dll), a host installs `runner` to evaluate fn(ctx, i) for
 * i in [0, n) over several threads. Iterations are independent and write to
 * disjoint regions, so any partition is bit-identical to the serial loop.
 * Install with sf_set_loop_runner(); pass NULL (the default) to run serially. */
typedef void (*sf_loop_fn)(void *ctx, size_t i);
typedef void (*sf_loop_runner)(sf_loop_fn fn, void *ctx, size_t n);
void sf_set_loop_runner(sf_loop_runner runner);
/* Same exact-kernel blur as sf_blur_plane3, but operating directly on a
   single flat w*h buffer (no 3-channel interleave) -- used for the
   per-sublayer dye-cloud blur inside grain generation, where each
   (channel, sub-layer) has its own sigma and needs its own buffer; one interleaved 3-channel pass
   will not do. No lower sigma cutoff
   (unlike sf_blur_plane3's 0.3px guard for the visible clump blur): the
   dye-cloud sigma is often well under a pixel and still meaningfully
   softens the raw particle draw, matching upstream's plain `> 0` check. */
void sf_blur_plane1(float *buf,
                    int w,
                    int h,
                    float sigma,
                    float *plane,
                    float *trans);
/* Additive unsharp mask on the scanned RGB ([df] apply_unsharp_mask):
   out = D + amount * (D - blur(D)). `orig` and `work` are w*h*3 and w*h
   scratch buffers supplied by the caller. */
void sf_unsharp_mask3(float *buf,
                      int w,
                      int h,
                      float sigma,
                      float amount,
                      float *orig,
                      float *work);
/* Viewing-glare veil ([gl] add_glare): adds a blurred lognormal field of mean
   percent/100 (relative std `roughness`) to all three channels. `field` is a
   w*h scratch buffer; roi_x/roi_y are absolute image coordinates so the veil
   is stable under pan and zoom. */
void sf_glare(float *rgb,
              int w,
              int h,
              float percent,
              float roughness,
              float blur,
              int roi_x,
              int roi_y,
              float *field);

void sf_multiplicative_unsharp_mask3(float *buf,
                                     int w,
                                     int h,
                                     float sigma,
                                     float amount,
                                     const float *floor_d,
                                     float *orig,
                                     float *work);
/* Two independently-controllable stages, matching upstream's HalationParams:
 *   scatter_amount / scatter_scale   -- stage 1, in-emulsion core+tail scatter
 *   halation_amount / halation_scale -- stage 2, back-reflection multi-bounce
 * halation_strength: per-channel (R,G,B) back-reflection strength at
 * halation_amount==1.0; halation_first_sigma_um: per-channel first-bounce
 * Gaussian radius in micrometres. Both come from sf_sim_halation_params() —
 * per-film when the pack provides film_render_defaults[stock].halation,
 * otherwise the generic still/strong-antihalation baseline. */
/* `sc_core` / `sc_tail` / `w_s` are the per-channel in-emulsion scatter PSF from
   sf_sim_scatter_params(): Gaussian core radius and exponential tail decay in
   micrometres on film, and the core/tail mix weight. Already collapsed to one
   value per channel for a single-emulsion stock, and already clamped by the
   caller to what the ROI padding covers. */
void sf_halation(float *raw,
                 int w,
                 int h,
                 double pixel_um,
                 const double sc_core[3],
                 const double sc_tail[3],
                 const double w_s[3],
                 float scatter_amount,
                 float scatter_scale,
                 float halation_amount,
                 float halation_scale,
                 const double halation_strength[3],
                 const double halation_first_sigma_um[3]);
/* Stops of headroom above the protect threshold over which the boost is spread.
   The reference spreads it from the threshold up to the frame's own peak; that
   peak is a whole-image reduction, which a per-ROI/per-tile pixelpipe cannot
   reproduce consistently. A fixed span keeps the curve identical in every tile
   and in both pipes, and puts both controls on the same footing -- protect_ev
   sets where the boost starts, this sets how far above that it reaches full
   strength. 4 EV matches the reference's typical frame peak for a normally
   exposed scene (midgray + 4-6 EV). */
#define SF_BOOST_SPAN_EV 4.0f

/* Highlight boost curve constants, shared by the CPU loop and the GPU kernel so
   both evaluate the same numbers. Below raw_x0 the curve is the identity; from
   raw_x0 up to xmax it is x + scale*(e^(a*dx) - a*dx - 1) with
   dx = (x - raw_x0)*inv_max; above xmax it continues along its tangent at xmax,
   x + ext + slope*(x - xmax), so a highlight brighter than the span gets a
   bounded, C1-continuous lift instead of an exponential one. a_log2e is
   a*log2(e), for evaluating e^(a*dx) as grain_exp2f(a_log2e*dx). */
typedef struct sf_boost_plan_t
{
  float raw_x0, xmax, inv_max;
  float a, a_log2e, scale;
  float ext, slope;
} sf_boost_plan_t;

/* Fill `plan`; returns 0 when the boost is a no-op. */
int sf_boost_build_plan(float boost_ev,
                        float boost_range,
                        float protect_ev,
                        sf_boost_plan_t *plan);

void sf_boost_highlights(float *raw,
                         int w,
                         int h,
                         float boost_ev,
                         float boost_range,
                         float protect_ev);
void sf_diffusion_filter(float *raw,
                         int w,
                         int h,
                         double pixel_um,
                         int family,
                         float strength,
                         float spatial_scale,
                         float halo_warmth,
                         float core_intensity,
                         float core_size,
                         float halo_intensity,
                         float halo_size,
                         float bloom_intensity,
                         float bloom_size);

/* Diffusion-filter Gaussian bank, built host-side and consumed by the GPU path
   (the CPU path builds it internally). Each entry is one Gaussian blur of the
   linear plane, with a per-channel weight; the scattered image is their sum, and
   the final mix is (1-p_s)*in + p_s*scatter. */
#define SF_DIFFUSION_MAX_BANK 11  /* core(2) + halo(3) + bloom(4) + margin */
typedef struct sf_diffusion_plan_t
{
  int n;
  int nc, nh, nb;                              /* number of Gaussian components */
  float sigma_um[SF_DIFFUSION_MAX_BANK];   /* blur sigma in micrometres (×scale/pixel = px) */
  float wr[SF_DIFFUSION_MAX_BANK];    /* per-channel weight (already ×group weight) */
  float wg[SF_DIFFUSION_MAX_BANK];
  float wb[SF_DIFFUSION_MAX_BANK];
  float p_s;                          /* scatter fraction */
} sf_diffusion_plan_t;

/* Fill `plan` for the given strength/warmth. Returns 0 and sets plan->p_s=0 when
   the filter is a no-op. spatial_scale/pixel are applied by the caller (sigma_px
   = sigma_um * spatial_scale / pixel_um). */
int sf_diffusion_build_plan(int family,
                            float strength,
                            float halo_warmth,
                            sf_diffusion_plan_t *plan);

/* The widest Gaussian that family builds, in micrometres, before the caller's
   own size and scale multipliers: max(halo, bloom) x sqrt(3), the same
   moment-matched sigma build_plan() puts in plan->sigma_um. A host that wants
   to say how wide a look is -- a preview cost note, a warning -- asks this
   instead of re-deriving the bank from the family tables. 0 for a family that
   does not exist. */
double sf_diffusion_widest_sigma_um(int family);


/* Maximum kernel half-width (taps = 2*radius+1) passed to
   dt_gaussian_kernel_1d.
   Caps cost for pathologically large sigma (very high film_format_mm
   combined with very low resolution); every physically-plausible sigma this
   module uses stays far under this. Shared by spektra_core.c's CPU direct
   convolution and spektrafilm.c's GPU host-side weight upload, so both
   dispatch the identical kernel for a given sigma. */
#define SF_GAUSS_MAX_RADIUS 512

/* Sigma at which the direct kernel hands over to the recursive one. This is
   the reference's own crossover (SMALL_SIGMA_MAX in fast_gaussian_filter.py),
   and above it both sides run the same Young-van Vliet filter, so a given
   sigma produces the same blur here, on the GPU, and in the app. */
#define SF_GAUSS_EXACT_MAX_SIGMA 3.0f
/* Below this the blur is skipped outright rather than run with a degenerate
   kernel: dt_gaussian_kernel_1d() floors its radius at 1, so a sigma of 0.1
   still produces a real 3-tap kernel with non-negligible side weights, not
   an identity. sf_blur_plane3/_fast apply this cut themselves; the constant is
   shared so spektrafilm.c's OpenCL path can apply it at the same call sites
   instead of blurring where the CPU does nothing. */
#define SF_GAUSS_MIN_SIGMA 0.3f

/* Young-van Vliet order-3 recursive Gaussian coefficients (B, B1, B2, B3),
   identical to the reference's _yvv_coeffs. Exported so the GPU host side can
   build the same filter the CPU runs. */
/* Widest sigma the recursive filter is asked for. Above this its float32
   coefficients stop describing the filter we want: B falls towards 1e-7 while
   B2 and B3 stay near -3 and 1, and their rounding moves the poles. The
   incremental evaluation in _sf_gauss_iir_1d keeps the DC gain at exactly 1
   whatever the coefficients round to, but the width still drifts. Measured
   effective vs requested sigma, single pass, float32, impulse response:

       requested   100    120    135    150    200    400    700
       effective   100    116    128    138    167    214    159

   -- so it tracks to ~120, is about 8% narrow at 150, and the width no longer
   follows the request past ~200, which is where cinebloom and pro-mist land at
   export resolution (their bloom reaches 2500 um and 1625 um, ~1000 px on a
   6000 px frame at 26 mm).

   A wider blur is therefore run as a cascade of n passes of sigma/sqrt(n),
   each inside this limit: Gaussians compose by adding variances, so the
   cascade delivers the requested sigma at every resolution instead of a
   pixel-capped one that would make preview, 1:1 and export disagree. */
#define SF_GAUSS_MAX_IIR_SIGMA 150.0f
void sf_gauss_yvv_coeffs(float sigma,
                         float out[4]);

/* Upper bound on the cascade above, so its cost is bounded too. n passes cost
   n sweeps, and n = (sigma / SF_GAUSS_MAX_IIR_SIGMA)^2, so the count that
   sf_gauss_iir_passes() derives passes INT_MAX at sigma ~ 6.95e6 px: the int
   cast is undefined there and in practice yields a negative count, which makes
   the pass loop run zero times and the stage return the unblurred plane
   without a word. Saturating in float before the cast keeps the count in range
   and the stage running.

   The value is the smallest power of four (so sqrt(n) is exact in float) that
   leaves every radius the parameter surface can ask for below saturation:

       halation at its ceiling   1000 um x scale 4 / 0.5 um px, 3rd bounce  13,856 px  ->     8,534 passes
       cinebloom bloom at 0.5 um px (lambda 1000 um x spread 2.5 x sqrt 3)   8,660 px  ->     3,334 passes
       the same with bloom size 4 and scale 4                               138,564 px  ->   853,335 passes

   So only the last, which pushes the look multipliers 16x past the widest
   radius any stage physically models (4,330 um), is clamped, and it is clamped
   from a blur 23x wider than a 6000 px frame to one 6x wider: with the
   edge-replicating recursion both are a flat field, and the passes saved are
   13x. A hand-made params block asking for 1e9 px, which today silently
   renders nothing, gets the same flat field instead.

   Saturation point: 150 * sqrt(65536) = 38,400 px, i.e. 19,200 um at the finest
   pitch the surface allows (0.5 um) and 4.4x the widest physical radius above.
   Beyond it the delivered sigma stops following the request. Both previews stay
   behind the full render there, so they cannot show a clamp the render does not
   have: the 1:1 crop renders at native scale with the film's own pitch, so its
   sigma in pixels is the render's, and the whole-image fit renders at rs <= 1
   of it while dividing pixel_um by rs to keep the blur's relative footprint,
   which leaves its sigma in pixels at rs times the render's. Both are flat
   fields past the cap, which is the only reason that is acceptable.

   This is a totality bound, not the cost ceiling: the ceiling that keeps a
   render finishable is the parameter surface (SP_HALATION_FIRST_SIGMA_UM in
   sim_port/sp_chain.h and the scale ranges in the hosts' params.h). */
#define SF_GAUSS_MAX_IIR_PASSES 65536

/* Number of recursive passes for `sigma`, and the per-pass sigma in
   *pass_sigma. 1 and sigma itself up to SF_GAUSS_MAX_IIR_SIGMA. Above that the
   cascade is capped at SF_GAUSS_MAX_IIR_PASSES, and *pass_sigma is clamped to
   SF_GAUSS_MAX_IIR_SIGMA to say so: the delivered sigma is then
   sqrt(passes) * SF_GAUSS_MAX_IIR_SIGMA rather than the request. Shared by the
   CPU blur and the GPU host so both run the same cascade. */
int sf_gauss_iir_passes(float sigma,
                        float *pass_sigma);

/* Build a normalized, truncated 1D Gaussian kernel. truncate = 3 sigma with
 * radius = int(3*sigma + 0.5), matching the reference's own
 * _gaussian_kernel_1d default, not scipy's truncate = 4 -- the
 * reference never calls scipy for this. `kernel` must have room for
 * 2*max_radius+1 taps; returns the radius actually used. Exported so both
 * the CPU convolution (spektra_core.c) and the GPU host-side weight upload
 * (spektrafilm.c's process_cl) build the identical kernel for a given sigma. */

/* grain_clampf, the grain hash (grain_hash / grain_uniform / grain_normal / grain_pixel_seed), the
   portable exp/log polynomials (grain_exp2i / grain_exp_neg / grain_exp2f /
   grain_log2f) and the grain sampler (grain_poisson / grain_layer_particle, plus
   GRAIN_POISSON_EXACT_MAX) now live in data/kernels/grain.h, included
   at the top of this header: the OpenCL kernel compiles that same file, so
   the two paths cannot drift. Read the rationale for every constant there. */

/* SF_GRAIN_REF_UM: the fixed reference scale (spektrafilm's own
   pixel_size_um=10) the particle model is generated at, independent of the
   live pipe's pixel_um — this keeps grain CHARACTER constant across zoom.
   Callers that turn the generated delta into visible clump STRUCTURE (the
   blur step) must still convert this reference into real pixels via the
   pipe's own pixel_um, or clump SIZE silently stops scaling with output
   resolution — see the grain blur in spektrafilm.c/.cl and
   _max_halo_sigma's ROI padding, all of which must agree. */
#define SF_GRAIN_REF_UM 10.0f

#ifdef __cplusplus
}
#endif
