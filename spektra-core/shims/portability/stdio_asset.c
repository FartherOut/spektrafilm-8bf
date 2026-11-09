/* SPDX-License-Identifier: GPL-3.0-or-later */
/* stdio_asset — FILE replacement over the embedded asset store.
 *
 * The engine's only stdio needs are read-only opens of pack files (g_fopen)
 * and fread/fclose on them. In the plugin builds there is no host filesystem,
 * so these calls resolve against sf_asset_lookup() (see assets_win.c) and
 * become cheap pointer arithmetic over the embedded bytes.
 *
 * The entry points are named sf_asset_fopen/fread/... rather than shadowing the
 * CRT's own fopen/fread/... Two reasons:
 *   - MSVC's static /MT CRT already defines fopen (libucrt.lib) and the two
 *     strong definitions collide (LNK2005). GCC/Clang's CRT is a DLL import, so
 *     the mingw validation build did not catch this.
 *   - A global `fopen` in the plugin link also hijacked the *host* transcript
 *     log (filter_loop.c opens %TEMP%\\spektrafilm.log with fopen), silently
 *     disabling it.
 * The engine translation units have the CRT names mapped onto these symbols at
 * compile time (see CMakeLists.txt / build-mingw.sh); host TUs keep the real CRT.
 *
 * Compiled into the plugin links; the Linux host builds use real libc stdio.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"

/* stdio.h is deliberately not included: we define our own FILE shape below,
 * and including it would only add conflicting declarations. SEEK_* are the
 * standard POSIX values. */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

typedef struct sf_file_t
{
  const unsigned char *data;
  size_t len;
  size_t pos;
} sf_file_t;

/* The engine's FILE spells are mapped onto these sf_asset_* names at compile
 * time in the plugin builds (see the header comment). */
typedef sf_file_t FILE_f;
#define FILE FILE_f

/* diagnostic counter: total payload bytes served by fread (dev builds) */
size_t sf_asset_read_bytes_total = 0;
size_t sf_asset_read_bytes_peek(void) { return sf_asset_read_bytes_total; }

/* Normalise a path: strip leading '/' (and all but the first of a run),
 * collapse "//", and drop "./" segments. Paths built by g_build_filename
 * are simple, so this stays conservative: no ".." resolution (nobody builds
 * one and a trail of ".." just won't match). */
static const char *sf_asset_norm(const char *p, char out[256])
{
  if(!p) return NULL;
  size_t o = 0;
  size_t i = 0;
  while(p[i] == '/') i++;
  while(p[i])
  {
    if(p[i] == '/')
    {
      if(o > 0 && out[o - 1] != '/') out[o++] = '/';
      i++;
      continue;
    }
    if(p[i] == '.' && p[i + 1] == '/' )
    {
      i += 2;
      continue;
    }
    if(o >= 255) return NULL;
    out[o++] = p[i++];
  }
  while(o > 1 && out[o - 1] == '/') o--;
  out[o] = 0;
  return out;
}

FILE *sf_asset_fopen(const char *path, const char *mode)
{
  (void)mode; /* assets are read-only; "rb" is the only mode used */
  char norm[256];
  if(!sf_asset_norm(path, norm)) return NULL;
  size_t len = 0;
  const unsigned char *d = sf_asset_lookup(norm, &len);
  if(!d) return NULL;
  FILE *f = (FILE *)malloc(sizeof(FILE));
  if(!f) return NULL;
  f->data = d;
  f->len = len;
  f->pos = 0;
  return f;
}

size_t sf_asset_fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
  if(!stream || size == 0 || nmemb == 0) return 0;
  const size_t can = (stream->len - stream->pos) / size;
  const size_t take = can < nmemb ? can : nmemb;
  if(take) memcpy(ptr, stream->data + stream->pos, take * size);
  stream->pos += take * size;
  sf_asset_read_bytes_total += take * size;
  return take;
}

int sf_asset_fclose(FILE *stream)
{
  if(stream) free(stream);
  return 0;
}

int sf_asset_feof(FILE *stream)
{
  return stream ? (int)(stream->pos >= stream->len) : 1;
}

long sf_asset_ftell(FILE *stream)
{
  return stream ? (long)stream->pos : -1L;
}

int sf_asset_fseek(FILE *stream, long offset, int whence)
{
  if(!stream) return -1;
  long base = 0;
  if(whence == SEEK_SET) base = 0;
  else if(whence == SEEK_CUR) base = (long)stream->pos;
  else if(whence == SEEK_END) base = (long)stream->len;
  else return -1;
  const long np = base + offset;
  if(np < 0 || (unsigned long)np > stream->len) return -1;
  stream->pos = (size_t)np;
  return 0;
}