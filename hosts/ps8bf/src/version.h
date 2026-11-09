/* SPDX-License-Identifier: GPL-3.0-or-later */
/* version.h — product identity + attribution shown in the About box and the
 * VERSIONINFO resource. Keep in sync with res/resource.rc and the README. */
#pragma once

/* The name is lowercase because the upstream project this one continues
 * describes its own name that way ("spektrafilm"), and the GUI should show the
 * name the way the project does. */
#define SF_PRODUCT_NAME "spektrafilm"
#define SF_VERSION "0.1.0"

/* Build identity, shown in the About box and written to the diagnostic log so a
 * loaded DLL can be told apart from a stale copy on the host's disk. Override
 * with -DSF_BUILD_STAMP=... for a reproducible shipping build. */
#ifndef SF_BUILD_STAMP
#define SF_BUILD_STAMP __DATE__ " " __TIME__
#endif
#define SF_COMPANY "spektrafilm"
#define SF_COPYRIGHT "Engine GPL-3.0-or-later; data pack CC BY-SA 4.0"