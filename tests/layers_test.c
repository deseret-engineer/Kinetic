/* What covers Kinetic's wallpaper, read from Hyprland's `j/layers`: the shell's
 * background and covers, owe's layer, and the order they are stacked in. The
 * replies are shaped like Hyprland 0.56's. Build and run: tests/run.sh */
#include "../modules/wall/src/lw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../modules/wall/src/json.c"
#include "../modules/wall/src/layers.c"

/* one layer entry, as hyprctl prints it */
static char *layer(char *buf, const char *ns, int pid, int w, int h) {
  sprintf(buf, "{\n \"address\": \"0x%x\",\n \"x\": 0,\n \"y\": 0,\n \"w\": %d,\n \"h\": %d,\n \"alpha\": 1,\n"
               " \"namespace\": \"%s\",\n \"pid\": %d\n}", pid, w, h, ns, pid);
  return buf;
}

static const char *K = "kinetic-wall", *SHELL = "omarchy-background", *OWE = "owe-background";

/* levels: up to three layers per level, NULL-terminated name lists */
static char json[16384];
static void build(const char *out, const char *lv[4][4], const int pid[4][4], const int w[4][4]) {
  char *p = json, one[512];
  p += sprintf(p, "{\n\"HDMI-A-1\": {\n \"levels\": {\n \"0\": [\n]\n }\n},\n\"%s\": {\n \"levels\": {\n", out);
  for (int l = 0; l < 4; l++) {
    p += sprintf(p, "%s \"%d\": [\n", l ? ",\n" : "", l);
    for (int i = 0; i < 4 && lv[l][i]; i++)
      p += sprintf(p, "%s%s", i ? ",\n" : "", layer(one, lv[l][i], pid[l][i], w[l][i] ? w[l][i] : 1920, 1200));
    p += sprintf(p, "\n]");
  }
  sprintf(p, "\n }\n}\n}\n");
}

static struct lw_layer_scan scan(const char *lv[4][4], const int pid[4][4], const int w[4][4]) {
  struct lw_layer_scan s;
  build("eDP-1", lv, pid, w);
  assert(lw_layers_scan(json, "eDP-1", 100, &s));
  return s;
}

int main(void) {
  static const int none[4][4];
  struct lw_layer_scan s;

  /* settled: the shell's background was there first, Kinetic's above it, the bar on top */
  {
    const char *lv[4][4] = {{SHELL, K}, {0}, {"omarchy-bar"}, {"omarchy-notifications"}};
    const int pid[4][4] = {{300, 100}, {0}, {300}, {300}};
    s = scan(lv, pid, none);
    assert(s.ours && s.level == 0 && s.index == 1);
    assert(s.shell_bg && !s.shell_above && !s.shell_cover && !s.owe_above && !s.other_above[0]);
  }
  /* login, the intro over: owe's layer came after Kinetic's, the shell's background after both */
  {
    const char *lv[4][4] = {{K, OWE, SHELL}, {0}, {"omarchy-bar"}, {0}};
    const int pid[4][4] = {{100, 200, 300}, {0}, {300}, {0}};
    s = scan(lv, pid, none);
    assert(s.ours && s.index == 0 && s.shell_bg && s.shell_above && s.owe_above && !s.shell_cover);
  }
  /* login, the black startup cover on the overlay level: wait for it */
  {
    const char *lv[4][4] = {{K}, {0}, {0}, {SHELL}};
    const int pid[4][4] = {{100}, {0}, {0}, {300}};
    s = scan(lv, pid, none);
    assert(s.ours && s.shell_cover && !s.shell_bg && !s.shell_above);
  }
  /* a theme change or a shell restart: the cover on the bottom level, over Kinetic's background level */
  {
    const char *lv[4][4] = {{K}, {SHELL}, {0}, {0}};
    const int pid[4][4] = {{100}, {300}, {0}, {0}};
    s = scan(lv, pid, none);
    assert(s.shell_cover && !s.shell_bg);
  }
  /* something else on the bottom level covers a background-level wallpaper (for the log) */
  {
    const char *lv[4][4] = {{K}, {"desktop-widgets"}, {0}, {0}};
    const int pid[4][4] = {{100}, {400}, {0}, {0}};
    s = scan(lv, pid, none);
    assert(!strcmp(s.other_above, "desktop-widgets") && !s.owe_above && !s.shell_cover);
  }
  /* owe's surface exists but is not mapped yet (no size): nothing covers Kinetic */
  {
    const char *lv[4][4] = {{K, OWE}, {0}, {0}, {0}};
    const int pid[4][4] = {{100, 200}, {0}, {0}, {0}};
    const int w[4][4] = {{0, -1}}; /* 0: full size, -1: no size yet */
    s = scan(lv, pid, w);
    assert(s.ours && !s.owe_above);
  }
  /* raised: Kinetic's layer is last on its level again */
  {
    const char *lv[4][4] = {{OWE, K}, {0}, {0}, {0}};
    const int pid[4][4] = {{200, 100}, {0}, {0}, {0}};
    s = scan(lv, pid, none);
    assert(s.ours && s.index == 1 && !s.owe_above);
  }
  /* layer = "bottom": the background level is under Kinetic, whatever maps there */
  {
    const char *lv[4][4] = {{OWE, SHELL}, {K}, {0}, {0}};
    const int pid[4][4] = {{200, 300}, {100}, {0}, {0}};
    s = scan(lv, pid, none);
    assert(s.ours && s.level == 1 && s.shell_bg && !s.shell_above && !s.owe_above);
  }
  /* another engine's layer (another pid) is not ours */
  {
    const char *lv[4][4] = {{K, SHELL}, {0}, {0}, {0}};
    const int pid[4][4] = {{999, 300}, {0}, {0}, {0}};
    s = scan(lv, pid, none);
    assert(!s.ours && s.shell_bg && !s.shell_above);
  }
  /* an output Hyprland does not list, and no reply at all */
  {
    const char *lv[4][4] = {{K}, {0}, {0}, {0}};
    const int pid[4][4] = {{100}, {0}, {0}, {0}};
    build("eDP-1", lv, pid, none);
    assert(!lw_layers_scan(json, "DP-3", 100, &s) && !s.ours);
    assert(!lw_layers_scan(NULL, "eDP-1", 100, &s));
    assert(!lw_layers_scan("{}", "eDP-1", 100, &s));
  }
  /* the walkers on their own: members and items, nested values skipped whole */
  {
    const char *it = "{ \"a\": {\"x\": [1, {\"y\": \"}\"}]}, \"b\": [10, 20, 30], \"c\": \"s\" }", *v;
    char key[8];
    int n = 0;
    while ((v = lw_json_member(&it, key, sizeof key))) {
      n++;
      if (!strcmp(key, "b")) {
        const char *ii = v, *x;
        int sum = 0;
        while ((x = lw_json_item(&ii))) sum += atoi(x);
        assert(sum == 60);
      }
    }
    assert(n == 3);
    const char *empty = "[ ]";
    assert(!lw_json_item(&empty));
  }
  puts("layers: the shell's background and covers, owe's layer and the stacking order are read right");
  return 0;
}
