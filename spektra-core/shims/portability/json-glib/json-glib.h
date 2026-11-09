/* SPDX-License-Identifier: GPL-3.0-or-later */
/* json_mini — a minimal, self-contained JSON DOM + the subset of the
 * json-glib API that the spektrafilm engine uses.
 *
 * Stands in for `<json-glib/json-glib.h>`. The API mirrors json-glib's
 * semantics precisely where the engine relies on them:
 *   - json_array_get_element / json_object_get_member return BORROWED nodes
 *     (no ref is taken for a getter);
 *   - json_object_get_members returns a GList of strings owned by the object
 *     (the caller frees only the list, never the strings);
 *   - null JSON elements decode through json_node_is_null();
 *   - json_parser_load_from_file / pack/profile loads parse a *tree* (nested
 *     objects and arrays), unlike the flat pc_json.h used for filter params.
 *
 * Implementation: ../json_mini.c. Strings are decoded (escapes handled) into
 * memory owned by their node; numbers are stored as double.
 */
#pragma once

#include "../glib-2.0/glib.h"

typedef struct _JsonParser JsonParser;
typedef struct _JsonNode JsonNode;

typedef enum
{
  JSON_NODE_OBJECT,
  JSON_NODE_ARRAY,
  JSON_NODE_STRING,
  JSON_NODE_NUMBER,
  JSON_NODE_BOOLEAN,
  JSON_NODE_NULL
} JsonNodeType;

/* ---- node (all value kinds live in one struct; JsonObject / JsonArray are
 * type casts of a JsonNode — the same aliasing json-glib exposes) ---- */

/* object member: name+node pair held by JsonNode.u.obj */
typedef struct _JsonMember
{
  const gchar *name;  /* decoded, owned by the member array */
  JsonNode *node;     /* owned */
} JsonMember;

struct _JsonNode
{
  GTypeInstance ref; /* shared refcount/kind tag (see glib.h) */
  JsonNodeType type;
  union
  {
    struct
    {
      JsonMember *members;
      gint n, cap;
    } obj;
    struct
    {
      JsonNode **els;
      gint n, cap;
    } arr;
    gchar *str; /* decoded string */
    double num;
    gboolean b;
  } u;
};

typedef JsonNode JsonObject;
typedef JsonNode JsonArray;

/* ---- parser ---- */
struct _JsonParser
{
  GTypeInstance ref;
  gchar *buf;        /* owned copy of the input */
  gsize len;
  JsonNode *root;    /* owned */
  const gchar *fail; /* human message, static */
  struct jarena *mem; /* parse-time bump arena, freed at destroy */
};

JsonParser *json_parser_new(void);
gboolean json_parser_load_from_data(JsonParser *parser,
                                    const gchar *data,
                                    gssize length,
                                    GError **error);
gboolean json_parser_load_from_file(JsonParser *parser,
                                    const gchar *filename,
                                    GError **error);
JsonNode *json_parser_get_root(JsonParser *parser);

/* ---- node accessors ---- */
JsonNodeType json_node_get_node_type(const JsonNode *node);
#define JSON_NODE_HOLDS_OBJECT(node) ((node) && (node)->type == JSON_NODE_OBJECT)
#define JSON_NODE_HOLDS_ARRAY(node) ((node) && (node)->type == JSON_NODE_ARRAY)
#define JSON_NODE_HOLDS_STRING(node) ((node) && (node)->type == JSON_NODE_STRING)
#define JSON_NODE_HOLDS_NUMBER(node) ((node) && (node)->type == JSON_NODE_NUMBER)
#define JSON_NODE_HOLDS_BOOLEAN(node) ((node) && (node)->type == JSON_NODE_BOOLEAN)
#define JSON_NODE_HOLDS_NULL(node) ((node) && (node)->type == JSON_NODE_NULL)

JsonObject *json_node_get_object(JsonNode *node);
JsonArray *json_node_get_array(JsonNode *node);
const gchar *json_node_get_string(JsonNode *node);
double json_node_get_double(JsonNode *node);
gboolean json_node_get_boolean(JsonNode *node);
gboolean json_node_is_null(const JsonNode *node);

/* ---- object accessors ---- */
gboolean json_object_has_member(JsonObject *object, const gchar *member_name);
JsonNode *json_object_get_member(JsonObject *object, const gchar *member_name);
GList *json_object_get_members(JsonObject *object);

JsonObject *json_object_get_object_member(JsonObject *object, const gchar *name);
JsonArray *json_object_get_array_member(JsonObject *object, const gchar *name);
const gchar *json_object_get_string_member(JsonObject *object, const gchar *name);
gint json_object_get_int_member(JsonObject *object, const gchar *name);
double json_object_get_double_member(JsonObject *object, const gchar *name);
gboolean json_object_get_boolean_member(JsonObject *object, const gchar *name);
JsonNode *json_object_get_null_member(JsonObject *object, const gchar *name);

/* ---- array accessors ---- */
guint json_array_get_length(JsonArray *array);
JsonNode *json_array_get_element(JsonArray *array, guint index);
JsonObject *json_array_get_object_element(JsonArray *array, guint index);
JsonArray *json_array_get_array_element(JsonArray *array, guint index);
const gchar *json_array_get_string_element(JsonArray *array, guint index);
gint json_array_get_int_element(JsonArray *array, guint index);
double json_array_get_double_element(JsonArray *array, guint index);
gboolean json_array_get_boolean_element(JsonArray *array, guint index);