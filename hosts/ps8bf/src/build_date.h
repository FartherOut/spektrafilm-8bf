/* SPDX-License-Identifier: GPL-3.0-or-later */
/* build_date.h — the build date as both About surfaces show it.
 *
 * __DATE__ is always "Mmm dd yyyy" (US month-name order), which reads oddly in a
 * dialog and cannot be sorted or grepped. Both About surfaces — the ImGui dialog
 * and the host's plug-in About selector — therefore show only the build date, as
 * EN ISO 8601 (YYYY-MM-DD). The diagnostic log keeps the full SF_BUILD_STAMP
 * (date + time), because there the point is telling a loaded DLL from a stale
 * copy on disk.
 *
 * Shared here rather than kept in the dialog's translation unit because the
 * dialog is C++ and the About selector is C. */
#pragma once

#include <stdio.h>
#include <string.h>

static inline const char *sf_build_date_iso(void)
{
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char *d = __DATE__;                 /* e.g. "Nov  4 2026" */
  char mon3[4] = { d[0], d[1], d[2], '\0' };
  const char *m = strstr(months, mon3);
  int day  = (d[4] == ' ') ? (d[5] - '0')
                           : (d[4] - '0') * 10 + (d[5] - '0');
  int mon  = m ? (int)(m - months) / 3 + 1 : 1;
  int year = (d[7] - '0') * 1000 + (d[8] - '0') * 100
           + (d[9] - '0') * 10 + (d[10] - '0');
  static char buf[32];
  snprintf(buf, sizeof buf, "%04d-%02d-%02d", year, mon, day);
  return buf;
}
