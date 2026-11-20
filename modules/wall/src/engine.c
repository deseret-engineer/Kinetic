/* Engine core: event loop, config, persisted state, and the policy that decides
 * which wallpaper runs where, how fast, and when it should not run at all. */
#include "lw.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/timerfd.h>
#include <unistd.h>

struct lw_engine E;

/* ---------- loop ---------- */
static struct { int fd; lw_fd_cb cb; void *data; } handlers[48];
static int nhandlers;

void lw_loop_add(int fd, lw_fd_cb cb, void *data) {
  if (nhandlers == 48) return;
  handlers[nhandlers].fd = fd;
  handlers[nhandlers].cb = cb;
  handlers[nhandlers++].data = data;
  struct epoll_event ev = {.events = EPOLLIN, .data.fd = fd};
  epoll_ctl(E.epfd, EPOLL_CTL_ADD, fd, &ev);
}

void lw_loop_del(int fd) {
  epoll_ctl(E.epfd, EPOLL_CTL_DEL, fd, NULL);
  for (int i = 0; i < nhandlers; i++)
    if (handlers[i].fd == fd) {
      handlers[i] = handlers[--nhandlers];
      return;
    }
}

/* Called by main: dispatch one epoll event. */
void lw_loop_dispatch(int fd, uint32_t events) {
  for (int i = 0; i < nhandlers; i++)
    if (handlers[i].fd == fd) {
      handlers[i].cb(handlers[i].data, events);
      return;
    }
}

int lw_debounce_fd(void) { return timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC); }

void lw_debounce_arm(int fd, double seconds) {
  struct itimerspec its = {0};
  its.it_value.tv_sec = (time_t)seconds;
  its.it_value.tv_nsec = (long)((seconds - (time_t)seconds) * 1e9);
  timerfd_settime(fd, 0, &its, NULL);
}

/* ---------- config ---------- */
static void cpy(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src); }

/* Every global option lives in this one table: wall.toml, the `kinetic wall config`
 * command and the settings panel are all generated from it. */
#include <stddef.h>
struct opt {
  const char *key, *group, *label, *desc;
  char type; /* f = number, i = whole number, b = switch, c = choice, s = text */
  size_t off;
  double def, min, max, step;
  const char *sdef, *options; /* choices, separated by | */
};
#define O(field) offsetof(struct lw_config, field)
static const struct opt opts[] = {
    {"fps", "Performance", "Frame rate cap", "Upper limit for every wallpaper", 'i', O(fps), 60, 1, 240, 1},
    {"fps_occupied", "Performance", "Frame rate with windows open", "Cap while the visible workspace has windows; 0 turns it off", 'i', O(fps_occupied), 30, 0, 240, 1},
    {"render_scale", "Performance", "Resolution scale", "Multiplies every wallpaper's own render scale", 'f', O(render_scale), 1, 0.25, 1, 0.05},
    {"adaptive", "Performance", "Adaptive resolution", "Lower the resolution when a frame goes over the GPU budget", 'b', O(adaptive), 1},
    {"fps_monitor", "Performance", "FPS monitor", "Show frame rate, frame times, GPU and CPU cost in a corner", 'c', O(fps_monitor), 0, 0, 0, 0, "off", "off|top-left|top-right|bottom-left|bottom-right"},
    {"gpu_budget_ms", "Performance", "GPU budget per frame (ms)", "Target used by adaptive resolution", 'f', O(gpu_budget_ms), 4, 1, 16, 0.5},
    {"time_scale", "Appearance", "Animation speed", "Global multiplier on every wallpaper's clock", 'f', O(time_scale), 1, 0, 4, 0.05},
    {"dim", "Appearance", "Dim", "Darken the wallpaper so windows stand out", 'f', O(dim), 0, 0, 0.9, 0.05},
    {"transition_ms", "Appearance", "Wallpaper crossfade (ms)", "0 switches instantly", 'i', O(transition_ms), 700, 0, 3000, 50},
    {"theme_fade_ms", "Appearance", "Theme color fade (ms)", "How long colors take to follow a theme switch", 'i', O(theme_fade_ms), 800, 0, 3000, 50},
    {"sound_react", "Sound", "React to sound", "Wallpapers move with whatever the system plays: music, video, notification sounds. Each wallpaper can override this", 'b', O(sound_react), 0},
    {"sound_strength", "Sound", "Reaction strength", "", 'f', O(sound_strength), 1, 0.1, 3, 0.05},
    {"audio_gain", "Sound", "Audio sensitivity", "Gain applied to the captured sound before anything reacts to it", 'f', O(audio_gain), 1, 0.2, 5, 0.1},
    {"glass_still", "Glass", "Glass on Omarchy's background", "With the live wallpaper off, Kinetic shows your Omarchy background itself (it never animates and costs nothing at rest), so the glass under the bar and windows can still bend it. A video background goes back to Omarchy's player", 'b', O(glass_still), 1},
    {"window_glass", "Glass", "Glass behind windows", "Bend what is behind windows you can see through: see-through ones only (Hyprland opacity under 95%, or an app below), floating ones, or all. Drawn by Kinetic's Hyprland plugin in the same frame as the window, so it moves exactly with it", 'c', O(window_glass), 0, 0, 0, 0, "off", "off|see-through|floating|all"},
    {"window_glass_apps", "Glass", "See-through apps", "Apps whose own config makes them see-through, which Hyprland cannot tell: classes (a trailing * matches a prefix) or Hyprland tags, separated by spaces. Omarchy's glassy windows make every terminal see-through, and tags them all \"terminal\"", 's', O(window_glass_apps), 0, 0, 0, 0, "terminal", NULL},
    {"window_hypr_blur", "Glass", "Hyprland's blur too", "Off: while window glass is on, its frost replaces Hyprland's blur everywhere, so the bend at the rim stays sharp and nothing is blurred twice. On: Hyprland blurs too, softer and costlier", 'b', O(window_hypr_blur), 0},
    {"window_refract", "Glass", "Refraction (px)", "How far the glass bends the wallpaper at its rim", 'f', O(window_refract), 18, 0, 60, 1},
    {"window_bevel", "Glass", "Window bevel (px)", "How far in from a window's rim its glass curves; 0 follows the window's size", 'f', O(window_bevel), 36, 0, 160, 2},
    {"window_curve", "Glass", "Edge profile", "Under 1 a soft slope, 1 a rounded edge, over 1 a sharp rim with a flat middle", 'f', O(window_curve), 1, 0.3, 4, 0.05},
    {"window_frost", "Glass", "Frost", "Blur of what shows through; 0 is clear glass", 'f', O(window_frost), 2, 0, 6, 0.25},
    {"window_dispersion", "Glass", "Dispersion", "Color fringes where the glass bends most", 'f', O(window_dispersion), 0.25, 0, 1, 0.05},
    {"window_saturation", "Glass", "Saturation", "Color of what shows through the glass", 'f', O(window_saturation), 1.1, 0, 2, 0.05},
    {"window_brightness", "Glass", "Brightness", "Lighten or darken what shows through", 'f', O(window_brightness), 0, -0.5, 0.5, 0.02},
    {"window_tint", "Glass", "Window tint", "Mix the windows' glass toward the theme's background color, so text stays readable over a busy wallpaper", 'f', O(window_tint), 0.15, 0, 1, 0.05},
    {"window_specular", "Glass", "Rim light", "Light catching the curved rim", 'f', O(window_specular), 0.3, 0, 1, 0.05},
    {"window_light", "Glass", "Light angle (°)", "Where the rim light comes from: 0 the right, 90 the top", 'f', O(window_light), 120, 0, 360, 5},
    {"light_mode", "Light", "Selection light", "A soft glow around the focused window (or every window), drawn under the windows by Kinetic's Hyprland plugin: it shows through transparent windows and spills into the gaps", 'c', O(light_mode), 0, 0, 0, 0, "off", "off|focused|all"},
    {"light_color", "Light", "Color", "A theme color, or rainbow: the theme's palette flowing around the window", 'c', O(light_color), 0, 0, 0, 0, "accent", "accent|foreground|red|green|yellow|blue|magenta|cyan|rainbow"},
    {"light_strength", "Light", "Strength", "", 'f', O(light_strength), 0.45, 0, 1.5, 0.05},
    {"light_unfocused", "Light", "Unfocused windows", "With every window lit: how bright the ones without focus are, against the focused one", 'f', O(light_unfocused), 0.3, 0, 1, 0.05},
    {"light_outside", "Light", "Glow outside (px)", "How far the light spills into the gaps around the window", 'f', O(light_outside), 26, 0, 160, 2},
    {"light_inside", "Light", "Glow inside (px)", "How far in from the edge the light reaches, seen through a transparent window", 'f', O(light_inside), 80, 0, 400, 5},
    {"light_edge", "Light", "Edge line (px)", "A thin bright line right at the window's edge; 0 for none", 'f', O(light_edge), 1.5, 0, 8, 0.5},
    {"light_softness", "Light", "Falloff", "How the light fades: under 1 a long soft tail, over 1 a tighter halo", 'f', O(light_softness), 1, 0.3, 3, 0.05},
    {"light_animation", "Light", "Animation", "Breathe: a slow pulse. Flow: color traveling around the rim. Shimmer: a light running along the edge", 'c', O(light_animation), 0, 0, 0, 0, "none", "none|breathe|flow|shimmer"},
    {"light_speed", "Light", "Animation speed", "", 'f', O(light_speed), 1, 0.1, 4, 0.1},
    {"light_sound", "Light", "React to sound", "Brighten with whatever is playing; 0 for not at all", 'f', O(light_sound), 0, 0, 2, 0.1},
    {"light_urgent", "Light", "Attention", "A window asking for attention pulses until it is focused", 'b', O(light_urgent), 1},
    {"light_fade", "Light", "Fade (ms)", "How long the light takes to move to a newly focused window", 'f', O(light_fade), 260, 0, 1500, 10},
    {"pause_fullscreen", "Behavior", "Pause under fullscreen windows", "", 'b', O(pause_fullscreen), 1},
    {"pause_occupied", "Behavior", "Pause while windows are open", "Freeze whenever the visible workspace has any window", 'b', O(pause_occupied), 0},
    {"pointer", "Behavior", "Pointer interaction", "Let wallpapers that ask for it react to the mouse", 'b', O(pointer), 1},
    {"suspend_shell", "Behavior", "Suspend Omarchy's background", "Hide the shell's own background layer while Kinetic shows a live wallpaper", 'b', O(suspend_shell), 1},
    {"hot_reload", "Behavior", "Hot reload", "Recompile a wallpaper when its files are saved", 'b', O(hot_reload), 1},
    {"layer", "Behavior", "Layer", "Which shell layer the wallpaper sits on", 'c', O(layer), 0, 0, 0, 0, "background", "background|bottom"},
    {"battery.mode", "Battery", "On battery", "Keep playing, throttle, or pause", 'c', O(battery_mode), 0, 0, 0, 0, "throttle", "play|throttle|pause"},
    {"battery.fps", "Battery", "Throttled frame rate", "", 'i', O(battery_fps), 30, 1, 120, 1},
    {"battery.scale", "Battery", "Throttled resolution scale", "", 'f', O(battery_scale), 0.75, 0.25, 1, 0.05},
    {"battery.pause_below", "Battery", "Pause below charge (%)", "", 'i', O(battery_pause_below), 10, 0, 50, 1},
    {"shuffle_minutes", "Rotation", "Change wallpaper every (minutes)", "0 keeps the wallpaper you chose", 'i', O(shuffle_minutes), 0, 0, 240, 1},
    {"shuffle_order", "Rotation", "Order", "", 'c', O(shuffle_order), 0, 0, 0, 0, "random", "next|random"},
};
#undef O
#define NOPTS ((int)(sizeof opts / sizeof opts[0]))

static bool choice_ok(const struct opt *o, const char *v) {
  size_t len = strlen(v);
  for (const char *p = o->options; p && *p;) {
    const char *e = strchr(p, '|');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n == len && !strncmp(p, v, n)) return true;
    p = e ? e + 1 : NULL;
  }
  return false;
}

#define OPT_TEXT 128 /* the longest value, and the size of a text option's field */

/* Parses and stores one value; false if it is not acceptable for this option. */
static bool opt_assign(const struct opt *o, const char *v) {
  void *dst = (char *)&E.cfg + o->off;
  if (o->type == 'c') {
    if (!choice_ok(o, v)) return false;
    snprintf(dst, 16, "%s", v);
  } else if (o->type == 's') { /* words: letters, digits, . _ - * and spaces or commas between them */
    if (strlen(v) >= OPT_TEXT || v[strspn(v, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-* ,")]) return false;
    snprintf(dst, OPT_TEXT, "%s", strcmp(v, "none") ? v : "");
  } else if (o->type == 'b') {
    *(bool *)dst = !strcmp(v, "true") || !strcmp(v, "on") || strtod(v, NULL) != 0;
  } else {
    char *end;
    double n = strtod(v, &end);
    if (end == v) return false;
    if (o->type == 'i') n = round(n);
    *(float *)dst = lw_clampf((float)n, (float)o->min, (float)o->max);
  }
  return true;
}

static void opt_string(const struct opt *o, char out[OPT_TEXT]) {
  const void *src = (const char *)&E.cfg + o->off;
  if (o->type == 's')
    snprintf(out, OPT_TEXT, "%s", *(const char *)src ? (const char *)src : "none");
  else if (o->type == 'c')
    snprintf(out, 32, "%s", (const char *)src);
  else if (o->type == 'b')
    snprintf(out, 32, "%s", *(const bool *)src ? "true" : "false");
  else
    snprintf(out, 32, "%g", *(const float *)src);
}

static void shuffle_arm(void);

/* Precedence: built-in default < wall.toml < values changed live (kept in the state file). */
void lw_config_load(void) {
  struct lw_config *c = &E.cfg;
  char path[LW_PATH], buf[160];
  lw_dir_config(path, "wall.toml");
  lw_toml_free(c->doc);
  struct lw_toml *t = c->doc = lw_toml_load(path);
  cpy(c->wallpaper, sizeof c->wallpaper, lw_toml_str(t, "", "wallpaper", "aurora"));
  for (int i = 0; i < NOPTS; i++) {
    const struct opt *o = &opts[i];
    const char *dot = strchr(o->key, '.');
    char table[32] = "";
    if (dot) snprintf(table, sizeof table, "%.*s", (int)(dot - o->key), o->key);
    const struct lw_tval *v = lw_toml_get(t, table, dot ? dot + 1 : o->key);
    if (o->type == 'c' || o->type == 's')
      snprintf(buf, sizeof buf, "%s", o->sdef);
    else
      snprintf(buf, sizeof buf, "%g", o->def);
    opt_assign(o, buf);
    if (v) {
      if (v->type == LW_T_STR)
        snprintf(buf, sizeof buf, "%s", v->s);
      else
        snprintf(buf, sizeof buf, "%g", v->n);
      if (!opt_assign(o, buf)) lw_log("wall.toml: '%s' is not a valid value for %s", buf, o->key);
    }
    snprintf(buf, sizeof buf, "cfg.%s", o->key);
    const char *live = lw_state_get(buf);
    if (live) opt_assign(o, live);
  }
  if (E.timerfd > 0) shuffle_arm();
}

bool lw_config_set(const char *key, const char *value, char *err, size_t errlen) {
  for (int i = 0; i < NOPTS; i++) {
    const struct opt *o = &opts[i];
    if (strcmp(o->key, key)) continue;
    char skey[96];
    snprintf(skey, sizeof skey, "cfg.%s", key);
    if (value && !opt_assign(o, value)) {
      snprintf(err, errlen, "'%s' is not a valid value for %s%s%s%s", value, key, o->options ? " (one of " : "",
               o->options ? o->options : "", o->options ? ")" : "");
      return false;
    }
    char norm[OPT_TEXT];
    opt_string(o, norm);
    lw_state_set(skey, value ? norm : NULL);
    lw_state_save();
    lw_config_load();
    lw_engine_config_changed();
    return true;
  }
  snprintf(err, errlen, "no option named '%s' (see: kinetic wall config)", key);
  return false;
}

void lw_engine_config_changed(void) {
  lw_engine_still_update();
  lw_plugin_sync(false); /* window glass and the light: on, off or changed */
  static char layer[16];
  bool relayer = layer[0] && strcmp(layer, E.cfg.layer);
  cpy(layer, sizeof layer, E.cfg.layer);
  if (relayer && E.enabled) { /* a layer surface cannot change layer in place */
    lw_wl_map_all(false);
    lw_wl_map_all(true);
  }
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    if (o->wp) lw_engine_apply_params(o->wp);
    lw_wl_update_input(o);
  }
  if (!E.cfg.suspend_shell || !E.enabled) lw_shell_suspend(false); /* the shell's background is to stay, or Kinetic is off */
  lw_shell_check(0.2); /* and look at the layers with the new settings (once something is drawn) */
  lw_engine_policy();
  lw_engine_invalidate(NULL);
}

/* ---------- persisted state: flat "key = value" lines ---------- */
static struct { char key[128], val[128]; } state[128];
static int nstate;

const char *lw_state_get(const char *key) {
  for (int i = 0; i < nstate; i++)
    if (!strcmp(state[i].key, key)) return state[i].val;
  return NULL;
}

void lw_state_set(const char *key, const char *value) {
  for (int i = 0; i < nstate; i++)
    if (!strcmp(state[i].key, key)) {
      if (value)
        cpy(state[i].val, sizeof state[i].val, value);
      else
        state[i] = state[--nstate];
      return;
    }
  if (!value || nstate == 128) return;
  cpy(state[nstate].key, sizeof state[nstate].key, key);
  cpy(state[nstate++].val, sizeof state[0].val, value);
}

void lw_state_load(void) {
  char path[LW_PATH];
  lw_dir_state(path, "state");
  char *text = lw_read_file(path, NULL);
  nstate = 0;
  if (!text) return;
  for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
    char *eq = strstr(line, " = ");
    if (!eq) continue;
    *eq = 0;
    lw_state_set(line, eq + 3);
  }
  free(text);
}

void lw_state_save(void) {
  char dir[LW_PATH], path[LW_PATH], tmp[LW_PATH + 8];
  lw_dir_state(dir, "");
  lw_mkdir_p(dir);
  lw_dir_state(path, "state");
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "w");
  if (!f) return;
  for (int i = 0; i < nstate; i++) fprintf(f, "%s = %s\n", state[i].key, state[i].val);
  fclose(f);
  rename(tmp, path);
}

/* ---------- wallpaper selection ---------- */
const char *lw_engine_wallpaper_for(const struct lw_output *o) {
  if (E.still) return "still";
  char key[160];
  snprintf(key, sizeof key, "monitor.%s", o->name);
  const char *v = lw_state_get(key);
  if (v) return v;
  v = lw_toml_str(E.cfg.doc, "monitors", o->name, NULL);
  return v ? v : E.current;
}

static void tval_string(const struct lw_tval *v, char *out, size_t len) {
  if (v->type == LW_T_STR)
    snprintf(out, len, "%s", v->s);
  else
    snprintf(out, len, "%g", v->n);
}

/* Layers user values over the manifest: wall.toml [params.<id>], then live changes. */
void lw_engine_apply_params(struct lw_wallpaper *wp) {
  struct lw_wallpaper_info *info = lw_wallpaper_info(wp);
  char table[96], key[160], buf[64];
  snprintf(table, sizeof table, "params.%s", info->id);
  for (int i = 0; i < info->nparams; i++) {
    struct lw_param *p = &info->params[i];
    const struct lw_tval *v = lw_toml_get(E.cfg.doc, table, p->name);
    if (v) {
      tval_string(v, buf, sizeof buf);
      lw_wallpaper_set_param(wp, p->name, buf);
    }
    snprintf(key, sizeof key, "param.%s.%s", info->id, p->name);
    const char *s = lw_state_get(key);
    if (s) lw_wallpaper_set_param(wp, p->name, s);
  }
  /* per-wallpaper engine overrides */
  info->fps = info->def_fps;
  info->scale = info->def_scale;
  snprintf(key, sizeof key, "param.%s.@fps", info->id);
  const char *s = lw_state_get(key);
  if (s) info->fps = lw_clampf(strtof(s, NULL), 0, 240);
  snprintf(key, sizeof key, "param.%s.@scale", info->id);
  if ((s = lw_state_get(key))) info->scale = lw_clampf(strtof(s, NULL), 0.1f, 1);
}

bool lw_engine_param(const char *name, const char *value) {
  bool any = false;
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    if (!o->wp) continue;
    char key[160], val[32] = "";
    snprintf(key, sizeof key, "param.%s.%s", o->wp_id, name);
    if (name[0] == '@') {
      if (strcmp(name, "@fps") && strcmp(name, "@scale")) continue;
      snprintf(val, sizeof val, "%g", strtof(value, NULL));
    } else {
      if (!lw_wallpaper_set_param(o->wp, name, value)) continue;
      struct lw_wallpaper_info *info = lw_wallpaper_info(o->wp);
      for (int i = 0; i < info->nparams; i++)
        if (!strcmp(info->params[i].name, name)) lw_param_format(&info->params[i], info->params[i].value, val);
    }
    lw_state_set(key, val);
    if (name[0] == '@') lw_engine_apply_params(o->wp);
    any = true;
  }
  if (any) {
    lw_state_save();
    lw_engine_policy();
    lw_engine_invalidate(NULL);
  }
  return any;
}

void lw_engine_param_reset(void) {
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    if (!o->wp) continue;
    struct lw_wallpaper_info *info = lw_wallpaper_info(o->wp);
    char key[160];
    for (int i = 0; i < info->nparams; i++) {
      snprintf(key, sizeof key, "param.%s.%s", info->id, info->params[i].name);
      lw_state_set(key, NULL);
      memcpy(info->params[i].value, info->params[i].def, sizeof info->params[i].def);
    }
    snprintf(key, sizeof key, "param.%s.@fps", info->id);
    lw_state_set(key, NULL);
    snprintf(key, sizeof key, "param.%s.@scale", info->id);
    lw_state_set(key, NULL);
    lw_engine_apply_params(o->wp);
  }
  lw_state_save();
  lw_engine_policy();
  lw_engine_invalidate(NULL);
}

bool lw_engine_preset(const char *name) {
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    if (!o->wp) continue;
    struct lw_wallpaper_info *info = lw_wallpaper_info(o->wp);
    for (int i = 0; i < info->npresets; i++) {
      struct lw_preset pr = info->presets[i];
      if (strcasecmp(pr.name, name)) continue;
      for (int k = 0; k < pr.n; k++) lw_engine_param(pr.key[k], pr.val[k]);
      return true;
    }
  }
  return false;
}

void lw_engine_apply(void) {
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    if (!o->configured) continue;
    const char *want = lw_engine_wallpaper_for(o);
    if (o->wp && !strcmp(o->wp_id, want)) continue;
    char err[1024] = "";
    if (!lw_wl_output_load(o, want, o->wp != NULL, err, sizeof err)) {
      lw_log("%s: %s", want, err);
      snprintf(E.last_error, sizeof E.last_error, "%s", err);
      if (!o->wp && strcmp(want, "aurora") && lw_wl_output_load(o, "aurora", false, err, sizeof err))
        lw_log("%s: showing aurora instead", o->name);
      if (!o->wp && strcmp(want, "still") && lw_wl_output_load(o, "still", false, err, sizeof err))
        lw_log("%s: showing Omarchy's background instead", o->name); /* the plainest one there is */
    }
  }
  lw_engine_policy();
  lw_engine_watch_wallpaper_dirs();
}

bool lw_engine_set(const char *id, const char *monitor, char *err, size_t errlen) {
  char dir[LW_PATH];
  if (!lw_wallpaper_find(id, dir)) {
    snprintf(err, errlen, "no wallpaper named '%s'", id);
    return false;
  }
  /* Compile on every affected output first, so a broken shader changes nothing. */
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    if (!o->configured) continue;
    if (monitor && strcmp(monitor, o->name)) continue;
    char key[160];
    snprintf(key, sizeof key, "monitor.%s", o->name);
    if (!monitor && (lw_state_get(key) || lw_toml_str(E.cfg.doc, "monitors", o->name, NULL))) continue;
    if (o->wp && !strcmp(o->wp_id, id)) continue;
    if (!lw_wl_output_load(o, id, true, err, errlen)) {
      snprintf(E.last_error, sizeof E.last_error, "%s", err);
      return false;
    }
  }
  E.last_error[0] = 0;
  if (monitor) {
    char key[160];
    snprintf(key, sizeof key, "monitor.%s", monitor);
    lw_state_set(key, id);
  } else {
    cpy(E.current, sizeof E.current, id);
    lw_state_set("wallpaper", id);
  }
  lw_state_save();
  lw_engine_policy();
  lw_engine_watch_wallpaper_dirs();
  return true;
}

struct cycle {
  char ids[128][64];
  int n;
};

static void collect(const char *id, const char *dir, void *data) {
  struct cycle *c = data;
  if (c->n < 128) cpy(c->ids[c->n++], 64, id);
}

bool lw_engine_cycle(int dir, char *err, size_t errlen) {
  struct cycle c = {.n = 0};
  lw_wallpaper_each(collect, &c);
  if (!c.n) {
    snprintf(err, errlen, "no wallpapers installed");
    return false;
  }
  int cur = 0;
  for (int i = 0; i < c.n; i++)
    if (!strcmp(c.ids[i], E.current)) cur = i;
  int next = dir ? (cur + dir + c.n) % c.n : (cur + 1 + rand() % (c.n > 1 ? c.n - 1 : 1)) % c.n;
  return lw_engine_set(c.ids[next], NULL, err, errlen);
}

bool lw_engine_reload(char *err, size_t errlen) {
  bool ok = true;
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    if (!o->configured || !o->wp) continue;
    char id[64];
    cpy(id, sizeof id, o->wp_id);
    if (!lw_wl_output_load(o, id, false, err, errlen)) {
      snprintf(E.last_error, sizeof E.last_error, "%s", err);
      ok = false;
    }
  }
  if (ok) E.last_error[0] = 0;
  lw_engine_policy();
  return ok;
}

/* ---------- policy ---------- */
void lw_engine_policy(void) {
  const struct lw_config *c = &E.cfg;
  bool throttle = E.sys.on_battery && !strcmp(c->battery_mode, "throttle");
  bool want_audio = false;
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    if (!o->wp) continue;
    struct lw_wallpaper_info *info = lw_wallpaper_info(o->wp);
    const char *reason = NULL;
    if (!E.enabled) reason = "disabled";
    else if (E.manual_pause) reason = "manual";
    else if (o->ws_fullscreen && c->pause_fullscreen) reason = "fullscreen";
    else if (o->ws_windows > 0 && c->pause_occupied) reason = "occupied";
    else if (E.sys.on_battery && !strcmp(c->battery_mode, "pause")) reason = "battery";
    else if (E.sys.on_battery && E.sys.has_battery && E.sys.battery * 100 < c->battery_pause_below) reason = "battery-low";

    float fps = info->fps <= 0 ? 0 : fminf(info->fps, c->fps);
    if (o->ws_windows > 0 && c->fps_occupied > 0) fps = fminf(fps, c->fps_occupied);
    if (throttle && c->battery_fps > 0) fps = fminf(fps, c->battery_fps);

    bool was = o->paused;
    o->paused = reason != NULL;
    o->pause_reason = reason;
    o->fps = fps;
    want_audio |= (info->audio || lw_wallpaper_sound(o->wp) > 0 || (E.cfg.light_sound > 0 && strcmp(E.cfg.light_mode, "off"))) && !o->paused;
    lw_wl_resize(o); /* battery scale may have changed; a no-op otherwise */
    if (was && !o->paused) lw_engine_invalidate(o);
    else if (!o->paused) lw_wl_schedule(o);
  }
  E.sys.audio_wanted = want_audio;
  lw_audio_set_active(want_audio);
}

void lw_engine_invalidate(struct lw_output *only) {
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    if (only && o != only) continue;
    o->dirty = true;
    lw_wl_schedule(o);
  }
}

/* Off, with glass_still on and a still image for a background: Kinetic keeps
 * drawing, showing Omarchy's background itself (the Still wallpaper, which
 * redraws only on change), so the glass under the bar and the windows still has
 * pixels to bend. A video background belongs to Omarchy's own player: then
 * off hands the desktop back, as it does with glass_still off. */
static bool want_still(void) { return E.cfg.glass_still && !lw_background_is_video(); }

bool lw_engine_on(void) { return E.enabled && !E.still; }

void lw_engine_set_enabled(bool on) {
  bool still = !on && want_still(), mapped = on || still;
  if (E.enabled == mapped && E.still == still) return;
  lw_state_set("enabled", on ? "1" : "0");
  lw_state_save();
  bool remap = E.enabled != mapped, reload = !remap && E.still != still;
  E.enabled = mapped;
  E.still = still;
  if (remap) lw_wl_map_all(mapped);
  if (reload) /* stays on screen: crossfade between the live wallpaper and the still */
    for (struct lw_output *o = E.outputs; o; o = o->next) {
      char err[512];
      if (o->configured && !lw_wl_output_load(o, lw_engine_wallpaper_for(o), true, err, sizeof err)) lw_log("%s", err);
    }
  if (!mapped) lw_shell_suspend(false);
  lw_engine_policy();
  lw_engine_invalidate(NULL);
  lw_ipc_publish();
}

void lw_engine_still_update(void) {
  const char *en = lw_state_get("enabled");
  if (en && !strcmp(en, "0")) lw_engine_set_enabled(false); /* re-decides between still and handing back */
}

static void lerp3(float *o, const float *a, const float *b, float t) {
  for (int i = 0; i < 3; i++) o[i] = a[i] + (b[i] - a[i]) * t;
}

void lw_engine_theme_changed(const struct lw_theme *t, bool animate) {
  struct lw_sys *s = &E.sys;
  bool renamed = strcmp(s->theme_to.name, t->name) != 0;
  s->theme_from = s->theme;
  s->theme_to = *t;
  s->theme_t0 = lw_now();
  s->theme_animating = animate;
  if (!animate) s->theme = *t;
  s->bg_stale = true;
  lw_engine_still_update(); /* a video background ends still mode, an image one may start it */
  lw_plugin_sync(false);    /* the glass's tint and the light's colors */
  if (renamed && animate) {
    const char *mapped = lw_toml_str(E.cfg.doc, "themes", t->name, NULL);
    if (mapped && strcmp(mapped, E.current)) {
      char err[1024];
      if (!lw_engine_set(mapped, NULL, err, sizeof err)) lw_log("theme mapping %s -> %s: %s", t->name, mapped, err);
    }
  }
  lw_engine_invalidate(NULL);
}

void lw_engine_tick(void) {
  struct lw_sys *s = &E.sys;
  static double last_audio;
  double now = lw_now();
  if (s->theme_animating) {
    float t = E.cfg.theme_fade_ms > 0 ? (float)((now - s->theme_t0) * 1000.0 / E.cfg.theme_fade_ms) : 1;
    if (t >= 1) {
      t = 1;
      s->theme_animating = false;
    }
    t = t * t * (3 - 2 * t);
    lerp3(s->theme.bg, s->theme_from.bg, s->theme_to.bg, t);
    lerp3(s->theme.fg, s->theme_from.fg, s->theme_to.fg, t);
    lerp3(s->theme.accent, s->theme_from.accent, s->theme_to.accent, t);
    for (int i = 0; i < 16; i++) lerp3(s->theme.palette[i], s->theme_from.palette[i], s->theme_to.palette[i], t);
    s->theme.dark = s->theme_from.dark + (s->theme_to.dark - s->theme_from.dark) * t;
    if (!s->theme_animating) s->theme = s->theme_to;
  }
  if (s->audio_wanted && now - last_audio > 0.004) { /* once per frame even with several outputs */
    double dt = last_audio > 0 ? fmin(now - last_audio, 0.25) : 0.016;
    last_audio = now;
    lw_audio_update(s);
    /* Loudness envelope (fast attack, slow release) and onset detection: a level
     * well above its recent average, rising, counts as a kick. */
    static float avg, prev;
    float lvl = s->audio[3];
    avg += (lvl - avg) * (1 - expf(-(float)dt / 1.2f));
    float k = lvl > s->sound_energy ? 0.03f : 0.35f;
    s->sound_energy += (lvl - s->sound_energy) * (1 - expf(-(float)dt / k));
    if (lvl > avg * 1.5f + 0.03f && lvl - prev > 0.015f && now - s->kick_time > 0.2) {
      s->kick_time = now;
      s->kick_strength = lw_clampf((lvl - avg) * 4, 0.25f, 1);
      s->kicks++;
    }
    prev = lvl;
    lw_plugin_sound();
  } else if (!s->audio_wanted && s->sound_energy != 0) {
    s->sound_energy = 0;
    lw_plugin_sound();
  }
}

/* ---------- frame timer ---------- */
void lw_engine_arm_timer(void) {
  double next = 0;
  for (struct lw_output *o = E.outputs; o; o = o->next)
    if (o->due > 0 && (next == 0 || o->due < next)) next = o->due;
  struct itimerspec its = {0};
  if (next > 0) {
    if (next < 1e-6) next = 1e-6;
    its.it_value.tv_sec = (time_t)next;
    its.it_value.tv_nsec = (long)((next - (time_t)next) * 1e9);
  }
  timerfd_settime(E.timerfd, TFD_TIMER_ABSTIME, &its, NULL);
}

void lw_engine_timer_fired(void *data, uint32_t ev) {
  uint64_t n;
  if (read(E.timerfd, &n, sizeof n) < 0 && errno != EAGAIN) return;
  double now = lw_now();
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    if (o->due > 0 && now >= o->due - 0.0005) {
      o->due = 0;
      if (!o->frame_cb && lw_wl_drawable(o)) lw_wl_render(o);
    }
  }
  lw_engine_arm_timer();
}

/* ---------- hot reload of wallpaper sources ---------- */
static int watch_fd = -1, watch_timer = -1;

static void watch_event(void *data, uint32_t ev) {
  char buf[4096];
  while (read(watch_fd, buf, sizeof buf) > 0) {}
  lw_debounce_arm(watch_timer, 0.12);
}

static void watch_fire(void *data, uint32_t ev) {
  uint64_t n;
  if (read(watch_timer, &n, sizeof n) < 0) return;
  char err[1024] = "";
  if (lw_engine_reload(err, sizeof err))
    lw_log("reloaded wallpaper sources");
  else
    lw_log("reload failed, keeping the running wallpaper: %s", err);
}

void lw_engine_watch_wallpaper_dirs(void) {
  if (!E.cfg.hot_reload) return;
  if (watch_fd >= 0) { /* simplest way to drop old watches */
    lw_loop_del(watch_fd);
    close(watch_fd);
  }
  watch_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (watch_fd < 0) return;
  if (watch_timer < 0) {
    watch_timer = lw_debounce_fd();
    lw_loop_add(watch_timer, watch_fire, NULL);
  }
  for (struct lw_output *o = E.outputs; o; o = o->next)
    if (o->wp) inotify_add_watch(watch_fd, lw_wallpaper_info(o->wp)->dir, IN_CLOSE_WRITE | IN_MOVED_TO);
  lw_loop_add(watch_fd, watch_event, NULL);
}

/* ---------- rotation ---------- */
static int shuffle_fd = -1;

static void shuffle_fire(void *data, uint32_t ev) {
  uint64_t n;
  char err[512];
  if (read(shuffle_fd, &n, sizeof n) < 0) return;
  if (E.enabled && !E.manual_pause) lw_engine_cycle(strcmp(E.cfg.shuffle_order, "next") ? 0 : 1, err, sizeof err);
}

static void shuffle_arm(void) {
  static float armed = -1;
  if (shuffle_fd < 0) {
    shuffle_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    lw_loop_add(shuffle_fd, shuffle_fire, NULL);
  }
  if (armed == E.cfg.shuffle_minutes) return; /* unrelated config changes must not restart the countdown */
  armed = E.cfg.shuffle_minutes;
  struct itimerspec its = {0};
  its.it_value.tv_sec = its.it_interval.tv_sec = (time_t)(E.cfg.shuffle_minutes * 60);
  timerfd_settime(shuffle_fd, 0, &its, NULL);
}

void lw_config_list(struct lw_buf *b, const char *only) {
  const char *group = "";
  char val[OPT_TEXT];
  for (int i = 0; i < NOPTS; i++) {
    const struct opt *o = &opts[i];
    if (only && strcmp(only, o->key)) continue;
    char skey[96];
    snprintf(skey, sizeof skey, "cfg.%s", o->key);
    opt_string(o, val);
    if (only) {
      lw_buf_printf(b, "%s\n", val);
      return;
    }
    if (strcmp(group, o->group)) lw_buf_printf(b, "%s[%s]\n", i ? "\n" : "", group = o->group);
    lw_buf_printf(b, "  %-22s %-10s%s  %s", o->key, val, lw_state_get(skey) ? "*" : " ", o->label);
    if (o->options) lw_buf_printf(b, " (%s)", o->options);
    else if (o->type == 's') lw_buf_printf(b, " (words, or none)");
    else if (o->type != 'b') lw_buf_printf(b, " (%g..%g)", o->min, o->max);
    lw_buf_printf(b, "\n");
  }
  if (only) lw_buf_printf(b, "error: no option named '%s'\n", only);
  else lw_buf_printf(b, "\n* changed live. `kinetic wall config <key> <value>` sets one, `... default` resets it.\n");
}

/* ---------- describe: the data behind any settings UI ---------- */
static void describe_library(const char *id, const char *dir, void *data) {
  struct lw_buf *b = data;
  struct lw_wallpaper_info info;
  char err[256];
  if (!lw_wallpaper_read_info(dir, &info, err, sizeof err)) return;
  lw_buf_printf(b, "{\"id\":");
  lw_buf_json(b, id);
  lw_buf_printf(b, ",\"name\":");
  lw_buf_json(b, info.title);
  lw_buf_printf(b, ",\"description\":");
  lw_buf_json(b, info.description);
  lw_buf_printf(b, ",\"audio\":%s,\"pointer\":%s},", info.audio ? "true" : "false", info.pointer ? "true" : "false");
}

void lw_engine_describe(struct lw_buf *b) {
  char val[OPT_TEXT];
  lw_buf_printf(b, "{\"enabled\":%s,\"still\":%s,\"paused\":%s,\"current\":", lw_engine_on() ? "true" : "false",
                E.still ? "true" : "false", E.manual_pause ? "true" : "false");
  lw_buf_json(b, E.current);
  lw_buf_printf(b, ",\"wallpaper\":");
  struct lw_output *o = E.outputs;
  while (o && !o->wp) o = o->next;
  if (!o) {
    lw_buf_printf(b, "null");
  } else {
    struct lw_wallpaper_info *info = lw_wallpaper_info(o->wp);
    lw_buf_printf(b, "{\"id\":");
    lw_buf_json(b, info->id);
    lw_buf_printf(b, ",\"name\":");
    lw_buf_json(b, info->title);
    lw_buf_printf(b, ",\"description\":");
    lw_buf_json(b, info->description);
    lw_buf_printf(b, ",\"fps\":%g,\"fps_default\":%g,\"scale\":%g,\"scale_default\":%g,\"gpu_ms\":%.2f,\"state\":\"%s\",\"params\":[",
                  info->fps, info->def_fps, info->scale, info->def_scale, o->gpu_ms, o->paused ? o->pause_reason : "running");
    static const char *types[] = {"float", "int", "bool", "choice", "bits", "color"};
    for (int i = 0; i < info->nparams; i++) {
      struct lw_param *p = &info->params[i];
      lw_buf_printf(b, "%s{\"name\":\"%s\",\"type\":\"%s\",\"label\":", i ? "," : "", p->name, types[p->type]);
      lw_buf_json(b, p->label);
      lw_buf_printf(b, ",\"description\":");
      lw_buf_json(b, p->desc);
      lw_param_format(p, p->value, val);
      lw_buf_printf(b, ",\"value\":%s%s%s", p->type == LW_P_COLOR ? "\"" : "", val, p->type == LW_P_COLOR ? "\"" : "");
      lw_param_format(p, p->def, val);
      lw_buf_printf(b, ",\"default\":%s%s%s", p->type == LW_P_COLOR ? "\"" : "", val, p->type == LW_P_COLOR ? "\"" : "");
      lw_buf_printf(b, ",\"min\":%g,\"max\":%g,\"step\":%g,\"bits\":%d,\"options\":[", p->min, p->max, p->step, p->bits);
      for (int k = 0; k < p->nopt; k++) {
        if (k) lw_buf_printf(b, ",");
        lw_buf_json(b, p->options[k]);
      }
      lw_buf_printf(b, "]}");
    }
    lw_buf_printf(b, "],\"presets\":[");
    for (int i = 0; i < info->npresets; i++) {
      if (i) lw_buf_printf(b, ",");
      lw_buf_json(b, info->presets[i].name);
    }
    lw_buf_printf(b, "]}");
  }
  lw_buf_printf(b, ",\"theme\":");
  lw_buf_json(b, E.sys.theme_to.name);
  lw_buf_printf(b, ",\"palette\":[");
  for (int i = 0; i < 16; i++) {
    const float *c = E.sys.theme_to.palette[i];
    lw_buf_printf(b, "%s\"#%02x%02x%02x\"", i ? "," : "", (int)(c[0] * 255 + .5f), (int)(c[1] * 255 + .5f), (int)(c[2] * 255 + .5f));
  }
  char thumbs[LW_PATH];
  lw_dir_cache(thumbs, "thumbs");
  lw_buf_printf(b, "],\"thumbs\":");
  lw_buf_json(b, thumbs);
  lw_buf_printf(b, ",\"library\":[");
  lw_wallpaper_each(describe_library, b);
  if (b->s[b->n - 1] == ',') b->n--;
  lw_buf_printf(b, "]");
  lw_buf_printf(b, ",\"config\":[");
  for (int i = 0; i < NOPTS; i++) {
    const struct opt *op = &opts[i];
    char skey[96];
    snprintf(skey, sizeof skey, "cfg.%s", op->key);
    opt_string(op, val);
    lw_buf_printf(b, "%s{\"key\":\"%s\",\"group\":\"%s\",\"type\":\"%s\",\"label\":", i ? "," : "", op->key, op->group,
                  op->type == 'f' ? "float" : op->type == 'i' ? "int" : op->type == 'b' ? "bool" : op->type == 's' ? "text" : "choice");
    lw_buf_json(b, op->label);
    lw_buf_printf(b, ",\"description\":");
    lw_buf_json(b, op->desc);
    if (op->type == 'c') {
      lw_buf_printf(b, ",\"value\":\"%s\",\"default\":\"%s\",\"options\":[", val, op->sdef);
      for (const char *p = op->options; p && *p;) {
        const char *e = strchr(p, '|');
        lw_buf_printf(b, "%s\"%.*s\"", p == op->options ? "" : ",", (int)(e ? e - p : (long)strlen(p)), p);
        p = e ? e + 1 : NULL;
      }
      lw_buf_printf(b, "]");
    } else if (op->type == 's') {
      lw_buf_printf(b, ",\"value\":");
      lw_buf_json(b, val);
      lw_buf_printf(b, ",\"default\":");
      lw_buf_json(b, op->sdef);
    } else {
      lw_buf_printf(b, ",\"value\":%s,\"default\":%g,\"min\":%g,\"max\":%g,\"step\":%g", val, op->def, op->min, op->max, op->step);
    }
    lw_buf_printf(b, ",\"changed\":%s}", lw_state_get(skey) ? "true" : "false");
  }
  lw_buf_printf(b, "]}\n");
}

/* ---------- status ---------- */
int lw_engine_stats(char *out, size_t len) {
  size_t n = 0;
  for (struct lw_output *o = E.outputs; o && n + 200 < len; o = o->next) {
    if (!o->wp) continue;
    n += (size_t)snprintf(out + n, len - n,
                          "%-10s %-12s %5.1f fps (cap %g)  frame %5.2f ms, worst %5.2f  gpu %5.2f ms  cpu %4.1f%%  %dx%d",
                          o->name, o->wp_id, o->m_fps, o->fps, o->m_frame_ms, o->m_max_ms, o->gpu_ms, o->m_cpu, o->bw, o->bh);
    float cov = lw_wallpaper_coverage(o->wp);
    if (cov >= 0) n += (size_t)snprintf(out + n, len - n, "  filled %.0f%%", cov * 100);
    if (lw_wallpaper_sound(o->wp) > 0 || E.sys.audio_running)
      n += (size_t)snprintf(out + n, len - n, "  sound %.2f (onsets %d)", E.sys.sound_energy, E.sys.kicks);
    n += (size_t)snprintf(out + n, len - n, "%s%s\n", o->paused ? "  paused: " : "", o->paused ? o->pause_reason : "");
  }
  if (!n) n = (size_t)snprintf(out, len, "(nothing rendering)\n");
  return (int)n;
}

int lw_engine_status_json(char *buf, size_t len) {
  size_t n = 0;
#define P(...) n += (size_t)snprintf(buf + n, n < len ? len - n : 0, __VA_ARGS__)
  P("{\"version\":\"%s\",\"enabled\":%s,\"still\":%s,\"paused\":%s,\"wallpaper\":\"%s\",\"theme\":\"%s\",", LW_VERSION,
    lw_engine_on() ? "true" : "false", E.still ? "true" : "false", E.manual_pause ? "true" : "false", E.current,
    E.sys.theme_to.name);
  char plugin[16];
  lw_plugin_status(plugin, sizeof plugin);
  P("\"plugin\":\"%s\",", plugin); /* Kinetic's Hyprland plugin: window glass and the light */
  P("\"on_battery\":%s,\"battery\":%.2f,\"audio\":%s,\"uptime\":%.0f,\"outputs\":[", E.sys.on_battery ? "true" : "false",
    E.sys.has_battery ? E.sys.battery : -1.0f, E.sys.audio_running ? "true" : "false", lw_now() - E.started);
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    P("%s{\"name\":\"%s\",\"wallpaper\":\"%s\",\"logical\":[%d,%d],\"buffer\":[%d,%d],\"fps_cap\":%.0f,"
      "\"state\":\"%s\",\"gpu_ms\":%.2f,\"adaptive_scale\":%.2f,\"frames\":%llu,\"workspace\":%d,\"windows\":%d}",
      o == E.outputs ? "" : ",", o->name, o->wp_id, o->lw, o->lh, o->bw, o->bh, o->fps,
      o->paused ? o->pause_reason : "running", o->gpu_ms, o->adaptive, (unsigned long long)o->frames, o->ws_id,
      o->ws_windows);
  }
  P("],\"error\":\"");
  for (const char *e = E.last_error; *e && n + 8 < len; e++) {
    if (*e == '"' || *e == '\\') buf[n++] = '\\';
    buf[n++] = *e == '\n' ? ' ' : *e;
  }
  P("\"}\n");
#undef P
  return (int)n;
}
