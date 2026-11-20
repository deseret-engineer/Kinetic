/* Kinetic's Hyprland plugin (modules/glass/plugin) draws the glass behind
 * windows and the selection light inside Hyprland's own frame, where each
 * window is exactly where Hyprland draws it. The engine keeps their options
 * (Glass and Light, in wall.toml) and sends them on Hyprland's socket:
 *
 *   kinetic-glass set glass=see-through apps=terminal refract=18 ... light=focused color=r,g,b ...
 *
 * whenever they or the theme change, when Hyprland connects, and when the plugin
 * says it was (re)loaded (its `kinetic-glass>>ready` event). A line that has not
 * changed is not sent again.
 *
 * While window glass is on its frost replaces Hyprland's blur, so Hyprland's
 * blur is switched off (kinetic-window-blur, beside this binary, writes the
 * toggle), unless window_hypr_blur keeps it.
 *
 * A light that reacts to sound reads the engine's level from a small shared file
 * ($XDG_RUNTIME_DIR/kinetic/light-level: two floats, energy and onset), written
 * as the engine reads its audio: no messages per frame. */
#include "lw.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static char sent[2048];   /* the last line sent */
static bool loaded;       /* the plugin answered the last line */
static char blur_state[8]; /* what kinetic-window-blur was last told */

static void theme_color(const char *name, float out[3]) {
  const struct lw_theme *t = &E.sys.theme_to; /* where a theme change is going: no stream of lines while it fades */
  static const char *names[] = {"red", "green", "yellow", "blue", "magenta", "cyan"};
  memcpy(out, t->accent, sizeof(float) * 3);
  if (!strcmp(name, "foreground")) memcpy(out, t->fg, sizeof(float) * 3);
  for (int i = 0; i < 6; i++)
    if (!strcmp(name, names[i])) memcpy(out, t->palette[i + 1], sizeof(float) * 3);
}

static void blur_sync(const char *want) {
  if (!strcmp(blur_state, want)) return;
  snprintf(blur_state, sizeof blur_state, "%s", want);
  char exe[LW_PATH], path[LW_PATH + 32];
  const char *bin = "kinetic-window-blur";
  ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
  if (n > 0) {
    exe[n] = 0;
    char *slash = strrchr(exe, '/');
    if (slash) {
      *slash = 0;
      snprintf(path, sizeof path, "%s/kinetic-window-blur", exe);
      if (!access(path, X_OK)) bin = path;
    }
  }
  char *argv[] = {(char *)bin, (char *)want, NULL};
  lw_spawn(argv);
}

/* The apps list goes as one word: spaces become commas. */
static void apps_word(char *out, size_t len) {
  size_t n = 0;
  for (const char *s = E.cfg.window_glass_apps; *s && n + 1 < len; s++)
    if (*s == ' ' || *s == ',') {
      if (n && out[n - 1] != ',') out[n++] = ',';
    } else if ((unsigned char)*s > ' ' && *s != ';')
      out[n++] = *s;
  if (n && out[n - 1] == ',') n--;
  out[n] = 0;
}

void lw_plugin_sync(bool force) {
  if (!lw_hypr_connected()) return;
  const struct lw_config *c = &E.cfg;
  const struct lw_theme *t = &E.sys.theme_to;
  bool glass = strcmp(c->window_glass, "off") != 0;
  blur_sync(glass && !c->window_hypr_blur ? "on" : "off");

  char apps[160], line[sizeof sent];
  apps_word(apps, sizeof apps);
  float col[3];
  theme_color(c->light_color, col);
  char pal[256];
  size_t pn = 0;
  for (int i = 0; i < 6; i++)
    pn += (size_t)snprintf(pal + pn, sizeof pal - pn, "%s%.3f,%.3f,%.3f", i ? ";" : "", t->palette[i + 1][0], t->palette[i + 1][1], t->palette[i + 1][2]);
  snprintf(line, sizeof line,
           "kinetic-glass set glass=%s apps=%s refract=%g bevel=%g curve=%g frost=%g disp=%g sat=%g bright=%g tint=%g "
           "tintrgb=%.3f,%.3f,%.3f spec=%g angle=%g "
           "light=%s anim=%s color=%.3f,%.3f,%.3f rainbow=%d pal=%s strength=%g unfocused=%g outside=%g inside=%g edge=%g "
           "softness=%g speed=%g sound=%g urgent=%d urgentrgb=%.3f,%.3f,%.3f fade=%g",
           c->window_glass, apps[0] ? apps : ",", c->window_refract, c->window_bevel, c->window_curve, c->window_frost,
           c->window_dispersion, c->window_saturation, c->window_brightness, c->window_tint, t->bg[0], t->bg[1], t->bg[2],
           c->window_specular, c->window_light, c->light_mode, c->light_animation, col[0], col[1], col[2],
           !strcmp(c->light_color, "rainbow"), pal, c->light_strength, c->light_unfocused, c->light_outside, c->light_inside,
           c->light_edge, c->light_softness, c->light_speed, c->light_sound, c->light_urgent, t->palette[1][0],
           t->palette[1][1], t->palette[1][2], c->light_fade);
  if (!force && !strcmp(line, sent)) return;
  char *r = lw_hypr_request(line);
  bool was = loaded;
  loaded = r && !strncmp(r, "ok", 2);
  if (loaded) snprintf(sent, sizeof sent, "%s", line);
  else sent[0] = 0; /* try again next time */
  if (!loaded && (glass || strcmp(c->light_mode, "off")) && (was || force))
    lw_log("plugin: Kinetic's Hyprland plugin did not take the settings (%s): window glass and the light are off until it is loaded "
           "(`kinetic glass plugin`)", r && *r ? strtok(r, "\n") : "no answer");
  free(r);
}

void lw_plugin_off(void) {
  if (blur_state[0] && strcmp(blur_state, "off")) blur_sync("off");
  if (!loaded || !lw_hypr_connected()) return;
  free(lw_hypr_request("kinetic-glass set glass=off light=off"));
  loaded = false;
  sent[0] = 0;
}

int lw_plugin_status(char *out, size_t len) { return snprintf(out, len, "%s", loaded ? "loaded" : "missing"); }

/* ---------- the sound level ---------- */
static float *level;

void lw_plugin_sound(void) {
  bool want = E.cfg.light_sound > 0 && strcmp(E.cfg.light_mode, "off");
  if (!level) {
    if (!want) return;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt) return;
    char path[LW_PATH];
    snprintf(path, sizeof path, "%s/kinetic", rt);
    lw_mkdir_p(path);
    snprintf(path, sizeof path, "%s/kinetic/light-level", rt);
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return;
    if (ftruncate(fd, 8) < 0) {
      close(fd);
      return;
    }
    void *p = mmap(NULL, 8, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return;
    level = p;
  }
  level[0] = want ? E.sys.sound_energy : 0;
  level[1] = want ? E.sys.kick_strength : 0;
}
