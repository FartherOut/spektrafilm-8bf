/* SPDX-License-Identifier: GPL-3.0-or-later */
/* pc_json.h — tiny flat-object JSON reader for the plugin params.
 *
 * The host hands the adapter a flat object of validated parameters plus
 * `_image` (host geometry):  {"amount": 50, "mode": "soft", "_image": {...}}.
 * This reader pulls a few known scalar keys out of the top level and blindly
 * skips everything else (nested objects/arrays and strings included). No
 * allocations, no libc, no recursion.
 *
 * API
 *   pcj_obj(s, len)              -> iterator index just past optional '{'
 *   pcj_next_key(i, key, slot)   -> advance to the key's value (or 0 if done)
 *   pcj_number(...) / pcj_bool(...) / pcj_skip_value(...)
 *
 * Passing through the whole object costs O(len) — JSON is tiny per band.
 */
#ifndef PC_JSON_H
#define PC_JSON_H

#include <stdint.h>

typedef uint8_t  pj_u8;
typedef uint32_t pj_u32;
typedef int32_t  pj_i32;
typedef double   pj_f64;

static inline pj_u32 pj_ws(const pj_u8* s, pj_u32 i, pj_u32 end) {
    while (i < end && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    return i;
}

/* s[i] must be '"'. Returns index just past the closing quote. Handles \x escapes. */
static inline pj_u32 pj_string_end(const pj_u8* s, pj_u32 i, pj_u32 end) {
    ++i;
    while (i < end) {
        pj_u8 c = s[i];
        if (c == '\\') { i += 2; continue; }
        if (c == '"') return i + 1;
        ++i;
    }
    return end;
}

/* Compares the key string s[ks..ke) (ke = index of closing quote) with `lit`. */
static inline int pj_key_is(const pj_u8* s, pj_u32 ks, pj_u32 ke, const char* lit) {
    const char* l = lit;
    while (*l) {
        if (ks >= ke || s[ks] != (pj_u8)*l) return 0;
        ++ks;
        ++l;
    }
    return ks == ke;
}

/* Parses a JSON number starting at s[i]. Returns index after the token. */
static inline pj_u32 pj_number(const pj_u8* s, pj_u32 i, pj_u32 end, pj_f64* out) {
    pj_f64 v = 0.0;
    int neg = 0;
    if (i < end && s[i] == '-') { neg = 1; ++i; }
    while (i < end && s[i] >= '0' && s[i] <= '9') { v = v * 10.0 + (pj_f64)(s[i] - '0'); ++i; }
    if (i < end && s[i] == '.') {
        ++i;
        pj_f64 scale = 0.1;
        while (i < end && s[i] >= '0' && s[i] <= '9') {
            v += (pj_f64)(s[i] - '0') * scale;
            scale *= 0.1;
            ++i;
        }
    }
    if (i < end && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        int neg_exp = 0;
        if (i < end && (s[i] == '+' || s[i] == '-')) { if (s[i] == '-') neg_exp = 1; ++i; }
        pj_i32 e = 0;
        while (i < end && s[i] >= '0' && s[i] <= '9') { e = e * 10 + (pj_i32)(s[i] - '0'); ++i; }
        if (neg_exp) { while (e-- > 0) v /= 10.0; }
        else         { while (e-- > 0) v *= 10.0; }
    }
    *out = neg ? -v : v;
    return i;
}

/* Copies the raw content of a JSON string whose opening quote is at s[i] into
 * out (NUL-terminated, up to cap-1 chars). Escapes are collapsed to their
 * char (no \u decoding — choices never carry them). Returns index just past
 * the closing quote. */
static inline pj_u32 pj_string(const pj_u8* s, pj_u32 i, pj_u32 end,
                                char* out, pj_u32 cap) {
    ++i; /* opening quote */
    pj_u32 o = 0;
    while (i < end && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < end) ++i; /* skip the backslash */
        if (o + 1 < cap) out[o++] = (char)s[i];
        ++i;
    }
    if (i < end) ++i; /* closing quote */
    if (o < cap) out[o] = 0;
    return i;
}

/* Skips one JSON value (string, object, array, number, literal) starting at s[i]. */
static inline pj_u32 pj_skip_value(const pj_u8* s, pj_u32 i, pj_u32 end) {
    if (i >= end) return end;
    pj_u8 c = s[i];
    if (c == '"') return pj_string_end(s, i, end);
    if (c == '{' || c == '[') {
        pj_u32 depth = 1;
        ++i;
        while (i < end && depth) {
            pj_u8 d = s[i];
            if (d == '"') { i = pj_string_end(s, i, end); continue; }
            if (d == '{' || d == '[') ++depth;
            else if (d == '}' || d == ']') --depth;
            ++i;
        }
        return i;
    }
    /* number or true/false/null literal: read until a structural delimiter */
    while (i < end) {
        pj_u8 d = s[i];
        if (d == ',' || d == '}' || d == ']' || d == ':' || d == ' ' || d == '\t' || d == '\n' || d == '\r') break;
        ++i;
    }
    return i;
}

#endif /* PC_JSON_H */