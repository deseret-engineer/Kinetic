/* A small TOML subset: [tables], [[array tables]], and key = string | number |
 * bool | flat array. Enough for wallpaper manifests, the config, and Omarchy's
 * colors.toml. Dotted table names are kept verbatim ("params.aurora"). */
#include "lw.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct entry {
  char table[96];
  char key[64];
  struct lw_tval val;
};

struct lw_toml {
  struct entry *e;
  int n, cap;
  struct { char name[64]; int count; } arrays[16];
  int narrays;
};

static const char *skip_ws(const char *p) {
  while (*p == ' ' || *p == '\t') p++;
  return p;
}

static void val_free(struct lw_tval *v) {
  free(v->s);
  for (int i = 0; i < v->len; i++) val_free(&v->arr[i]);
  free(v->arr);
}

static const char *parse_string(const char *p, char **out) {
  char q = *p++;
  size_t cap = 64, n = 0;
  char *s = malloc(cap);
  while (*p && *p != q && *p != '\n') {
    char c = *p++;
    if (c == '\\' && q == '"' && *p) {
      char e = *p++;
      c = e == 'n' ? '\n' : e == 't' ? '\t' : e;
    }
    if (n + 2 >= cap) s = realloc(s, cap *= 2);
    s[n++] = c;
  }
  s[n] = 0;
  if (*p == q) p++;
  *out = s;
  return p;
}

static const char *parse_value(const char *p, struct lw_tval *v) {
  memset(v, 0, sizeof *v);
  p = skip_ws(p);
  if (*p == '"' || *p == '\'') {
    v->type = LW_T_STR;
    return parse_string(p, &v->s);
  }
  if (*p == '[') {
    v->type = LW_T_ARR;
    p++;
    for (;;) {
      while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') p++;
      if (*p == '#') {
        while (*p && *p != '\n') p++;
        continue;
      }
      if (!*p || *p == ']') break;
      v->arr = realloc(v->arr, sizeof(struct lw_tval) * (v->len + 1));
      const char *q = parse_value(p, &v->arr[v->len]);
      v->len++;
      if (q == p) break;
      p = q;
    }
    if (*p == ']') p++;
    return p;
  }
  if (!strncmp(p, "true", 4)) {
    v->type = LW_T_BOOL;
    v->n = 1;
    return p + 4;
  }
  if (!strncmp(p, "false", 5)) {
    v->type = LW_T_BOOL;
    return p + 5;
  }
  char *end;
  v->type = LW_T_NUM;
  v->n = strtod(p, &end);
  if (end == p) { /* bare word: treat as string up to delimiter */
    const char *s = p;
    while (*p && !strchr(" \t\n\r,]#", *p)) p++;
    v->type = LW_T_STR;
    v->s = strndup(s, p - s);
    return p;
  }
  return end;
}

struct lw_toml *lw_toml_parse(const char *text) {
  struct lw_toml *t = calloc(1, sizeof *t);
  char table[96] = "";
  const char *p = text;
  while (*p) {
    p = skip_ws(p);
    if (*p == '\n' || *p == '\r') {
      p++;
      continue;
    }
    if (*p == '#') {
      while (*p && *p != '\n') p++;
      continue;
    }
    if (*p == '[') {
      bool arr = p[1] == '[';
      p += arr ? 2 : 1;
      p = skip_ws(p);
      const char *s = p;
      while (*p && *p != ']' && *p != '\n') p++;
      const char *e = p;
      while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
      char name[64];
      snprintf(name, sizeof name, "%.*s", (int)(e - s), s);
      for (char *r = name, *w = name;; r++) { /* quoted segments: [preset."Day & Night"] */
        if (*r != '"') *w++ = *r;
        if (!*r) break;
      }
      if (arr) {
        int i;
        for (i = 0; i < t->narrays; i++)
          if (!strcmp(t->arrays[i].name, name)) break;
        if (i == t->narrays && t->narrays < 16) {
          snprintf(t->arrays[i].name, sizeof t->arrays[i].name, "%s", name);
          t->arrays[i].count = 0;
          t->narrays++;
        }
        if (i < t->narrays) snprintf(table, sizeof table, "%s.%d", name, t->arrays[i].count++);
      } else {
        snprintf(table, sizeof table, "%s", name);
      }
      while (*p && *p != '\n') p++;
      continue;
    }
    /* key */
    char key[64];
    if (*p == '"' || *p == '\'') {
      char *k;
      p = parse_string(p, &k);
      snprintf(key, sizeof key, "%s", k);
      free(k);
    } else {
      const char *s = p;
      while (*p && *p != '=' && *p != '\n' && *p != ' ' && *p != '\t') p++;
      snprintf(key, sizeof key, "%.*s", (int)(p - s), s);
    }
    p = skip_ws(p);
    if (*p != '=') {
      while (*p && *p != '\n') p++;
      continue;
    }
    p++;
    if (t->n == t->cap) t->e = realloc(t->e, sizeof(struct entry) * (t->cap = t->cap ? t->cap * 2 : 32));
    struct entry *en = &t->e[t->n++];
    snprintf(en->table, sizeof en->table, "%s", table);
    snprintf(en->key, sizeof en->key, "%s", key);
    p = parse_value(p, &en->val);
    while (*p && *p != '\n') p++;
  }
  return t;
}

struct lw_toml *lw_toml_load(const char *path) {
  char *text = lw_read_file(path, NULL);
  if (!text) return NULL;
  struct lw_toml *t = lw_toml_parse(text);
  free(text);
  return t;
}

void lw_toml_free(struct lw_toml *t) {
  if (!t) return;
  for (int i = 0; i < t->n; i++) val_free(&t->e[i].val);
  free(t->e);
  free(t);
}

const struct lw_tval *lw_toml_get(const struct lw_toml *t, const char *table, const char *key) {
  if (!t) return NULL;
  for (int i = t->n - 1; i >= 0; i--)
    if (!strcmp(t->e[i].key, key) && !strcmp(t->e[i].table, table)) return &t->e[i].val;
  return NULL;
}

const char *lw_toml_str(const struct lw_toml *t, const char *table, const char *key, const char *def) {
  const struct lw_tval *v = lw_toml_get(t, table, key);
  return v && v->type == LW_T_STR ? v->s : def;
}

double lw_toml_num(const struct lw_toml *t, const char *table, const char *key, double def) {
  const struct lw_tval *v = lw_toml_get(t, table, key);
  return v && (v->type == LW_T_NUM || v->type == LW_T_BOOL) ? v->n : def;
}

bool lw_toml_bool(const struct lw_toml *t, const char *table, const char *key, bool def) {
  const struct lw_tval *v = lw_toml_get(t, table, key);
  return v && (v->type == LW_T_NUM || v->type == LW_T_BOOL) ? v->n != 0 : def;
}

int lw_toml_count(const struct lw_toml *t, const char *array_table) {
  if (!t) return 0;
  for (int i = 0; i < t->narrays; i++)
    if (!strcmp(t->arrays[i].name, array_table)) return t->arrays[i].count;
  return 0;
}

const char *lw_toml_table(const struct lw_toml *t, int idx) {
  if (!t) return NULL;
  for (int i = 0; i < t->n; i++) {
    bool first = true;
    for (int j = 0; j < i && first; j++) first = strcmp(t->e[j].table, t->e[i].table) != 0;
    if (first && idx-- == 0) return t->e[i].table;
  }
  return NULL;
}

bool lw_toml_each(const struct lw_toml *t, const char *table, int idx, const char **key, const struct lw_tval **val) {
  if (!t) return false;
  for (int i = 0; i < t->n; i++) {
    if (strcmp(t->e[i].table, table)) continue;
    if (idx-- == 0) {
      *key = t->e[i].key;
      *val = &t->e[i].val;
      return true;
    }
  }
  return false;
}
