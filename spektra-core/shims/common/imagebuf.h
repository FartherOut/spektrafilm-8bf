/* SPDX-License-Identifier: GPL-3.0-or-later */
/* spektra-core extraction spike — shim for darktable's "common/imagebuf.h". */
#pragma once

#include <stddef.h>

/* darktable copies an image buffer with dt_iop_image_copy(out, in, size)
 * where size is the number of FLOATS (dt's own "nfloats", see imagebuf.c). */
void dt_iop_image_copy(void *const restrict out,
                       const void *const restrict in,
                       const size_t size);