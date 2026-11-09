/* SPDX-License-Identifier: GPL-3.0-or-later */
/* glib_mini — a minimal, self-contained subset of GLib for porting the
 * spektrafilm engine off darktable (Phase 1).
 *
 * This header stands in for `<glib.h>` via `-I` ordering: the engine sources
 * (spektra_sim.c) include `<glib.h>` and `<json-glib/json-glib.h>` untouched,
 * and only the *identifiers they actually use* are provided here. Anything not
 * in this list is a compile error — which is what we want: the surface is
 * deliberate, not incidental.
 *
 * Everything is header-declared here and implemented in ../glib_mini.c, except
 * tiny allocation wrappers which are inline. The implementation must build on
 * Linux/gcc+glibc AND MSVC's CRT, so it only uses C99 + libc.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ types */
typedef int gboolean;
typedef char gchar;
typedef unsigned char guchar;
typedef signed char gint8;
typedef unsigned char guint8;
typedef short gint16;
typedef unsigned short guint16;
typedef int gint;
typedef unsigned int guint;
typedef int32_t gint32;
typedef uint32_t guint32;
typedef int64_t gint64;
typedef uint64_t guint64;
typedef float gfloat;
typedef double gdouble;
typedef void *gpointer;
typedef const void *gconstpointer;
typedef size_t gsize;
typedef intptr_t gssize;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

/* MIN/MAX/CLAMP — darktable's common/darktable.h shim also defines MIN/MAX
 * under #ifndef; glib's CLAMP is what spektra_sim.c reaches for. */
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef CLAMP
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif

/* ---------------------------------------------------------------- memory */
static inline gpointer g_malloc(const gsize n)
{
  return malloc(n ? n : 1);
}
static inline gpointer g_malloc0(const gsize n)
{
  return calloc(n ? n : 1, 1);
}
static inline gpointer g_realloc(gpointer p, const gsize n)
{
  return realloc(p, n ? n : 1);
}
static inline void g_free(gpointer p)
{
  free(p);
}
#define g_new(type, n) ((type *)g_malloc((gsize)(n) * sizeof(type)))
#define g_new0(type, n) ((type *)g_malloc0((gsize)(n) * sizeof(type)))

/* ------------------------------------------------------------------ error */
typedef struct _GError GError;
struct _GError
{
  gint domain;
  gint code;
  gchar *message;
};
void g_clear_error(GError **err);

/* ------------------------------------------------------------------ strings */
gchar *g_strdup(const gchar *str);
gchar *g_strndup(const gchar *str, gsize n);
gchar *g_strdup_vprintf(const gchar *format, va_list args);
gchar *g_strdup_printf(const gchar *format, ...);
gsize g_strlcpy(gchar *dest, const gchar *src, gsize dest_size);
gint g_strcmp0(const gchar *str1, const gchar *str2);

/* ------------------------------------------------------------ hash table */
typedef struct _GHashTable GHashTable;
typedef guint (*GHashFunc)(gconstpointer key);
typedef gboolean (*GEqualFunc)(gconstpointer a, gconstpointer b);
typedef void (*GDestroyNotify)(gpointer data);

GHashTable *g_hash_table_new_full(GHashFunc hash_func,
                                  GEqualFunc key_equal_func,
                                  GDestroyNotify key_destroy_func,
                                  GDestroyNotify value_destroy_func);
void g_hash_table_insert(GHashTable *hash_table, gpointer key, gpointer value);
gpointer g_hash_table_lookup(const GHashTable *hash_table, gconstpointer key);
void g_hash_table_destroy(GHashTable *hash_table);
guint g_str_hash(gconstpointer v);
gboolean g_str_equal(gconstpointer v1, gconstpointer v2);

/* ------------------------------------------------------------------ lists */
typedef struct _GList
{
  gpointer data;
  struct _GList *next;
  struct _GList *prev;
} GList;
GList *g_list_append(GList *list, gpointer data);
void g_list_free(GList *list);

/* ------------------------------------------------------------------ atomics */
/* Single-threaded use only; the engine never shares these across threads. */
#define g_atomic_int_inc(ai) ((void)(++(*g_atomic_int_location(ai))))
static inline gint *g_atomic_int_location(gint *ai)
{
  return ai;
}
static inline gboolean g_atomic_int_dec_and_test(gint *ai)
{
  return --(*ai) == 0;
}

/* ------------------------------------------------------------------ misc */
guint64 g_ascii_strtoull(const gchar *nptr, gchar **endptr, guint base);
gdouble g_ascii_strtod(const gchar *nptr, gchar **endptr);
gchar *g_build_filename(const gchar *first_element, ...);
void g_warning(const gchar *format, ...);

/* glib-object subset: only object unref is used, and only on JsonParser /
 * JsonNode. Dispatched in json_mini.c via a shared header tag. */
typedef struct _GTypeInstance
{
  gint _refs;
  gint _kind; /* 0 = parser, 1 = node */
} GTypeInstance;
void g_object_unref(gpointer object);