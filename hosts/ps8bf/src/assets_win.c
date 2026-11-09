/* SPDX-License-Identifier: GPL-3.0-or-later */
/* assets_win.c — sf_asset_lookup() backed by Win32 RCDATA resources.
 *
 * Pairs with the portable stdio_asset.c shim (linked on Windows too): the
 * engine's g_fopen -> fopen resolves against sf_asset_lookup(), which here
 * walks the generated resource table (asset_ids.h) and returns the locked
 * resource bytes. Read-only, zero heap.
 */
#include <windows.h>
#include <string.h>

#include "assets.h"
#include "asset_ids.h"

static HINSTANCE g_hinst = NULL;

void sf_asset_set_hinstance(HINSTANCE hinst) { g_hinst = hinst; }

/* On Windows the resource table (SF_ASSET_RES) is the source of truth, so the
 * public table is empty. stdio_asset.c only ever calls sf_asset_lookup(). */
const sf_asset_t sf_assets[1] = { { NULL, NULL, 0 } };
const int sf_assets_n = 0;

const unsigned char *sf_asset_lookup(const char *path, size_t *len)
{
  int i;
  if(len) *len = 0;
  if(!g_hinst || !path) return NULL;

  for(i = 0; i < SF_ASSET_RES_N; i++)
  {
    if(strcmp(path, SF_ASSET_RES[i].path) == 0)
    {
      HRSRC hr = FindResourceA(g_hinst, MAKEINTRESOURCEA(SF_ASSET_RES[i].id),
                               RT_RCDATA);
      HGLOBAL hg;
      void *p;
      DWORD sz;
      if(!hr) return NULL;
      hg = LoadResource(g_hinst, hr);
      if(!hg) return NULL;
      p = LockResource(hg);
      if(!p) return NULL;
      sz = SizeofResource(g_hinst, hr);
      if(len) *len = (size_t)sz;
      return (const unsigned char *)p;
    }
  }
  return NULL;
}