/* Workspace motion in step with Hyprland.
 *
 * Kinetic's motion module configures Hyprland's workspace animation and tells
 * the engine the same duration and curve (`motion` over the socket). On a
 * switch, the wallpaper's workspace position then eases from where it was to
 * the new workspace on exactly that curve, so the wallpaper and the windows
 * land together. On top of that, a parallax amount pans wallpapers that
 * allow it by a fraction of the screen per workspace: the windows move a whole
 * screen, the wallpaper a part of one, and the desktop gets depth.
 *
 * Without a `motion` setting, the old behavior stands: the position follows
 * the workspace with a soft exponential ease, and nothing pans.
 *
 * Pass-through: with it on, Kinetic's Lua makes Super + 5 from workspace 1 step
 * through 2, 3 and 4, the whole trip one run of the switch curve (each Hyprland
 * step plays its slice of it; modules/motion/trip.py). Before the first step it
 * sends Hyprland a custom event, `kinetic-travel <output> <n> <dir> <ms> <x1,y1,x2,y2> <to>`,
 * which reaches the engine ahead of that first switch, and the engine traces
 * the same curve over the same time. */
#include "lw.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CURVE_BEZIER, CURVE_SPRING };
enum { STYLE_SLIDE, STYLE_FADE, STYLE_SLIDEFADE }; /* Hyprland's workspace styles, for placing window glass */

static struct {
  bool on;
  float ms;       /* bezier: the animation's duration */
  int curve;
  float p[4];     /* bezier x1, y1, x2, y2 | spring mass, stiffness, dampening */
  float parallax; /* screens per workspace */
  bool vertical;  /* slidevert: workspaces stack top to bottom */
  float spring_end; /* seconds until a spring has settled */
  int style;        /* slide, fade, or slide part of the way while fading */
  float fade_slide; /* slidefade: how far the workspaces travel, in screens */
} M;

static float bezier_axis(float t, float a, float b) { /* one coordinate of the cubic from 0 to 1 */
  float u = 1 - t;
  return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t;
}

/* y for the given x on the curve (0,0) (x1,y1) (x2,y2) (1,1): what Hyprland
 * evaluates at each instant of an animation. Newton, with bisection as the
 * safety net for steep curves. */
static float bezier_pts(const float *p, float x) {
  float t = x;
  for (int i = 0; i < 8; i++) {
    float dx = bezier_axis(t, p[0], p[2]) - x;
    if (fabsf(dx) < 1e-5f) return bezier_axis(t, p[1], p[3]);
    float u = 1 - t;
    float d = 3 * u * u * p[0] + 6 * u * t * (p[2] - p[0]) + 3 * t * t * (1 - p[2]);
    if (fabsf(d) < 1e-6f) break;
    t -= dx / d;
    if (t < 0 || t > 1) break;
  }
  float lo = 0, hi = 1;
  t = x;
  for (int i = 0; i < 30; i++) {
    t = (lo + hi) * 0.5f;
    if (bezier_axis(t, p[0], p[2]) < x) lo = t;
    else hi = t;
  }
  return bezier_axis(t, p[1], p[3]);
}

static float bezier(float x) { return bezier_pts(M.p, x); }

/* Omarchy gives each monitor its own block of ten workspaces (1-10, 11-20, ...).
 * The pan counts from the block's first, so workspace 1 always shows the
 * wallpaper where it starts and a new parallax amount moves it only a little. */
float lw_motion_origin(int id) { return id > 0 ? (float)((id - 1) / 10 * 10 + 1) : 1; }

/* A damped spring released from 0 toward 1, after `t` seconds: mass, stiffness, dampening. */
static float spring_pts(const float *p, float t) {
  float m = p[0], k = p[1], c = p[2];
  float w0 = sqrtf(k / m), z = c / (2 * sqrtf(k * m));
  if (z < 1) {
    float wd = w0 * sqrtf(1 - z * z);
    return 1 - expf(-z * w0 * t) * (cosf(wd * t) + z * w0 / wd * sinf(wd * t));
  }
  if (z == 1) return 1 - expf(-w0 * t) * (1 + w0 * t);
  float s = sqrtf(z * z - 1), r1 = -w0 * (z - s), r2 = -w0 * (z + s);
  return 1 - (r2 * expf(r1 * t) - r1 * expf(r2 * t)) / (r2 - r1);
}

static float spring(float t) { return spring_pts(M.p, t); }

static float spring_settle_pts(const float *p) {
  float m = p[0], k = p[1], c = p[2];
  float w0 = sqrtf(k / m), z = c / (2 * sqrtf(k * m));
  float rate = z <= 1 ? z * w0 : w0 * (z - sqrtf(z * z - 1)); /* the slowest decay */
  return rate > 0 ? fminf(10, logf(500) / rate) : 10;
}

static float spring_settle(void) { return spring_settle_pts(M.p); }

/* Advances one output's workspace position; true while it is still moving. */
bool lw_motion_step(struct lw_output *o, double now, float dt) {
  if (o->travel.on) {
    float t = (float)(now - o->travel.t0);
    o->ws_smooth = o->travel.from + (o->travel.to - o->travel.from) * bezier_pts(o->travel.p, fminf(1, t / o->travel.secs));
    if (t >= o->travel.secs) { /* arrived; anything since then eases on from here */
      o->travel.on = false;
      o->ws_from = o->ws_smooth = o->travel.to;
      o->ws_t0 = now;
    }
  } else if (!M.on) {
    float d = (float)o->ws_id - o->ws_smooth;
    o->ws_smooth = fabsf(d) < 0.002f ? (float)o->ws_id : o->ws_smooth + d * (1 - expf(-dt * 9));
  } else {
    float t = (float)(now - o->ws_t0), e;
    if (M.curve == CURVE_SPRING) e = t >= M.spring_end ? 1 : spring(t);
    else e = M.ms <= 0 || t * 1000 >= M.ms ? 1 : bezier(t * 1000 / M.ms);
    o->ws_smooth = e >= 1 && (M.curve == CURVE_BEZIER || t >= M.spring_end) ? (float)o->ws_id
                                                                            : o->ws_from + ((float)o->ws_id - o->ws_from) * e;
  }
  /* windows slide left toward a higher workspace (or up, stacked vertically);
   * the view over the wallpaper follows at the parallax fraction */
  float pan = M.on ? (o->ws_smooth - lw_motion_origin(o->ws_id)) * M.parallax : 0;
  o->view.parallax[0] = M.vertical ? 0 : pan;
  o->view.parallax[1] = M.vertical ? -pan : 0;
  return lw_motion_moving(o);
}

bool lw_motion_moving(const struct lw_output *o) { return o->travel.on || o->ws_smooth != (float)o->ws_id; }

/* A switch starts: ease from wherever the wallpaper is now. A step of a
 * pass-through is already on its way; anything else ends the pass-through. */
void lw_motion_switch(struct lw_output *o, int to) {
  o->ws_prev = o->ws_id;
  if (o->travel.on) {
    float lo = fminf(o->travel.from, o->travel.to) - 0.5f, hi = fmaxf(o->travel.from, o->travel.to) + 0.5f;
    if (to >= lo && to <= hi) return;
    o->travel.on = false;
  }
  o->ws_from = o->ws_smooth;
}

bool lw_motion_workspace_place(const struct lw_output *o, int ws, float *dx, float *dy, float *alpha) {
  *dx = *dy = 0;
  *alpha = 1;
  if (!M.on || !lw_motion_moving(o)) return ws == o->ws_id; /* without Kinetic motion, Hyprland switches at once */
  float rel, vis; /* where the workspace sits, in screens from the view, and how much of it shows */
  if (o->travel.on) {
    rel = (float)ws - o->ws_smooth; /* a pass-through: every workspace on the way, a step apart */
    if (fabsf(rel) >= 1) return false;
    vis = 1 - fabsf(rel);
  } else {
    /* one switch: Hyprland slides the old workspace out and the new one in, a
     * screen apart however far apart their numbers are */
    float span = (float)o->ws_id - o->ws_from, dir = span >= 0 ? 1.0f : -1.0f;
    float p = fabsf(span) > 1e-4f ? (o->ws_smooth - o->ws_from) / span : 1;
    if (ws == o->ws_id) rel = (1 - p) * dir, vis = p;
    else if (ws == o->ws_prev) rel = -p * dir, vis = 1 - p;
    else return false;
  }
  float shift = M.style == STYLE_FADE ? 0 : M.style == STYLE_SLIDEFADE ? rel * M.fade_slide : rel;
  if (M.style != STYLE_SLIDE) *alpha = lw_clampf(vis, 0, 1);
  if (M.vertical) *dy = shift;
  else *dx = shift;
  return true;
}

/* "a,b,c,d": `want` finite numbers. */
static bool parse_list(const char *s, float *p, int want) {
  char *end;
  for (int i = 0; i < want; i++) {
    p[i] = strtof(s, &end);
    if (end == s || !isfinite(p[i]) || (i < want - 1 && *end != ',')) return false;
    s = end + (i < want - 1);
  }
  return !*s;
}

void lw_motion_travel(const char *args) {
  char name[64], curve[128] = "";
  int n = 0, dir = 0, to = 0;
  float ms = 0, p[4];
  int got = sscanf(args, "%63s %d %d %f %127s %d", name, &n, &dir, &ms, curve, &to);
  if (got < 2) return;
  struct lw_output *o = E.outputs;
  while (o && strcmp(o->name, name)) o = o->next;
  if (!o) return;
  bool ok = M.on && n >= 2 && n <= 100 && (dir == 1 || dir == -1) && got >= 5 && ms > 0 && parse_list(curve, p, 4) &&
            p[0] >= 0 && p[0] <= 1 && p[2] >= 0 && p[2] <= 1;
  if (!ok) { /* `kinetic-travel <output> 0`: called off, ease on from where it is */
    if (o->travel.on) {
      o->travel.on = false;
      o->ws_from = o->ws_smooth;
      o->ws_t0 = lw_now();
    }
    return;
  }
  o->travel.on = true;
  o->travel.t0 = lw_now();
  o->travel.from = o->ws_smooth;
  /* where it ends: the workspace the Lua names, not ours plus n, since the
   * event can come in the same read as a step we haven't asked Hyprland about yet */
  o->travel.to = (float)(got == 6 && to > 0 ? to : o->ws_id + dir * n);
  o->travel.secs = lw_clampf(ms, 10, 20000) / 1000;
  memcpy(o->travel.p, p, sizeof p);
  o->travel.n = n;
  lw_engine_invalidate(o);
}

/* ---------- windows ----------
 * A motion line may carry Hyprland's window animations (open=.. move=..
 * close=.. fade=..: Kinetic's motion module writes them for Hyprland). The
 * engine has no use for them any more (window glass is drawn inside Hyprland,
 * by Kinetic's plugin), but it still checks them, so a bad line is refused. */
static const char *kind_key[] = {"open", "move", "close"};

/* "410/bezier/0.23,1,0.32,1/popin:87", "380/spring/1,120,14", "off" */
static bool valid_window(const char *spec) {
  if (!strcmp(spec, "off")) return true;
  char buf[160];
  snprintf(buf, sizeof buf, "%s", spec);
  char *f[4] = {0};
  int n = 0;
  for (char *save = NULL, *t = strtok_r(buf, "/", &save); t && n < 4; t = strtok_r(NULL, "/", &save)) f[n++] = t;
  if (n < 3) return false;
  char *end;
  float ms = strtof(f[0], &end), p[4];
  if (end == f[0] || *end || !isfinite(ms)) return false;
  int want = !strcmp(f[1], "spring") ? 3 : !strcmp(f[1], "bezier") ? 4 : 0;
  if (!want || !parse_list(f[2], p, want)) return false;
  if (want == 4 && (p[0] < 0 || p[0] > 1 || p[2] < 0 || p[2] > 1)) return false;
  if (want == 3 && (p[0] <= 0 || p[1] <= 0 || p[2] < 0)) return false;
  if (n == 4 && strcmp(f[3], "slide") && strcmp(f[3], "gnomed") && strcmp(f[3], "fade")) {
    if (strncmp(f[3], "popin", 5)) return false;
    const char *v = f[3] + 5;
    if (*v == ':') v++;
    if (*v) {
      strtof(v, &end);
      if (end == v || (*end && strcmp(end, "%"))) return false;
    }
  }
  return true;
}

static int describe(char *out, size_t len) {
  size_t n;
  if (!M.on) n = (size_t)snprintf(out, len, "off (the wallpaper follows workspaces softly; no parallax)\n");
  else if (M.curve == CURVE_SPRING)
    n = (size_t)snprintf(out, len, "spring mass %g stiffness %g dampening %g (settles in %.2f s), parallax %g %s\n", M.p[0],
                         M.p[1], M.p[2], M.spring_end, M.parallax, M.vertical ? "vertical" : "horizontal");
  else
    n = (size_t)snprintf(out, len, "%g ms bezier %g,%g,%g,%g, parallax %g %s\n", M.ms, M.p[0], M.p[1], M.p[2], M.p[3],
                         M.parallax, M.vertical ? "vertical" : "horizontal");
  return (int)(n < len ? n : len - 1);
}

/* The window part of a motion line: open=.. move=.. close=.. fade=on|off, after the rest. */
static bool check_windows(int argc, char **argv, char *err, size_t errlen) {
  for (int i = 0; i < argc; i++) {
    char *eq = strchr(argv[i], '=');
    if (!eq) return snprintf(err, errlen, "'%s' where a window setting (open=, move=, close=, fade=) belongs", argv[i]), false;
    size_t kl = (size_t)(eq - argv[i]);
    if (kl == 4 && !strncmp(argv[i], "fade", 4)) continue;
    int k = -1;
    for (int j = 0; j < 3; j++)
      if (strlen(kind_key[j]) == kl && !strncmp(argv[i], kind_key[j], kl)) k = j;
    if (k < 0) return snprintf(err, errlen, "no window animation named '%.*s' (open, move, close, fade)", (int)kl, argv[i]), false;
    if (!valid_window(eq + 1))
      return snprintf(err, errlen, "bad %s '%s' (want <ms>/bezier/<x1,y1,x2,y2>[/popin:<percent>|slide|gnomed|fade], or off)", kind_key[k], eq + 1), false;
  }
  return true;
}

/* Parses and applies one setting; the same text is kept in the state file.
 *   motion <ms> bezier|spring <points> <parallax> h|v [style] [open=.. move=.. close=.. fade=..]
 *   motion off [open=.. move=.. close=.. fade=..] */
static bool apply(int argc, char **argv, char *err, size_t errlen) {
  int pos = 1; /* the positional part ends where the window settings begin */
  while (pos < argc && !strchr(argv[pos], '=')) pos++;
  if (!check_windows(argc - pos, argv + pos, err, errlen)) return false;
  argc = pos;
  if (argc == 2 && !strcmp(argv[1], "off")) {
    M.on = false;
  } else {
    if (argc != 6 && argc != 7) {
      snprintf(err, errlen, "usage: motion <ms> bezier|spring <x1,y1,x2,y2 | mass,stiffness,dampening> <parallax> h|v [slide|fade|slidefade:<percent>] [open=.. move=.. close=.. fade=on|off]  (or: motion off [open=.. ...])");
      return false;
    }
    int style = STYLE_SLIDE;
    float fade_slide = 1;
    if (argc == 7) {
      if (!strcmp(argv[6], "slide")) style = STYLE_SLIDE;
      else if (!strcmp(argv[6], "fade")) style = STYLE_FADE;
      else if (!strncmp(argv[6], "slidefade:", 10)) {
        char *end;
        float pct = strtof(argv[6] + 10, &end);
        if (end == argv[6] + 10 || *end || !isfinite(pct)) return snprintf(err, errlen, "bad style '%s'", argv[6]), false;
        style = STYLE_SLIDEFADE;
        fade_slide = lw_clampf(pct, 0, 100) / 100;
      } else return snprintf(err, errlen, "the style is slide, fade or slidefade:<percent>"), false;
    }
    float p[4] = {0};
    int want = !strcmp(argv[2], "spring") ? 3 : !strcmp(argv[2], "bezier") ? 4 : 0;
    if (!want) return snprintf(err, errlen, "the curve is bezier or spring"), false;
    char *end;
    if (!parse_list(argv[3], p, want)) return snprintf(err, errlen, "bad curve '%s'", argv[3]), false;
    if (want == 4 && (p[0] < 0 || p[0] > 1 || p[2] < 0 || p[2] > 1))
      return snprintf(err, errlen, "a bezier's x values lie between 0 and 1"), false;
    if (want == 3 && (p[0] <= 0 || p[1] <= 0 || p[2] < 0)) return snprintf(err, errlen, "a spring needs mass and stiffness above 0"), false;
    float ms = strtof(argv[1], &end);
    if (end == argv[1] || !isfinite(ms)) return snprintf(err, errlen, "bad duration '%s'", argv[1]), false;
    float par = strtof(argv[4], &end);
    if (end == argv[4] || !isfinite(par)) return snprintf(err, errlen, "bad parallax '%s'", argv[4]), false;
    M.on = true;
    M.ms = lw_clampf(ms, 0, 5000);
    M.curve = want == 3 ? CURVE_SPRING : CURVE_BEZIER;
    memcpy(M.p, p, sizeof p);
    M.parallax = lw_clampf(par, 0, 2);
    M.vertical = argv[5][0] == 'v';
    M.style = style;
    M.fade_slide = fade_slide;
    if (M.curve == CURVE_SPRING) M.spring_end = spring_settle();
  }
  return true;
}

int lw_motion_command(int argc, char **argv, char *out, size_t len) {
  if (argc < 2) return describe(out, len);
  char err[512];
  if (!apply(argc, argv, err, sizeof err)) return snprintf(out, len, "error: %s\n", err);
  char keep[512] = "";
  size_t n = 0;
  for (int i = 1; i < argc && n < sizeof keep; i++) n += (size_t)snprintf(keep + n, sizeof keep - n, "%s%s", i > 1 ? " " : "", argv[i]);
  char path[LW_PATH], tmp[LW_PATH + 8];
  lw_signal_path(path, "motion"); /* the same file `kinetic motion` writes: kept across restarts */
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "w");
  if (f) {
    fprintf(f, "%s\n", keep);
    fclose(f);
    rename(tmp, path);
  }
  lw_engine_invalidate(NULL);
  return describe(out, len);
}

void lw_motion_load(void) {
  char path[LW_PATH];
  lw_signal_path(path, "motion");
  char *saved = lw_read_file(path, NULL);
  if (!saved) return;
  char buf[512], *argv[LW_MOTION_ARGS] = {"motion"};
  int argc = 1;
  snprintf(buf, sizeof buf, "%s", saved);
  free(saved);
  buf[strcspn(buf, "\n")] = 0;
  for (char *tok = strtok(buf, " "); tok && argc < LW_MOTION_ARGS; tok = strtok(NULL, " ")) argv[argc++] = tok;
  char err[512];
  if (!apply(argc, argv, err, sizeof err)) lw_log("motion: ignoring saved setting: %s", err);
}

/* For `thumbnail --parallax`: the pan a wallpaper would show, in screens. */
float lw_motion_parallax(void) { return M.on ? M.parallax : 0; }
