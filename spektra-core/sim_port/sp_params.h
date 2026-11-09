/* SPDX-License-Identifier: GPL-3.0-or-later */
/* sp_params — shared flat params-JSON -> sp_chain_params_t parse.
 *
 * Used identically by the .8bf adapter (the host's injected params object)
 * and the Linux reference renderer (a params.json file). Keys are the shared
 * flat key set; `_image` (host geometry) and anything unknown are skipped.
 */
#pragma once

#include <stdint.h>

#include "sp_chain.h"

/* Parses `params` (len bytes of JSON) into `out` (pre-initialised defaults
 * are overwritten for keys present). Returns 0 on success. */
int sp_params_parse(const char *params, int len, sp_chain_params_t *out);

/* Fills `out` with the defaults shared by the plugin and refrender (film
 * kodak_ektar_100 / paper kodak_ektacolor_edge / negative, lut_steps 17). */
void sp_params_defaults(sp_chain_params_t *out);