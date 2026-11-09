/* SPDX-License-Identifier: GPL-3.0-or-later */
/* glib_mini — minimal GLib subset for the spektrafilm engine port (Phase 1).
 *
 * Implements exactly what glib.h declares, over plain libc. Kept deliberately
 * simple: the engine uses hash tables for pack metadata, GList only for JSON
 * member enumeration, and no threads.
 */
#include <stdio.h>
#include <stdarg.h>

#include "glib-2.0/glib.h"
#include "json-glib/json-glib.h"

/* implemented in json_mini.c; used by g_object_unref's dispatcher below */
void json_parser_free_internal(void *parser);
void json_node_free_internal(void *node);

/* ------------------------------------------------------------------ error */
void g_clear_error(GError **err)
{
  if(!err || !*err) return;
  g_free((*err)->message);
  g_free(*err);
  *err = NULL;
}

/* ------------------------------------------------------------------ strings */
gchar *g_strdup(const gchar *str)
{
  if(!str) return NULL;
  gsize n = strlen(str);
  gchar *p = g_malloc(n + 1);
  memcpy(p, str, n + 1);
  return p;
}

gchar *g_strndup(const gchar *str, gsize n)
{
  if(!str) return NULL;
  gsize l = 0;
  while(l < n && str[l]) l++;
  gchar *p = g_malloc(l + 1);
  memcpy(p, str, l);
  p[l] = 0;
  return p;
}

gchar *g_strdup_vprintf(const gchar *format, va_list args)
{
  va_list ap;
  va_copy(ap, args);
  int n = vsnprintf(NULL, 0, format, ap);
  va_end(ap);
  if(n < 0) return NULL;
  gchar *p = g_malloc((gsize)n + 1);
  va_copy(ap, args);
  vsnprintf(p, (gsize)n + 1, format, ap);
  va_end(ap);
  return p;
}

gchar *g_strdup_printf(const gchar *format, ...)
{
  va_list ap;
  va_start(ap, format);
  gchar *p = g_strdup_vprintf(format, ap);
  va_end(ap);
  return p;
}

gsize g_strlcpy(gchar *dest, const gchar *src, gsize dest_size)
{
  if(!dest_size) return strlen(src);
  const gsize sl = strlen(src);
  const gsize copy = sl < dest_size - 1 ? sl : dest_size - 1;
  memcpy(dest, src, copy);
  dest[copy] = 0;
  return sl;
}

gint g_strcmp0(const gchar *str1, const gchar *str2)
{
  if(!str1) return str2 ? -1 : 0;
  if(!str2) return 1;
  return strcmp(str1, str2);
}

/* ------------------------------------------------------------------ hashing */
guint g_str_hash(gconstpointer v)
{
  /* djb2 — matches GLib's hash so ordering/chaining behaviour is the same. */
  const unsigned char *p = (const unsigned char *)v;
  guint h = 5381;
  while(*p) h = (h << 5) + h + *p++;
  return h;
}

gboolean g_str_equal(gconstpointer v1, gconstpointer v2)
{
  return strcmp((const char *)v1, (const char *)v2) == 0;
}

/* ------------------------------------------------------------------ GList */
GList *g_list_append(GList *list, gpointer data)
{
  GList *n = g_new0(GList, 1);
  n->data = data;
  if(!list)
  {
    n->prev = NULL;
    return n;
  }
  GList *last = list;
  while(last->next) last = last->next;
  last->next = n;
  n->prev = last;
  return list;
}

void g_list_free(GList *list)
{
  while(list)
  {
    GList *n = list->next;
    g_free(list);
    list = n;
  }
}

/* -------------------------------------------------------------- GHashTable */
/* Small chained hash table. Fine for the engine's use (dozens of entries:
 * illuminant SPDs / dichroic curves by name). */
#define GH_NBUCKETS 67u

struct _GHashTable
{
  GHashFunc hash;
  GEqualFunc equal;
  GDestroyNotify key_destroy;
  GDestroyNotify value_destroy;
  struct _GHEntry
  {
    gpointer key, value;
    struct _GHEntry *next;
  } *buckets[GH_NBUCKETS];
  gint count;
};

GHashTable *g_hash_table_new_full(GHashFunc hash_func,
                                  GEqualFunc key_equal_func,
                                  GDestroyNotify key_destroy_func,
                                  GDestroyNotify value_destroy_func)
{
  GHashTable *ht = g_new0(GHashTable, 1);
  ht->hash = hash_func ? hash_func : g_str_hash;
  ht->equal = key_equal_func ? key_equal_func : g_str_equal;
  ht->key_destroy = key_destroy_func;
  ht->value_destroy = value_destroy_func;
  return ht;
}

void g_hash_table_insert(GHashTable *ht, gpointer key, gpointer value)
{
  const guint h = ht->hash(key) % GH_NBUCKETS;
  struct _GHEntry *e = ht->buckets[h];
  while(e)
  {
    if(ht->equal(e->key, key))
    {
      if(ht->key_destroy) ht->key_destroy(e->key);
      if(ht->value_destroy) ht->value_destroy(e->value);
      e->key = key;
      e->value = value;
      return;
    }
    e = e->next;
  }
  e = g_new0(struct _GHEntry, 1);
  e->key = key;
  e->value = value;
  e->next = ht->buckets[h];
  ht->buckets[h] = e;
  ht->count++;
}

gpointer g_hash_table_lookup(const GHashTable *ht, gconstpointer key)
{
  const guint h = ht->hash(key) % GH_NBUCKETS;
  const struct _GHEntry *e = ht->buckets[h];
  while(e)
  {
    if(ht->equal(e->key, key)) return e->value;
    e = e->next;
  }
  return NULL;
}

void g_hash_table_destroy(GHashTable *ht)
{
  if(!ht) return;
  for(guint i = 0; i < GH_NBUCKETS; i++)
  {
    struct _GHEntry *e = ht->buckets[i];
    while(e)
    {
      struct _GHEntry *n = e->next;
      if(ht->key_destroy) ht->key_destroy(e->key);
      if(ht->value_destroy) ht->value_destroy(e->value);
      g_free(e);
      e = n;
    }
  }
  g_free(ht);
}

/* ------------------------------------------------------------------ misc */
guint64 g_ascii_strtoull(const gchar *nptr, gchar **endptr, guint base)
{
  return (guint64)strtoull(nptr, endptr, (int)base);
}

gdouble g_ascii_strtod(const gchar *nptr, gchar **endptr)
{
  return strtod(nptr, endptr);
}

gchar *g_build_filename(const gchar *first_element, ...)
{
  gsize cap = 64, len = 0;
  gchar *out = g_malloc(cap);
  out[0] = 0;
  va_list ap;
  va_start(ap, first_element);
  const gchar *part = first_element;
  int wrote = 0;
  while(part)
  {
    const gsize pl = strlen(part);
    if(wrote) len++; /* separator */
    while((len + pl + 1) > cap)
    {
      cap *= 2;
      out = g_realloc(out, cap);
    }
    if(wrote)
    {
      out[len - 1] = '/';
      /* len already accounts for the separator slot; write part after it */
      memcpy(out + len, part, pl + 1); /* includes NUL */
      len += pl;
    }
    else
    {
      memcpy(out, part, pl + 1);
      len = pl;
    }
    wrote = 1;
    part = va_arg(ap, const gchar *);
  }
  va_end(ap);
  if(!wrote)
  {
    out[0] = 0;
    return out;
  }
  return out;
}

void g_warning(const gchar *format, ...)
{
  /* Engine diagnostics only; there is no console in the guest builds.
   * Kept as a real function so a future host-side log hook can land here
   * without touching the engine. */
  (void)format;
}

void g_object_unref(gpointer object)
{
  if(!object) return;
  GTypeInstance *t = (GTypeInstance *)object;
  if(--t->_refs > 0) return;
  if(t->_kind == 0)
    json_parser_free_internal((JsonParser *)object);
  else
    json_node_free_internal((JsonNode *)object);
}