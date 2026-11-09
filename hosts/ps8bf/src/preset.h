/* SPDX-License-Identifier: GPL-3.0-or-later */
/* preset.h — parameter presets: a named sf_params_block stored as portable JSON.
 *
 * Presets are the dialog's "Presets" group (Phase 4): one look, saved under a
 * name, that can be re-applied later or copied out as text and pasted back in.
 *
 * Two properties matter, and the harness tests both:
 *
 *  - Round-trip exactness. Encode a block, decode it again, and you get a
 *    bit-identical block — loading a preset never nudges a value, so the
 *    render stays byte-identical to what the dialog showed.
 *  - Portability. Film/paper are stored as pack stems ("film":
 *    "kodak_portra_400") rather than indices, so a preset survives the pack
 *    being reordered or extended; keys this build does not know are ignored,
 *    so a preset written by a newer build still loads.
 *
 * The codec is pure — no disk, no Win32 — so the Linux harness can exercise
 * it. File and clipboard I/O live in the GUI layer (sf_gui_imgui.cpp).
 */
#pragma once

#include <stddef.h>

#include "params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Identity written into, and required by, every preset. */
#define SF_PRESET_FORMAT  "spektrafilm-preset"
#define SF_PRESET_VERSION 1

/* Ceiling on a preset's JSON text: ~150 scalars plus a name fit in a few KB,
 * so 16 KB is generous and also caps how much a clipboard paste will read. */
#define SF_PRESET_JSON_MAX 16384

/* Encode `b` as preset JSON into `buf` (NUL-terminated on success). `name` is
 * the human label written as "name"; NULL is written as "".
 *
 * Returns the number of bytes written (excluding the NUL), or -1 if the
 * buffer is too small.
 *
 * Deliberately not written: the block header (magic/version), the document-
 * derived input matrix, the host-metered auto-exposure EV and the trailing
 * padding. Those describe a document or a frame, not a look. */
int sf_preset_write(const sf_params_block *b, const char *name,
                    char *buf, size_t cap);

/* Decode `len` bytes of preset JSON into `*b`.
 *
 * `*b` is replaced wholesale: keys absent from the JSON take the engine
 * default, because the decode starts from sf_params_init. A partial or old
 * preset therefore always yields one deterministic look instead of a mix with
 * whatever was in the block before. `*b` is left untouched when the input is
 * rejected.
 *
 * Returns 0 on success, or -1 if the input is not a preset this build accepts
 * (not JSON, missing/foreign "format", out-of-range "version", malformed
 * structure, or a value that cannot be represented). */
int sf_preset_read(const char *json, int len, sf_params_block *b);

#ifdef __cplusplus
}
#endif
