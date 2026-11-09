/* SPDX-License-Identifier: GPL-3.0-or-later */
/* sf_gui.h — parameter-dialog seam and shared document-profile detection.
 *
 * The dialog is Dear ImGui + Direct3D 11 (src/sf_gui_imgui.cpp). The original
 * Win32 dialog (dialog.c) was retired in phase B6, once ImGui was signed off in
 * Affinity, so there is now a single backend. */
#pragma once

#include "pifilter.h"
#include "params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Document ICC profile resolved once per dialog, for the auto-detect read-out
 * and the live preview (the render's start path resolves the same profile).
 * `ok` is 0 when no profile was supplied or it could not be parsed. */
typedef struct sf_detect_result
{
  int    ok;
  int    transfer;
  double matrix[9];
  double white_xy[2];
  char   name[96];
} sf_detect_result;

/* Read the document profile (working space first, then the image profile) and
 * resolve it into the matrix/white/transfer the engine consumes, plus a
 * human-readable name. `fr` and `out` may be NULL (then *out is cleared). */
void sf_detect_document_profile(FilterRecordPtr fr, sf_detect_result *out);

/* ImGui + Direct3D 11 backend (sf_gui_imgui.cpp). Returns 1=OK, 0=Cancel,
   <0 on setup failure (e.g. D3D11/WARP init failure). */
short sf_run_dialog_imgui(void *owner, FilterRecordPtr fr,
                          sf_params_block *block,
                          const float *src, int sw, int sh);

/* Show the modal parameter dialog. `owner` is an HWND (typed void* to keep this
 * header free of windows.h). `fr` is the live FilterRecord, read to name the
 * document ICC profile for the auto-detect read-out. `src` is an optional
 * interleaved float RGB image (sw x sh) to preview. Returns 1 on OK, 0 on
 * cancel (also 0 if the GUI cannot start). */
short sf_run_params_dialog(void *owner, FilterRecordPtr fr,
                           sf_params_block *block,
                           const float *src, int sw, int sh);

#ifdef __cplusplus
}
#endif
