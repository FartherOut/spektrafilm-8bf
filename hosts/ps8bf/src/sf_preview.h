/* SPDX-License-Identifier: GPL-3.0-or-later */
/* sf_preview.h — the parameter dialog's preview.
 *
 * The modal dialog used to run from filterSelectorParameters, before the host
 * had handed over any pixels, so it could only show a built-in chart. It now
 * runs from filterSelectorContinue (via the hook installed by plugin_main and
 * driven by filter_loop.c), after the selection has been gathered, so it can
 * preview the user's own image through the *same* engine and params as the
 * final render. No Win32 here; the control wiring lives in the GUI layer
 * (sf_gui_imgui.cpp).
 */
#pragma once

#include <stddef.h>

#include "params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Render the built-in proxy with *b at w x h. On success returns a malloc'd
 * packed RGB (w*h*3, top-down, row-major) buffer to free(). On failure returns
 * NULL and, when err is non-NULL, sets *err to a free()-able message (or NULL).
 * Used as the fallback when no image pixels are available. */
unsigned char *sf_preview_render(const sf_params_block *b, int w, int h,
                                 char **err);

/* True 1:1 preview: renders the out_w x out_h region of the source (src,
 * sw*sh*3 floats, RGB, top-down) at native pixel scale through the chain built
 * from *b, so every preview pixel matches the full-resolution render at 100%
 * zoom. The region is centred on the pan point (see sf_preview_set_pan) and
 * clamped to the source; when the image is smaller than the pane the result is
 * blitted centred with a neutral dark bar. The source buffer is never
 * modified. Returns malloc'd packed RGB (out_w*out_h*3) to free(), or NULL on
 * failure (setting *err when non-NULL, if available). */
unsigned char *sf_preview_render_image(const float *src, int sw, int sh,
                                       const sf_params_block *b, int out_w,
                                       int out_h, char **err);

/* Pan the 1:1 crop's centre, as fractions of the available slack in [-1, 1]
 * (0,0 = image centre, ±1 = flush against an edge). Applied by
 * sf_preview_render_image(); the fit preview always shows the whole frame. */
void sf_preview_set_pan(double fx, double fy);
double sf_preview_pan_x(void);
double sf_preview_pan_y(void);

/* Convert the chain's space-preserving samples (encoded in the document's own
 * primaries/white/transfer) to display-ready sRGB, in place — the transform a
 * colour-managed viewer would apply before showing the document. A no-op when
 * the document is already sRGB with the sRGB curve (the historical default), so
 * the common preview stays byte-identical. Exposed for the harness, which
 * asserts the transform is neutral-preserving. */
void sf_preview_display(float *rgb, size_t npix, const sf_params_block *b);

/* "Whole image" preview: the entire frame, supersampled. The source is
 * box-downsampled to up to 2x the display size (never below native), pushed
 * through the chain, and the *rendered result* is shrunk to fit out_w x out_h.
 * Because the spatial stages see the supersampled image — not a pre-shrunk
 * thumbnail — halation sources, fine texture and grain survive and the result
 * closely matches the full-resolution render. The pixel pitch passed to the
 * engine is scaled by the render factor, so spatial stages keep the same
 * *relative* size as the full-resolution render. The source buffer is never
 * modified. Returns malloc'd packed RGB (out_w*out_h*3) to free(), or NULL. */
unsigned char *sf_preview_render_fit(const float *src, int sw, int sh,
                                     const sf_params_block *b, int out_w,
                                     int out_h, char **err);

#ifdef __cplusplus
}
#endif
