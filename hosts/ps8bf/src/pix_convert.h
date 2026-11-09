/* SPDX-License-Identifier: GPL-3.0-or-later */
/* pix_convert.h — host sample <-> float conversion for the .8bf filter loop.
 *
 * The engine (sp_chain / spektra_sim) works on interleaved f32 RGB, channels
 * 0..2, values read as scene-linear. The host hands us 8- or 16-bit samples.
 *
 * These functions do a plain scale (v / 255 or v / SF_SAMPLE_MAX_16) — a
 * normalised code value, not linear light. The encode/decode to linear happens
 * once, inside sp_chain_render, selected by sp_chain_params_t.input_transfer
 * (sRGB by default). Keeping it there means the plugin, the live preview and
 * the Linux reference all share the same transfer, so the parity oracle holds.
 * The scale has to be written as a division for that oracle to hold: scaling
 * 8-bit input by the reciprocal (1.0f / 255.0f) is 1 ulp off for half the
 * codes, which the engine amplifies enough to move a rendered pixel by a
 * level.
 *
 * Output quantisation rounds half-up (refrender-identical). An ordered Bayer
 * dither is available under SF_QUANT_DITHER but is OFF by default so the
 * output stays byte-comparable to the reference.
 */
#pragma once

/* White level of a 16-bit host sample. Photoshop's "16-bit" data is 15+1 bits:
 * black 0, white 32768 — see the Adobe Photoshop SDK FAQ 3.3.1 ("Filters will
 * also see this range for 16 bit data") and the long-standing 0..32768
 * behaviour confirmed through current releases. Affinity's .8bf emulation
 * follows the same convention. Define SF_SAMPLE_MAX_16=65535 only if a host is
 * found to hand the filter full-range 16-bit samples. */
#ifndef SF_SAMPLE_MAX_16
#define SF_SAMPLE_MAX_16 32768.0f
#endif

/* Define to 1 to add an ordered 4x4 dither at the output quantise. */
#ifndef SF_QUANT_DITHER
#define SF_QUANT_DITHER 0
#endif

/* Decode a `w` x `h` tile of `planes`-interleaved `depth`-bit samples into
 * `out` (w*h*3 floats, RGB). Reads chan 0..2 from each pixel.
 * Returns 0, or non-zero on unsupported depth. */
int sf_pixels_to_float(const void *in, int in_row_bytes, int w, int h,
                       int planes, int depth, float *out);

/* Encode `in` (w*h*3 floats, RGB) into the tile, overwriting channels 0..2.
 * `row` is the tile's top row in image space (for the dither pattern). */
int sf_float_to_pixels(const float *in, void *out, int out_row_bytes, int w,
                       int h, int planes, int depth, int row);

/* Copy a whole in->out tile verbatim (all planes). Cheap pass-through so
 * alpha/extra planes survive the filter. Row bytes may differ. */
void sf_copy_tile(const void *in, void *out, int in_row_bytes, int out_row_bytes,
                  int w, int h, int planes, int depth);