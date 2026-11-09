/* SPDX-License-Identifier: GPL-3.0-or-later */
/* filter_loop.h — the 8BF selector bodies that move pixels.
 *
 * Kept free of any Win32 dependency (only pifilter.h + the engine) so the
 * Linux mock harness can drive the exact same code the DLL runs. plugin_main
 * owns the selectors; these are the start/continue/finish bodies.
 */
#pragma once

#include "pifilter.h"
#include "params.h"

/* Optional dialog callback (installed by the Win32 host; left NULL by the Linux
 * mock harness). It is invoked exactly once per invocation, from
 * filterSelectorContinue, *after* the selection has been decoded to float and
 * *before* it is rendered, so the dialog can preview the real image.
 *
 *   ctx  - opaque pointer supplied to sf_filter_set_dialog_hook
 *   fr   - the live FilterRecord (to store settings back into the handle)
 *   b    - the current settings (edited in place; used for the final render)
 *   src  - w*h*3 interleaved float RGB, top-down (the frame about to render)
 *
 * Returns nonzero to proceed with the render, 0 if the user cancelled. */
typedef int (*sf_dialog_hook)(void *ctx, FilterRecordPtr fr, sf_params_block *b,
                              const float *src, int w, int h);

void sf_filter_set_dialog_hook(sf_dialog_hook fn, void *ctx);

/* Mark this invocation as interactive (sent by filterSelectorParameters). The
 * dialog hook only runs for interactive invocations, so a "Last Filter" replay
 * — which skips Parameters — applies the stored settings silently. */
void sf_filter_mark_interactive(void);

/* filterSelectorStart body. Builds the chain from FilterRecord::parameters
 * and requests the whole selection as a single tile. Returns noErr,
 * filterBadMode/filterBadParameters, or a nonzero error. */
OSErr sf_filter_start(FilterRecordPtr fr);

/* filterSelectorContinue body. Processes the requested tile, then empties the
 * rects. May return userCanceledErr. */
OSErr sf_filter_continue(FilterRecordPtr fr);

/* filterSelectorFinish body (always called after a successful Start). */
OSErr sf_filter_finish(FilterRecordPtr fr);

/* Drop any half-built state (module unload / defensive). */
void sf_filter_reset(void);

/* The selection bounds a host is asking us to process: the 32-bit
 * filterRect32 when the host filled it, else the legacy int16 filterRect.
 * Shared with plugin_main's Prepare so the buffer request matches whichever
 * rect family the host uses. */
void sf_filter_selection_rect(FilterRecordPtr fr, VRect *r);

/* Diagnostic transcript target (see filter_loop.c). NULL disables it; the
 * Linux mock harness leaves it unset. */
void sf_filter_set_log_path(const char *path);

/* Diagnostic transcript helpers. begin/ensure open the file for append (no-op
 * when no path was set, as under the Linux harness); write takes printf-style
 * arguments. plugin_main records every selector so merely invoking the filter
 * produces a file identifying the loaded build. */
void sf_filter_log_begin(void);
void sf_filter_log_write(const char *fmt, ...);
void sf_filter_log_end(void);

/* Human-readable text for the last start/continue failure (static storage). */
const char *sf_filter_last_error(void);