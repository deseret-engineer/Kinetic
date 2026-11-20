/* What Hyprland stacks over Kinetic's wallpaper layer on one output, read from
 * `j/layers`. Hyprland keeps each level's layers in a list, drawn in order (the
 * last one on top), and hyprctl prints them in that order. A layer joins the end
 * of its level's list when it is created or moves to another level, so whoever
 * came last is on top. Pure: tests/layers_test.c runs it on canned replies. */
#include "lw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Levels: 0 background, 1 bottom, 2 top, 3 overlay. A wallpaper is covered by what
 * is above it on its own level, and by anything on the bottom level when it sits
 * on the background one. Bars and notifications live on top and overlay. */
bool lw_layers_scan(const char *json, const char *output, int pid, struct lw_layer_scan *s) {
  memset(s, 0, sizeof *s);
  s->level = -1;
  if (!json) return false;
  const char *mons = strchr(json, '{'), *it = mons, *mon;
  char name[64], lvl[8];
  while ((mon = lw_json_member(&it, name, sizeof name))) {
    if (strcmp(name, output) || *mon != '{') continue;
    const char *levels = lw_json_get(mon, "levels");
    if (!levels || *levels != '{') return false;
    /* two passes: find our layer first, then judge everything against it */
    for (int pass = 0; pass < 2; pass++) {
      const char *li = levels, *list;
      while ((list = lw_json_member(&li, lvl, sizeof lvl))) {
        int level = atoi(lvl), index = 0;
        const char *ii = list, *ls;
        while (list && *list == '[' && (ls = lw_json_item(&ii))) {
          char ns[64];
          const char *w = lw_json_get(ls, "w"), *h = lw_json_get(ls, "h"), *p = lw_json_get(ls, "pid");
          lw_json_str(lw_json_get(ls, "namespace"), ns, sizeof ns);
          int my = index++;
          bool shown = w && h && atoi(w) > 0 && atoi(h) > 0; /* an unmapped layer has no size yet */
          bool ours = !strcmp(ns, "kinetic-wall") && p && atoi(p) == pid;
          if (pass == 0) {
            if (ours) s->level = level, s->index = my, s->ours = true;
            continue;
          }
          if (ours || !shown) continue;
          bool above = s->ours && ((level == s->level && my > s->index) || (s->level == 0 && level == 1));
          bool shell = !strcmp(ns, "omarchy-background"), owe = !strcmp(ns, "owe-background");
          if (shell && level == 0) {
            s->shell_bg = true; /* the shell's wallpaper: costs its frames even when covered */
            s->shell_above |= above;
          } else if (shell) {
            s->shell_cover = true; /* the shell's startup or theme cover (bottom or overlay): it fades by itself */
          } else if (owe && above) {
            s->owe_above = true;
          } else if (above && !s->other_above[0]) {
            snprintf(s->other_above, sizeof s->other_above, "%s", ns);
          }
        }
      }
    }
    return true;
  }
  return false;
}
