/* SPDX-License-Identifier: GPL-3.0-or-later */
/* sf_gui.c — the parameter-dialog seam, plus the document-profile detection the
 * dialog shares with the render's start path.
 *
 * The original Win32 dialog (dialog.c) was retired in phase B6: the only
 * backend now is Dear ImGui + Direct3D 11 (sf_gui_imgui.cpp). */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "sf_gui.h"
#include "icc_profile.h"
#include "sp_color.h"

/* Read the document ICC profile the host handed us (working space first, then
 * the image profile, matching the render's start path) and resolve it into the
 * matrix/white/transfer the engine consumes, plus a human-readable name. */
void sf_detect_document_profile(FilterRecordPtr fr, sf_detect_result *out)
{
  const void *pdata = NULL;
  int psize = 0;
  char **hh = NULL;

  if (!out) return;
  out->ok = 0;
  out->transfer = -1;
  out->name[0] = '\0';
  if (!fr) return;

  if (fr->iCCWorkingProfileSize > 0 && fr->iCCWorkingProfileData)
  {
    hh = (char **)fr->iCCWorkingProfileData;
    pdata = (const void *)(hh ? *hh : NULL);
    psize = (int)fr->iCCWorkingProfileSize;
  }
  else if (fr->iCCprofileSize > 0 && fr->iCCprofileData)
  {
    hh = (char **)fr->iCCprofileData;
    pdata = (const void *)(hh ? *hh : NULL);
    psize = (int)fr->iCCprofileSize;
  }
  if (!pdata || psize <= 0) return;

  if (sf_icc_parse_profile(pdata, psize, out->matrix, out->white_xy,
                           &out->transfer) != 0)
    return;
  out->ok = 1;
  if (out->transfer < 0 || out->transfer >= sp_transfer_count())
    out->transfer = SP_TRANSFER_SRGB;
  if (sf_icc_profile_name(pdata, psize, out->name, (int)sizeof out->name) != 0)
    snprintf(out->name, sizeof out->name, "document profile");
}

/* The single dialog seam (see sf_gui.h). */
short sf_run_params_dialog(void *owner, FilterRecordPtr fr,
                           sf_params_block *block,
                           const float *src, int sw, int sh)
{
  short r = sf_run_dialog_imgui(owner, fr, block, src, sw, sh);
  if (r >= 0)
    return r;

  /* The ImGui/D3D11 dialog could not start (no usable hardware or WARP
   * device). There is no native fallback any more, so report and cancel rather
   * than silently doing nothing. */
  MessageBoxA((HWND)owner,
              "The spektrafilm dialog needs a working Direct3D 11 device "
              "(hardware or the WARP software renderer). GPU initialisation "
              "failed, so the filter was not applied.",
              "spektrafilm", MB_OK | MB_ICONERROR);
  return 0;
}
