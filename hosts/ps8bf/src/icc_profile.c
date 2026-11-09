/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "icc_profile.h"

#include <math.h>
#include <string.h>

#include "sp_color.h"

static unsigned int u16(const unsigned char *p)
{
  return ((unsigned int)p[0] << 8) | p[1];
}

static unsigned int u32(const unsigned char *p)
{
  return ((unsigned int)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static int s15_16(const unsigned char *p, double *x)
{
  signed int raw = (signed int)u32(p);
  *x = (double)raw / 65536.0;
  return 1;
}

static int require_tag(const unsigned char *blob, unsigned int size, unsigned sig, unsigned *off, unsigned *sz)
{
  /* tag table: count at 128. entries: sig(4), offset(4), size(4). */
  if (size < 132) return -1;
  unsigned count = u32(blob + 128);
  if (count > 128 || (size_t)132 + (size_t)count * 12 > size) return -1;
  for (unsigned i = 0; i < count; i++)
  {
    const unsigned char *ent = blob + 132 + i * 12;
    if (u32(ent) == sig)
    {
      *off = u32(ent + 4);
      *sz = u32(ent + 6 + 2);
      /* Subtraction form so a crafted offset+size cannot wrap the unsigned sum. */
      if (*off > size || *sz > size - *off) return -1;
      return 0;
    }
  }
  return -1;
}

static int parse_xyz_tag(const unsigned char *b, unsigned size, double *XYZ)
{
  /* XYZ type: 'XYZ ' (0x58595a20). reserved 4, then 3 s15.16 (reads to +20). */
  if (size < 20) return -1;
  if (b[0] != 'X' || b[1] != 'Y' || b[2] != 'Z' || b[3] != ' ') return -1;
  s15_16(b + 8, &XYZ[0]);
  s15_16(b + 12, &XYZ[1]);
  s15_16(b + 16, &XYZ[2]);
  return 0;
}

/* approximate TRC -> closest sp_input_transfer_t enum. */
static int trc_transfer_from_curv(const unsigned char *tag, unsigned tsize, double *gamma_out)
{
  /* 'curv': type(4) + reserved(4) + count(4) at +8, then count u16 entries.
   * The single-value case reads the u16 at +12, so 14 bytes are required. */
  if (tsize < 14) return -1;
  if (tag[0] != 'c' || tag[1] != 'u' || tag[2] != 'r' || tag[3] != 'v') return -1;
  unsigned count = u32(tag + 8);
  *gamma_out = 2.2;
  if (count == 0) return (int)SP_TRANSFER_LINEAR;
  if (count == 1)
  {
    /* single gamma u8.8 */
    double g = u16(tag + 12) / 256.0;
    if (g <= 0.2) *gamma_out = 1.0;
    *gamma_out = (g > 0.2 && g < 5.0) ? g : 2.2;
    if (*gamma_out < 0.5) return (int)SP_TRANSFER_LINEAR;
    if (*gamma_out > 1.70 && *gamma_out < 1.95) return SP_TRANSFER_PROPHOTO;
    if (*gamma_out > 2.15 && *gamma_out < 2.25) return SP_TRANSFER_G22;
    if (*gamma_out > 2.35 && *gamma_out < 2.45) return SP_TRANSFER_G24;
    if (*gamma_out > 2.55 && *gamma_out < 2.70) return SP_TRANSFER_G26;
    if (*gamma_out >= 0.95 && *gamma_out <= 1.05) return (int)SP_TRANSFER_LINEAR;
    return (int)SP_TRANSFER_SRGB;
  }
  /* LUT-based curve: assume sRGB-ish (classic profile). */
  return (int)SP_TRANSFER_SRGB;
}

static int trc_transfer_from_para(const unsigned char *tag, unsigned tsize, double *gamma_out)
{
  if (tsize < 16 || tag[0] != 'p' || tag[1] != 'a' || tag[2] != 'r' || tag[3] != 'a')
    return -1;
  unsigned func = u16(tag + 8);
  double g = 1.0;
  /* para type: reserved 4 bytes at tag+4; curve type u16 at +8.
     Function 1: Y = X^g, 2: Y=(aX+b)^g+c, etc. */
  /* parse g as s15.16 at tag+12 for func>=2; func1 uses at +12 too? */
  s15_16(tag + 12, &g);
  if (g <= 0 || g > 10.0) *gamma_out = 2.4; else *gamma_out = g;
  double c = 0.0, b = 0.0;
  if (func >= 2 && tsize >= 36) s15_16(tag + 16, &b); /* b? */
  if (func >= 3 && tsize >= 40) s15_16(tag + 20, &c);
  /* sRGB-like para: function type 3, offset c ≈ -0.055. */
  if (func == 3) return (int)SP_TRANSFER_SRGB;
  if (*gamma_out > 1.70 && *gamma_out < 1.95) return SP_TRANSFER_PROPHOTO;
  if (*gamma_out > 2.15 && *gamma_out < 2.25) return SP_TRANSFER_G22;
  if (*gamma_out > 2.35 && *gamma_out < 2.45) return SP_TRANSFER_G24;
  if (*gamma_out > 2.55 && *gamma_out < 2.70) return SP_TRANSFER_G26;
  if (*gamma_out <= 1.05) return (int)SP_TRANSFER_LINEAR;
  return (int)SP_TRANSFER_SRGB;
}

int sf_icc_parse_profile(const void *data, int size, double matrix[9], double white_xy[2], int *transfer_index)
{
  const unsigned char *b = (const unsigned char *)data;
  if (!b || size < 132) return -1;

  unsigned declared = u32(b);
  if (declared > (unsigned)size) return -1;

  /* header: verify RGB -> XYZ. */
  if (!(b[16] == 'R' && b[17] == 'G' && b[18] == 'B' && b[19] == ' '))
    return -1;
  if (!(b[20] == 'X' && b[21] == 'Y' && b[22] == 'Z' && b[23] == ' '))
    return -1;

  unsigned offr, szr, offg, szg, offb, szb;
  if (require_tag(b, size, 0x7258595A, &offr, &szr) != 0) return -1; /* rXYZ */
  if (require_tag(b, size, 0x6758595A, &offg, &szg) != 0) return -1; /* gXYZ */
  if (require_tag(b, size, 0x6258595A, &offb, &szb) != 0) return -1; /* bXYZ */

  double r[3], g[3], bcol[3];
  if (parse_xyz_tag(b + offr, szr, r) != 0) return -1;
  if (parse_xyz_tag(b + offg, szg, g) != 0) return -1;
  if (parse_xyz_tag(b + offb, szb, bcol) != 0) return -1;

  /* matrix columns */
  matrix[0] = r[0]; matrix[1] = g[0]; matrix[2] = bcol[0];
  matrix[3] = r[1]; matrix[4] = g[1]; matrix[5] = bcol[1];
  matrix[6] = r[2]; matrix[7] = g[2]; matrix[8] = bcol[2];

  double X = r[0] + g[0] + bcol[0];
  double Y = r[1] + g[1] + bcol[1];
  double Z = r[2] + g[2] + bcol[2];
  if (Y == 0.0 || (X + Y + Z) == 0.0) return -1;
  white_xy[0] = X / (X + Y + Z);
  white_xy[1] = Y / (X + Y + Z);

  /* transfer: look for rTRC (use it for all three; profiles usually match).
     fall through to g/b if needed. */
  unsigned off, sz;
  int tr = (int)SP_TRANSFER_SRGB;
  double gamma = 2.2;
  if (require_tag(b, size, 0x72545243, &off, &sz) == 0 && sz >= 4)
  {
    if (b[off] == 'c' && b[off + 1] == 'u' && b[off + 2] == 'r' && b[off + 3] == 'v')
      tr = trc_transfer_from_curv(b + off, sz, &gamma);
    else if (b[off] == 'p' && b[off + 1] == 'a' && b[off + 2] == 'r' && b[off + 3] == 'a')
      tr = trc_transfer_from_para(b + off, sz, &gamma);
    else
      tr = (int)SP_TRANSFER_SRGB;
  }
  if (transfer_index) *transfer_index = tr;
  return 0;
}

/* Human-readable description tag ('desc'): ICC v2 stores a textDescriptionType
 * with a counted ASCII string; ICC v4 stores a multiLocalizedUnicodeType whose
 * first record is UTF-16BE. Either way we fold to plain ASCII (non-ASCII folds
 * to '?') so the caller can show it in a Win32 ANSI dialog. */
int sf_icc_profile_name(const void *data, int size, char *out, int outlen)
{
  const unsigned char *b = (const unsigned char *)data;
  unsigned off = 0, sz = 0, i;
  int n = 0;

  if (!b || !out || outlen < 1 || size < 132) return -1;
  out[0] = '\0';
  if (require_tag(b, size, 0x64657363u /* 'desc' */, &off, &sz) != 0) return -1;
  /* Need the 4-byte type plus room for the type-specific prologue. The 'mluc'
   * branch needs 28 bytes; check that before it reads, below. */
  if (sz < 8 || off > (unsigned)size || (unsigned)size - off < 16) return -1;

  if (b[off] == 'd' && b[off + 1] == 'e' && b[off + 2] == 's' && b[off + 3] == 'c')
  {
    unsigned count = u32(b + off + 8);
    unsigned start = off + 12; /* off + 12 <= size is guaranteed above */
    if (count > (unsigned)size - start) count = (unsigned)size - start;
    for (i = 0; i < count && n < outlen - 1; i++)
    {
      unsigned char c = b[start + i];
      if (c == 0) break;
      out[n++] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
  }
  else if (b[off] == 'm' && b[off + 1] == 'l' && b[off + 2] == 'u' && b[off + 3] == 'c')
  {
    /* multiLocalizedUnicodeType: record count, record size, then the first
       record's (language, country, length, offset) followed by UTF-16BE. */
    unsigned recs, recsz, rlen, roff, start, j;
    if ((unsigned)size - off < 28) return -1; /* prologue + first record header */
    recs = u32(b + off + 8);
    recsz = u32(b + off + 12);
    if (recs < 1 || recsz < 12) return -1;
    rlen = u32(b + off + 16 + 4);
    roff = u32(b + off + 16 + 8);
    /* roff is relative to the tag start; reject a value that would wrap. */
    if (roff > (unsigned)size - off) return -1;
    start = off + roff;
    if (rlen > (unsigned)size - start) rlen = (unsigned)size - start;
    for (j = 0; j + 1 < rlen && n < outlen - 1; j += 2)
    {
      unsigned cp = ((unsigned)b[start + j] << 8) | b[start + j + 1];
      if (cp == 0) break;
      out[n++] = (cp >= 0x20 && cp < 0x7f) ? (char)cp : '?';
    }
  }
  else
  {
    return -1;
  }

  out[n] = '\0';
  return n > 0 ? 0 : -1;
}
