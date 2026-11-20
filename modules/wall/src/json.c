/* Just enough JSON for Hyprland's replies: skip values, find an object's keys,
 * walk an object's members or an array's items. No allocation, no validation:
 * the input is Hyprland's own output. */
#include "lw.h"
#include <stdio.h>
#include <string.h>

static const char *skip_string(const char *p) {
  for (p++; *p && *p != '"'; p++)
    if (*p == '\\' && p[1]) p++;
  return *p ? p + 1 : p;
}

static const char *skip_space(const char *p) {
  while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r' || *p == ',') p++;
  return p;
}

const char *lw_json_skip(const char *p) {
  if (*p == '"') return skip_string(p);
  if (*p == '{' || *p == '[') {
    int depth = 0;
    while (*p) {
      if (*p == '"') {
        p = skip_string(p);
        continue;
      }
      if (*p == '{' || *p == '[') depth++;
      if (*p == '}' || *p == ']') {
        if (--depth == 0) return p + 1;
      }
      p++;
    }
    return p;
  }
  while (*p && *p != ',' && *p != '}' && *p != ']') p++;
  return p;
}

const char *lw_json_next(const char *p) { return skip_space(lw_json_skip(p)); }

const char *lw_json_get(const char *obj, const char *key) {
  size_t klen = strlen(key);
  const char *p = obj + 1;
  while (*p) {
    p = skip_space(p);
    if (*p != '"') return NULL;
    const char *k = p + 1;
    p = skip_string(p);
    bool match = (size_t)(p - 1 - k) == klen && !strncmp(k, key, klen);
    while (*p == ' ' || *p == ':') p++;
    if (match) return p;
    p = lw_json_skip(p);
  }
  return NULL;
}

void lw_json_str(const char *v, char *out, size_t len) {
  out[0] = 0;
  if (!v || *v != '"') return;
  const char *e = skip_string(v);
  snprintf(out, len, "%.*s", (int)(e - v - 2), v + 1);
}

const char *lw_json_member(const char **it, char *key, size_t keylen) {
  const char *p = *it;
  if (!p) return NULL;
  if (*p == '{') p++;
  p = skip_space(p);
  if (*p != '"') return *it = NULL;
  const char *k = p + 1;
  p = skip_string(p);
  snprintf(key, keylen, "%.*s", (int)(p - 1 - k), k);
  while (*p == ' ' || *p == ':' || *p == '\n' || *p == '\t' || *p == '\r') p++;
  if (!*p) return *it = NULL;
  *it = lw_json_skip(p);
  return p;
}

const char *lw_json_item(const char **it) {
  const char *p = *it;
  if (!p) return NULL;
  if (*p == '[') p++;
  p = skip_space(p);
  if (!*p || *p == ']' || *p == '}') return *it = NULL;
  *it = lw_json_skip(p);
  return p;
}
