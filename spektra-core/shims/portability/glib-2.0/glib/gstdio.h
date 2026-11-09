/* SPDX-License-Identifier: GPL-3.0-or-later */
/* glib_mini — gstdio subset (only g_fopen is used by the engine). */
#pragma once

#include "../glib.h"

#include <stdio.h>

/* Opens a data file. On Linux this is real libc fopen; in the plugin build it
 * resolves against the embedded asset store (stdio_asset.c defines fopen). */
static inline FILE *g_fopen(const gchar *filename, const gchar *mode)
{
  return fopen(filename, mode);
}