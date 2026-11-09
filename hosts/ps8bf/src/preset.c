/* SPDX-License-Identifier: GPL-3.0-or-later */
/* preset.c — codec for parameter presets (see preset.h).
 *
 * The block is written field-by-field through a table of (key, offset, type,
 * count) rather than memcpy'd as bytes: a preset is meant to be hand-editable,
 * to survive SF_PARAMS_BLOCK_VERSION bumps and pack reordering, and to read
 * back exactly. Two consequences of that choice are worth stating up front:
 *
 *   - Any field added to sf_params_block must be added to sf_pfields[] below
 *     (and to the excluded list if it is not part of a look). A field left out
 *     silently keeps the engine default when a preset is loaded — the harness
 *     round-trip test perturbs a wide sample so most such slips show up there.
 *   - Numbers are printed in the shortest decimal form that reads back
 *     bit-exact, so a load never perturbs a value and the render stays
 *     byte-identical. Both directions are locale-proof: the C runtime formats
 *     and parses doubles with the *locale's* decimal separator, but a preset
 *     is always '.'-separated JSON, and a host is free to have called
 *     setlocale(). The separators are swapped explicitly rather than by
 *     touching the process locale, which the host owns.
 */
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "preset.h"

#include "pack_list.h"
#include "pc_json.h"

/* ----------------------------------------------------------------- types -- */

enum { PT_I32, PT_INT, PT_F32, PT_F64 };

typedef struct sf_pfield
{
  const char *name;   /* JSON key */
  size_t      off;    /* byte offset of the element into sf_params_block */
  unsigned char type; /* PT_* */
  unsigned char count; /* 1 = scalar, >1 = JSON array length */
} sf_pfield;

#define PF(n, t)     { #n, offsetof(sf_params_block, n), (unsigned char)(t), 1u }
#define PFA(n, t, c) { #n, offsetof(sf_params_block, n), (unsigned char)(t), (unsigned char)(c) }

/* Every field that is part of a "look", in sf_params_block order so the table
 * can be audited against params.h by eye. film_index/paper_index are handled
 * separately as pack stems. */
static const sf_pfield sf_pfields[] =
{
  PF(scan_film, PT_I32),
  PF(process_mode, PT_I32),
  PF(scan_negative_invert, PT_I32),
  PF(print_timing, PT_I32),
  PF(printer_lights_gang, PT_I32),
  PF(printer_light_calibration, PT_I32),
  PF(printer_lights_r, PT_F32),
  PF(printer_lights_g, PT_F32),
  PF(printer_lights_b, PT_F32),
  PF(camera_uv_filter_enabled, PT_I32),
  PF(camera_uv_cut_nm, PT_F32),
  PF(camera_ir_filter_enabled, PT_I32),
  PF(camera_ir_cut_nm, PT_F32),
  PF(exposure_ev, PT_F64),
  PF(print_exposure, PT_F64),
  PF(print_auto_exposure, PT_I32),
  PF(couplers_active, PT_I32),
  PF(coupler_diffusion_um, PT_F64),
  PF(lut_steps, PT_I32),
  PF(pixel_um, PT_F64),
  PF(input_space, PT_I32),
  PF(input_transfer, PT_I32),
  PF(input_profile_auto, PT_I32),
  /* input_custom_* (derived from the document ICC) and auto_exposure_ev
   * (metered by the host every invocation) are deliberately absent. */
  PF(auto_exposure, PT_I32),
  PF(auto_exposure_method, PT_I32),
  PF(coupler_amount, PT_F64),
  PF(coupler_inhibition_samelayer, PT_F64),
  PF(coupler_inhibition_interlayer, PT_F64),

  PF(boost_ev, PT_F32),
  PF(boost_range, PT_F32),
  PF(protect_ev, PT_F32),
  PF(diffusion_on, PT_I32),
  PF(diffusion_family, PT_I32),
  PF(diffusion_strength, PT_F32),
  PF(diffusion_scale, PT_F32),
  PF(diffusion_warmth, PT_F32),
  PF(diffusion_core_intensity, PT_F32),
  PF(diffusion_core_size, PT_F32),
  PF(diffusion_halo_intensity, PT_F32),
  PF(diffusion_halo_size, PT_F32),
  PF(diffusion_bloom_intensity, PT_F32),
  PF(diffusion_bloom_size, PT_F32),
  PF(halation_on, PT_I32),
  PF(scatter_amount, PT_F32),
  PF(scatter_scale, PT_F32),
  PF(halation_amount, PT_F32),
  PF(halation_scale, PT_F32),

  PF(grain_on, PT_I32),
  PF(grain_amount, PT_F32),
  PF(grain_saturation, PT_F32),
  PF(grain_blur, PT_F32),
  PF(grain_granularity, PT_F32),
  PF(grain_uniformity, PT_F32),
  PF(grain_seed, PT_INT),
  PF(grain_micro_structure_scale, PT_F32),
  PF(grain_micro_structure_sigma_nm, PT_F32),
  PF(grain_sublayer_scale, PT_F32),
  PF(grain_density_min, PT_F32),
  PF(grain_dye_cloud, PT_F32),
  PF(grain_usm_sigma, PT_F32),
  PF(grain_usm_amount, PT_F32),
  PF(grain_model, PT_I32),
  PF(grain_sublayers_enabled, PT_I32),
  PF(grain_sublayer_count, PT_I32),
  PFA(grain_particle_scale, PT_F32, 3),
  PFA(grain_particle_scale_layer, PT_F32, 3),
  PFA(grain_uniformity_rgb, PT_F32, 3),
  PFA(grain_density_min_rgb, PT_F32, 3),
  PF(grain_final_blur_um, PT_F32),
  PF(grain_dye_cloud_um, PT_F32),

  PF(print_diffusion_on, PT_I32),
  PF(print_diffusion_family, PT_I32),
  PF(print_diffusion_strength, PT_F32),
  PF(print_diffusion_scale, PT_F32),
  PF(print_diffusion_warmth, PT_F32),
  PF(print_diffusion_core_intensity, PT_F32),
  PF(print_diffusion_core_size, PT_F32),
  PF(print_diffusion_halo_intensity, PT_F32),
  PF(print_diffusion_halo_size, PT_F32),
  PF(print_diffusion_bloom_intensity, PT_F32),
  PF(print_diffusion_bloom_size, PT_F32),

  PF(scan_blur, PT_F32),
  PF(scan_usm_sigma, PT_F32),
  PF(scan_usm_amount, PT_F32),
  PF(glare_percent, PT_F32),
  PF(scan_glare_roughness, PT_F32),
  PF(scan_glare_blur, PT_F32),

  PFA(dir_gamma_same, PT_F64, 3),
  PFA(dir_gamma_r_gb, PT_F64, 2),
  PFA(dir_gamma_g_rb, PT_F64, 2),
  PFA(dir_gamma_b_rg, PT_F64, 2),
  PF(filter_m_shift, PT_F64),
  PF(filter_y_shift, PT_F64),
  PF(filter_c_shift, PT_F64),
  PF(preflash_exposure, PT_F64),
  PF(preflash_m_shift, PT_F64),
  PF(preflash_y_shift, PT_F64),
  PFA(halation_strength, PT_F64, 3),
  PFA(halation_first_sigma_um, PT_F64, 3),
  PF(halation_sigma_max_um, PT_F64),
  PF(scan_black_correction, PT_I32),
  PF(scan_white_correction, PT_I32),
  PF(scan_black_level, PT_F64),
  PF(scan_white_level, PT_F64),
  PF(scan_mtf50_lpmm, PT_F64),
  PF(scan_usm_radius_um, PT_F64),

  PF(film_push_pull_stops, PT_F64),
  PF(film_push_pull_mode, PT_INT),
  PF(negative_bleach_bypass_amount, PT_F64),
  PF(negative_leuco_cyan_coupling, PT_F64),
  PF(print_bleach_bypass_amount, PT_F64),
  PF(film_gamma, PT_F64),
  PF(print_gamma, PT_F64),
  PF(print_push_pull_stops, PT_F64),
  PF(print_shadow_shape, PT_F64),
  PF(print_highlight_shape, PT_F64),

  PF(film_format, PT_I32),
  PF(rgb_to_raw_method, PT_I32)
};

#define SF_PF_COUNT (sizeof sf_pfields / sizeof sf_pfields[0])

/* Longest "%.17g" run plus slack, and the token buffers that read numbers. */
#define SF_NUM_SZ 40
#define SF_TOK_SZ (SF_NUM_SZ + 8)

/* ------------------------------------------------------------- formatting -- */

/* The decimal separator the C runtime is currently using. Anything that is not
 * the plain ASCII '.' is swapped in and out around strtod/snprintf. */
static char sf_decsep(void)
{
  const struct lconv *lc = localeconv();
  if(lc && lc->decimal_point && lc->decimal_point[0] && !lc->decimal_point[1])
    return lc->decimal_point[0];
  return '.';
}

static void sf_sep_to_dot(char *s)
{
  const char d = sf_decsep();
  if(d == '.') return;
  for(; *s; s++)
    if(*s == d) *s = '.';
}

static void sf_dot_to_sep(char *s)
{
  const char d = sf_decsep();
  if(d == '.') return;
  for(; *s; s++)
    if(*s == '.') *s = d;
}

/* strtod over a '.'-decimal token, honouring the current locale. */
static double sf_strtod(const char *tok)
{
  char tmp[SF_TOK_SZ];
  size_t n = strlen(tok);
  if(n >= sizeof tmp) return 0.0;
  memcpy(tmp, tok, n + 1);
  sf_dot_to_sep(tmp);
  return strtod(tmp, NULL);
}

/* Append printf-formatted text at *p; -1 if it would not fit. vsnprintf
 * NUL-terminates within the buffer, so buf stays valid at every step. */
static int sf_app(char *buf, size_t cap, size_t *p, const char *fmt, ...)
{
  va_list ap;
  int w;
  if(*p >= cap) return -1;
  va_start(ap, fmt);
  w = vsnprintf(buf + *p, cap - *p, fmt, ap);
  va_end(ap);
  if(w < 0 || (size_t)w >= cap - *p) return -1;
  *p += (size_t)w;
  return 0;
}

/* The shortest decimal form that reads back bit-exact: %.6g for a float and
 * %.15g for a double first, so common values stay readable (0.5, 1.25), then
 * the precision guaranteed for the type — 9 digits always identify a float,
 * 17 always reproduce a double. Non-finite values have no JSON spelling, so
 * they are normalised to 0 rather than allowed to poison the file.
 *
 * The exactness test runs before the separator swap because snprintf has just
 * written the locale's separator, which is precisely what strtod wants here.
 * Returns -1 only if the buffer is too small. */
static int sf_num(char *dst, size_t cap, double v, int is_float)
{
  const int short_p = is_float ? 6 : 15;
  const int full_p  = is_float ? 9 : 17;
  char tmp[SF_NUM_SZ];
  int w;
  int exact = 0;

  if(!isfinite(v)) v = 0.0;

  w = snprintf(tmp, sizeof tmp, "%.*g", short_p, v);
  if(w > 0 && (size_t)w < sizeof tmp)
    exact = is_float ? ((float)strtod(tmp, NULL) == (float)v)
                     : (strtod(tmp, NULL) == v);

  if(!exact)
  {
    w = snprintf(tmp, sizeof tmp, "%.*g", full_p, v);
    if(w <= 0 || (size_t)w >= sizeof tmp) return -1;
  }

  sf_sep_to_dot(tmp);
  if((size_t)w + 1 > cap) return -1;
  memcpy(dst, tmp, (size_t)w + 1);
  return w;
}

/* Quote `s` as a JSON string (including the quotes). Control characters are
 * dropped — they are invalid unescaped and meaningless in a label. */
static int sf_qstr(char *dst, size_t cap, const char *s)
{
  size_t o = 0;
  if(!s) s = "";
  if(cap < 3) return -1;
  dst[o++] = '"';
  for(; *s; s++)
  {
    const unsigned char c = (unsigned char)*s;
    if(c == '"' || c == '\\')
    {
      if(o + 3 > cap) return -1;
      dst[o++] = '\\';
      dst[o++] = (char)c;
    }
    else if(c >= 0x20)
    {
      if(o + 2 > cap) return -1;
      dst[o++] = (char)c;
    }
  }
  if(o + 2 > cap) return -1;
  dst[o++] = '"';
  dst[o] = 0;
  return (int)o;
}

/* "key": value, separated by ",\n" from every entry but the first. */
static int sf_kv(char *buf, size_t cap, size_t *p, int *first,
                 const char *key, const char *val)
{
  if(sf_app(buf, cap, p, "%s    \"%s\": %s", *first ? "" : ",\n", key, val))
    return -1;
  *first = 0;
  return 0;
}

/* --------------------------------------------------------------- reading -- */

static int sf_find(const char *const *list, int n, const char *stem)
{
  int i;
  if(!stem || !stem[0]) return -1;
  for(i = 0; i < n; i++)
    if(list[i] && strcmp(list[i], stem) == 0) return i;
  return -1;
}

/* Copy the raw token at s[i] (a run of non-structural characters) into dst. */
static pj_u32 sf_token(const pj_u8 *s, pj_u32 i, pj_u32 end, char *dst, size_t cap)
{
  size_t o = 0;
  if(cap) dst[0] = 0;
  while(i < end)
  {
    const pj_u8 c = s[i];
    if(c == ',' || c == '}' || c == ']' || c == ':' || c == ' ' || c == '\t' ||
       c == '\n' || c == '\r')
      break;
    if(o + 1 < cap) dst[o++] = (char)c;
    ++i;
  }
  if(cap) dst[o] = 0;
  return i;
}

/* A JSON number in the token, or -1 (true/false/null/"x" and non-finite values
 * are rejected so the field keeps its default instead of becoming 0). */
static int sf_num_text(const char *tok, double *out)
{
  const char *t = tok;
  if(*t == '-') ++t;
  if(*t < '0' || *t > '9') return -1;
  *out = sf_strtod(tok);
  if(!isfinite(*out)) return -1;
  return 0;
}

/* A JSON integer in the token, or -1. The whole token must be consumed, so
 * "1.0"/"0x10" are refused for an integer field rather than silently truncated. */
static int sf_int_text(const char *tok, long long *out)
{
  char *e = NULL;
  long long v;
  if(tok[0] != '-' && (tok[0] < '0' || tok[0] > '9')) return -1;
  errno = 0;
  v = strtoll(tok, &e, 10);
  if(e == tok || *e != '\0' || errno == ERANGE) return -1;
  *out = v;
  return 0;
}

/* Write one element of a field from a parsed token. Returns 0 when it took,
 * -1 when the value was refused (the field then keeps its default). */
static int sf_assign(const sf_pfield *f, sf_params_block *out, unsigned k,
                     const char *tok)
{
  unsigned char *base = (unsigned char *)(void *)out;

  switch(f->type)
  {
    case PT_I32:
    {
      long long v;
      int32_t t;
      if(sf_int_text(tok, &v) || v < INT32_MIN || v > INT32_MAX) return -1;
      t = (int32_t)v;
      memcpy(base + f->off + k * sizeof t, &t, sizeof t);
      return 0;
    }
    case PT_INT:
    {
      long long v;
      int t;
      if(sf_int_text(tok, &v) || v < INT_MIN || v > INT_MAX) return -1;
      t = (int)v;
      memcpy(base + f->off + k * sizeof t, &t, sizeof t);
      return 0;
    }
    case PT_F32:
    {
      double d;
      float t;
      if(sf_num_text(tok, &d)) return -1;
      t = (float)d;
      if(!isfinite(t)) return -1;   /* 1e300 is finite but overflows a float */
      memcpy(base + f->off + k * sizeof t, &t, sizeof t);
      return 0;
    }
    case PT_F64:
    {
      double d;
      if(sf_num_text(tok, &d)) return -1;
      memcpy(base + f->off + k * sizeof d, &d, sizeof d);
      return 0;
    }
  }
  return -1;
}

/* Parse the flat "params" object at *ip into `out`. Returns -1 on a
 * structural error; unknown keys and mistyped values are skipped, leaving the
 * default in place. */
static int sf_read_fields(const pj_u8 *s, pj_u32 *ip, pj_u32 end,
                          sf_params_block *out)
{
  pj_u32 i = *ip;
  char str[128];

  if(i >= end || s[i] != '{') return -1;
  ++i;

  for(;;)
  {
    pj_u32 ks, ke;
    int matched = 0;

    i = pj_ws(s, i, end);
    if(i < end && s[i] == ',') i = pj_ws(s, i + 1, end);   /* member separator */
    if(i >= end) return -1;
    if(s[i] == '}') { ++i; break; }
    if(s[i] != '"') return -1;

    ks = i + 1;
    ke = pj_string_end(s, i, end);
    if(ke == 0 || ke > end || s[ke - 1] != '"') return -1;   /* unterminated */
    ke -= 1;

    i = pj_ws(s, ke + 1, end);
    if(i >= end || s[i] != ':') return -1;
    i = pj_ws(s, i + 1, end);
    if(i >= end) return -1;

    if(pj_key_is(s, ks, ke, "film") || pj_key_is(s, ks, ke, "paper"))
    {
      const int is_film = pj_key_is(s, ks, ke, "film");
      if(s[i] == '"')
      {
        i = pj_string(s, i, end, str, sizeof str);
        {
          const int idx = sf_find(is_film ? SF_FILMS : SF_PAPERS,
                                  is_film ? SF_FILMS_COUNT : SF_PAPERS_COUNT,
                                  str);
          if(idx >= 0)
          {
            if(is_film) out->film_index = (int16_t)idx;
            else        out->paper_index = (int16_t)idx;
          }
        }
      }
      else
        i = pj_skip_value(s, i, end);
      matched = 1;
    }
    else
    {
      size_t f;
      for(f = 0; f < SF_PF_COUNT; f++)
      {
        if(!pj_key_is(s, ks, ke, sf_pfields[f].name)) continue;
        if(sf_pfields[f].count > 1)
        {
          unsigned n = 0;
          if(s[i] != '[') { i = pj_skip_value(s, i, end); matched = 1; break; }
          ++i;
          for(;;)
          {
            i = pj_ws(s, i, end);
            if(i >= end) return -1;
            if(s[i] == ']') { ++i; break; }
            if(s[i] == '"' || s[i] == '{' || s[i] == '[')
              i = pj_skip_value(s, i, end);
            else
            {
              char tok[SF_TOK_SZ];
              const pj_u32 before = i;
              i = sf_token(s, i, end, tok, sizeof tok);
              if(i == before) return -1;
              if(n < sf_pfields[f].count)
              {
                if(sf_assign(&sf_pfields[f], out, n, tok) == 0) ++n;
              }
            }
            i = pj_ws(s, i, end);
            if(i < end && s[i] == ',') ++i;
          }
        }
        else if(s[i] == '"' || s[i] == '{' || s[i] == '[')
        {
          i = pj_skip_value(s, i, end);   /* a string/array/object where a
                                           * number goes: keep the default */
        }
        else
        {
          char tok[SF_TOK_SZ];
          const pj_u32 before = i;
          i = sf_token(s, i, end, tok, sizeof tok);
          if(i == before) return -1;
          sf_assign(&sf_pfields[f], out, 0, tok);   /* refusal keeps default */
        }
        matched = 1;
        break;
      }
    }

    if(!matched) i = pj_skip_value(s, i, end);
  }

  *ip = i;
  return 0;
}

/* ----------------------------------------------------------------- write -- */

/* Text for one element: a bare number for scalars, "[a,b,c]" for arrays. */
static int sf_field_text(const sf_pfield *f, const void *blk, unsigned k,
                         char *dst, size_t cap)
{
  const unsigned char *base = (const unsigned char *)blk;

  switch(f->type)
  {
    case PT_I32: { int32_t v; memcpy(&v, base + f->off + k * sizeof v, sizeof v);
                   return snprintf(dst, cap, "%lld", (long long)v); }
    case PT_INT: { int v;     memcpy(&v, base + f->off + k * sizeof v, sizeof v);
                   return snprintf(dst, cap, "%lld", (long long)v); }
    case PT_F32: { float v;   memcpy(&v, base + f->off + k * sizeof v, sizeof v);
                   return sf_num(dst, cap, (double)v, 1); }
    case PT_F64: { double v;  memcpy(&v, base + f->off + k * sizeof v, sizeof v);
                   return sf_num(dst, cap, v, 0); }
  }
  return -1;
}

static int sf_field_value(const sf_pfield *f, const void *blk, char *dst, size_t cap)
{
  size_t p = 0;
  unsigned k;

  if(f->count == 1)
    return sf_field_text(f, blk, 0, dst, cap);

  if(sf_app(dst, cap, &p, "[")) return -1;
  for(k = 0; k < f->count; k++)
  {
    char num[SF_NUM_SZ];
    if(sf_field_text(f, blk, k, num, sizeof num) < 0) return -1;
    if(sf_app(dst, cap, &p, "%s%s", k ? "," : "", num)) return -1;
  }
  if(sf_app(dst, cap, &p, "]")) return -1;
  return (int)p;
}

int sf_preset_write(const sf_params_block *b, const char *name,
                    char *buf, size_t cap)
{
  char q[256];
  char val[SF_NUM_SZ * 12];
  size_t p = 0, i;
  int first = 1;

  if(!b || !buf || cap < 16) return -1;

  if(sf_app(buf, cap, &p, "{\n  \"format\": \"%s\",\n  \"version\": %d,\n",
            SF_PRESET_FORMAT, SF_PRESET_VERSION))
    return -1;
  if(sf_qstr(q, sizeof q, name) < 0) return -1;
  if(sf_app(buf, cap, &p, "  \"name\": %s,\n", q)) return -1;
  if(sf_app(buf, cap, &p, "  \"params\": {\n")) return -1;

  /* Stems, not indices: a preset must not break when the pack gains a film. */
  if(b->film_index >= 0 && b->film_index < SF_FILMS_COUNT)
  {
    if(sf_qstr(q, sizeof q, SF_FILMS[b->film_index]) < 0) return -1;
    if(sf_kv(buf, cap, &p, &first, "film", q)) return -1;
  }
  if(b->paper_index >= 0 && b->paper_index < SF_PAPERS_COUNT)
  {
    if(sf_qstr(q, sizeof q, SF_PAPERS[b->paper_index]) < 0) return -1;
    if(sf_kv(buf, cap, &p, &first, "paper", q)) return -1;
  }

  for(i = 0; i < SF_PF_COUNT; i++)
  {
    if(sf_field_value(&sf_pfields[i], b, val, sizeof val) < 0) return -1;
    if(sf_kv(buf, cap, &p, &first, sf_pfields[i].name, val)) return -1;
  }

  if(sf_app(buf, cap, &p, "\n  }\n}\n")) return -1;
  return (int)p;
}

/* ------------------------------------------------------------------ read -- */

int sf_preset_read(const char *json, int len, sf_params_block *b)
{
  const pj_u8 *s;
  sf_params_block out;
  pj_u32 i, end;
  int have_format = 0, have_version = 0, have_params = 0, version = 0;

  if(!json || !b || len <= 0 || len > SF_PRESET_JSON_MAX) return -1;
  s = (const pj_u8 *)json;
  end = (pj_u32)len;

  sf_params_init(&out);

  i = pj_ws(s, 0, end);
  if(i >= end || s[i] != '{') return -1;
  ++i;

  for(;;)
  {
    pj_u32 ks, ke;
    i = pj_ws(s, i, end);
    if(i < end && s[i] == ',') i = pj_ws(s, i + 1, end);   /* member separator */
    if(i >= end) return -1;
    if(s[i] == '}') { ++i; break; }
    if(s[i] != '"') return -1;

    ks = i + 1;
    ke = pj_string_end(s, i, end);
    if(ke == 0 || ke > end || s[ke - 1] != '"') return -1;
    ke -= 1;

    i = pj_ws(s, ke + 1, end);
    if(i >= end || s[i] != ':') return -1;
    i = pj_ws(s, i + 1, end);
    if(i >= end) return -1;

    if(pj_key_is(s, ks, ke, "format"))
    {
      char str[64];
      if(s[i] != '"') return -1;
      i = pj_string(s, i, end, str, sizeof str);
      if(strcmp(str, SF_PRESET_FORMAT) != 0) return -1;   /* foreign JSON */
      have_format = 1;
    }
    else if(pj_key_is(s, ks, ke, "version"))
    {
      char tok[SF_TOK_SZ];
      long long v;
      const pj_u32 before = i;
      i = sf_token(s, i, end, tok, sizeof tok);
      if(i == before || sf_int_text(tok, &v)) return -1;
      version = (int)v;
      have_version = 1;
    }
    else if(pj_key_is(s, ks, ke, "params"))
    {
      if(sf_read_fields(s, &i, end, &out)) return -1;
      have_params = 1;
    }
    else
      i = pj_skip_value(s, i, end);   /* "name" and anything unknown */
  }

  if(!have_format || !have_version) return -1;
  if(version < 1 || version > SF_PRESET_VERSION) return -1;
  if(!have_params) return -1;

  *b = out;
  return 0;
}
