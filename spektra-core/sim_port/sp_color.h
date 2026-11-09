/* SPDX-License-Identifier: GPL-3.0-or-later */
/* sp_color — input colour primaries and transfer functions (the "Colour
 * Management" surface), split into two independent controls:
 *
 *   Input colour space ->  primaries (an RGB->XYZ matrix + its white point)
 *   Input Transfer     :  the decode curve (encoded sample -> scene-linear),
 *                         inverted on the way out.
 *
 * The OFX plugin exposes one 26-entry menu that pairs a primaries matrix with a
 * transfer curve (e.g. "Sony S-Log3 / S-Gamut3"). The port splits the two so a
 * host can pair any gamut with any curve; the tables below cover the same set.
 *
 * Index 0 of each table is the default — sRGB primaries, sRGB curve — so a
 * params block that leaves both selections at 0 renders byte-for-byte as the
 * Phase-1 slice. The indices are persisted (sp_params / the .8bf block), so new
 * entries are appended, never reordered.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Input transfer (decode curve): host sample -> scene-linear, with its inverse
 * applied on the way out. Keep SP_TRANSFER_SRGB == 0. */
typedef enum
{
  SP_TRANSFER_SRGB = 0,       /* IEC 61966-2-1 (default) */
  SP_TRANSFER_LINEAR = 1,     /* no transfer (already scene-linear) */
  SP_TRANSFER_G22 = 2,        /* pure 2.2 gamma */
  SP_TRANSFER_PROPHOTO = 3,   /* ROMM RGB (1.8 gamma with a linear toe) */
  SP_TRANSFER_G24 = 4,        /* pure 2.4 gamma */
  SP_TRANSFER_G26 = 5,        /* pure 2.6 gamma */
  SP_TRANSFER_LOGC4 = 6,      /* ARRI LogC4 */
  SP_TRANSFER_LOGC3 = 7,      /* ARRI LogC3 (EI800) */
  SP_TRANSFER_BMDFILM = 8,    /* Blackmagic Film Gen 5 */
  SP_TRANSFER_DAVINCI = 9,    /* DaVinci Intermediate */
  SP_TRANSFER_LOG3G10 = 10,   /* RED Log3G10 (v3) */
  SP_TRANSFER_SLOG3 = 11,     /* Sony S-Log3 */
  SP_TRANSFER_CANONLOG2 = 12, /* Canon Log 2 (v1.2) */
  SP_TRANSFER_CANONLOG3 = 13, /* Canon Log 3 (v1.2) */
  SP_TRANSFER_VLOG = 14,      /* Panasonic V-Log */
  SP_TRANSFER_ACES_CCT = 15,  /* ACEScct */
  SP_TRANSFER_ACES_CC = 16,   /* ACEScc */
  SP_TRANSFER_COUNT = 17
} sp_input_transfer_t;

int sp_transfer_count(void);
const char *sp_transfer_label(int index);
/* Decode a host sample to scene-linear, and encode scene-linear back. Both are
 * monotonic and inverse to within float precision; out-of-range indices fall
 * back to sRGB. */
float sp_transfer_decode(int index, float v);
float sp_transfer_encode(int index, float v);

/* Input primaries (gamut). rgb_to_xyz is row-major, XYZ = M * RGB, computed
 * under the space's own white point; the engine then CAT16-adapts it to the
 * table illuminant. Keep SP_COLOR_SPACE_SRGB == 0. */
int sp_color_space_count(void);
const char *sp_color_space_label(int index);
void sp_color_space_matrix(int index, double rgb_to_xyz[9], double white_xy[2]);

#ifdef __cplusplus
}
#endif
