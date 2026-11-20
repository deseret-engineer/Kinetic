/* Omarchy theme module: reads the active theme's colors.toml and follows theme
 * and background switches through inotify, so no hook or polling is needed. */
#include "lw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

static int ino = -1, timer = -1;

static bool color(const struct lw_toml *t, const char *key, float rgb[3]) {
  return lw_parse_hex(lw_toml_str(t, "", key, NULL), rgb);
}

static void fallback(float dst[3], const float src[3]) { memcpy(dst, src, sizeof(float) * 3); }

void lw_theme_load(struct lw_theme *th) {
  char path[LW_PATH];
  memset(th, 0, sizeof *th);
  /* sane defaults if Omarchy's files are missing */
  lw_parse_hex("#1a1b26", th->bg);
  lw_parse_hex("#a9b1d6", th->fg);
  lw_parse_hex("#7aa2f7", th->accent);
  th->dark = 1;
  snprintf(th->name, sizeof th->name, "unknown");

  lw_dir_omarchy(path, "theme.name");
  char *name = lw_read_file(path, NULL);
  if (name) {
    name[strcspn(name, "\r\n")] = 0;
    snprintf(th->name, sizeof th->name, "%s", name);
    free(name);
  }

  lw_dir_omarchy(path, "theme/colors.toml");
  struct lw_toml *t = lw_toml_load(path);
  color(t, "background", th->bg);
  color(t, "foreground", th->fg);
  if (!color(t, "accent", th->accent)) color(t, "blue", th->accent);
  const char *mode = lw_toml_str(t, "", "mode", NULL);
  if (mode)
    th->dark = strcmp(mode, "light") ? 1 : 0;
  else
    th->dark = (th->bg[0] * 0.3f + th->bg[1] * 0.59f + th->bg[2] * 0.11f) < 0.5f;

  /* ANSI-ordered palette. Both of Omarchy's schemas are understood: the named
   * one (red, bright_red, ...) and the older color0..color15. */
  static const char *named[16] = {"darker_background", "red", "green", "yellow", "blue", "magenta", "cyan", "foreground",
                                  "muted", "bright_red", "bright_green", "bright_yellow", "bright_blue", "bright_magenta",
                                  "bright_cyan", "bright_foreground"};
  for (int i = 0; i < 16; i++) {
    char key[16];
    snprintf(key, sizeof key, "color%d", i);
    if (color(t, named[i], th->palette[i]) || color(t, key, th->palette[i])) continue;
    if (i >= 9 && i <= 14)
      fallback(th->palette[i], th->palette[i - 8]);
    else if (i == 0)
      fallback(th->palette[i], th->bg);
    else if (i == 7 || i == 15)
      fallback(th->palette[i], th->fg);
    else if (i == 8)
      for (int c = 0; c < 3; c++) th->palette[i][c] = (th->bg[c] + th->fg[c]) * 0.5f;
    else
      fallback(th->palette[i], th->accent);
  }
  lw_toml_free(t);
}

static void on_inotify(void *data, uint32_t ev) {
  char buf[4096];
  while (read(ino, buf, sizeof buf) > 0) {}
  lw_debounce_arm(timer, 0.15); /* a theme switch touches several files; act once */
}

static void on_timer(void *data, uint32_t ev) {
  uint64_t n;
  if (read(timer, &n, sizeof n) < 0) return;
  struct lw_theme t;
  lw_theme_load(&t);
  lw_dbg("theme: %s", t.name);
  lw_engine_theme_changed(&t, true);
}

static bool start(void) {
  struct lw_theme t;
  lw_theme_load(&t);
  E.sys.theme_to = t;
  lw_engine_theme_changed(&t, false);
  char dir[LW_PATH];
  lw_dir_omarchy(dir, "");
  ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (ino < 0 || inotify_add_watch(ino, dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE) < 0) {
    lw_log("theme: cannot watch %s, theme changes need 'kinetic wall refresh'", dir);
    return true;
  }
  timer = lw_debounce_fd();
  lw_loop_add(ino, on_inotify, NULL);
  lw_loop_add(timer, on_timer, NULL);
  return true;
}

static void stop(void) {
  if (ino >= 0) close(ino);
  if (timer >= 0) close(timer);
}

struct lw_module lw_mod_theme = {"theme", start, stop, false};
