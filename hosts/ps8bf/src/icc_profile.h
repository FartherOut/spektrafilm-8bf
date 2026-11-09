/* SPDX-License-Identifier: GPL-3.0-or-later */
/* icc_profile.h — a small, freestanding reader for RGB ICC profiles.
 *
 * Pulls the matrices (rXYZ/gXYZ/bXYZ) and TRC curve shape out of one of the
 * document profiles Photoshop hands us in FilterRecord and reduces them to a
 * plain (XYZ matrix, transfer id) pair that sf_params/sim_port can consume.
 * No external dependencies are used, so the .8bf can keep its minimal import
 * set. Returns 0 on success.
 */
#pragma once

#include "sp_color.h"

#ifdef __cplusplus
extern "C" {
#endif

int sf_icc_parse_profile(const void *data, int size,
                         double matrix[9], double white_xy[2],
                         int *transfer_index);

/* Pull a human-readable profile description out of the `desc` tag (ICC v2
 * textDescriptionType, or the v4 multiLocalizedUnicodeType) so the UI can name
 * the auto-detected document profile. `out` is NUL-terminated; returns 0 on
 * success, -1 when the profile carries no usable description. */
int sf_icc_profile_name(const void *data, int size, char *out, int outlen);

#ifdef __cplusplus
}
#endif
