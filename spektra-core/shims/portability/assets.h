/* SPDX-License-Identifier: GPL-3.0-or-later */
/* sf_asset — read-only embedded asset store for the plugin builds.
 *
 * The engine loads a data pack through fopen/fread; the host adapter supplies
 * a lookup table (see stdio_asset.c) so those opens resolve against embedded
 * bytes instead of the filesystem. tools/gen_assets.py generates the table
 * from a data-pack directory. On the Linux host builds the same engine code
 * reads real files instead.
 */
#pragma once

#include <stddef.h>

typedef struct sf_asset_t
{
  const char *path;         /* normalised relative path, e.g. "pack/pack.json" */
  const unsigned char *data;
  size_t len;
} sf_asset_t;

extern const sf_asset_t sf_assets[];
extern const int sf_assets_n;

/* Exact-match lookup on a normalised path ("pack/pack.json"). Returns the
 * data pointer and length, or NULL when the path is not embedded. */
const unsigned char *sf_asset_lookup(const char *path, size_t *len);