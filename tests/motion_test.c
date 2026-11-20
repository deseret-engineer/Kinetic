/* Workspace motion: the curves match Hyprland's definitions, a switch lands on
 * time, and parallax pans the right way. Build and run: tests/run.sh */
#include "../modules/wall/src/lw.h"
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the few engine pieces motion.c touches */
struct lw_engine E;
float lw_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
void lw_signal_path(char *out, const char *name) { snprintf(out, LW_PATH, "/tmp/kinetic-motion-test-%s", name); }
void lw_engine_invalidate(struct lw_output *o) {}
char *lw_read_file(const char *path, size_t *len) { return NULL; }
void lw_log(const char *fmt, ...) {}
static double clock_now;
double lw_now(void) { return clock_now; }

#include "../modules/wall/src/motion.c"

static void set(const char *spec) {
  char buf[512], *argv[LW_MOTION_ARGS] = {"motion"}, out[1024];
  int argc = 1;
  snprintf(buf, sizeof buf, "%s", spec);
  for (char *t = strtok(buf, " "); t; t = strtok(NULL, " ")) argv[argc++] = t;
  lw_motion_command(argc, argv, out, sizeof out);
  assert(strncmp(out, "error", 5) != 0);
}

#define NEAR(a, b, eps) assert(fabsf((float)(a) - (float)(b)) < (eps))

int main(void) {
  /* easeInOutCubic (Omarchy's): symmetric enough that halfway in time is near halfway */
  set("500 bezier 0.65,0.05,0.36,1 0.25 h");
  NEAR(bezier(0), 0, 1e-4);
  NEAR(bezier(1), 1, 1e-4);
  float prev = 0;
  for (int i = 1; i <= 100; i++) { /* never runs backwards */
    float y = bezier(i / 100.0f);
    assert(y >= prev - 1e-4f);
    prev = y;
  }
  NEAR(bezier(0.5f), 0.5f, 0.12);
  /* linear is the identity */
  set("300 bezier 0,0,1,1 0 h");
  for (int i = 0; i <= 10; i++) NEAR(bezier(i / 10.0f), i / 10.0f, 1e-3);
  /* an overshooting curve passes 1 before settling */
  set("300 bezier 0.34,1.56,0.64,1 0 h");
  float top = 0;
  for (int i = 0; i <= 100; i++) top = fmaxf(top, bezier(i / 100.0f));
  assert(top > 1.05f);

  /* a switch from workspace 1 to 3 lands exactly at the duration */
  set("500 bezier 0.65,0.05,0.36,1 0.25 h");
  struct lw_output o = {0};
  o.ws_id = 1;
  o.ws_smooth = 1;
  lw_motion_switch(&o, 3);
  o.ws_id = 3;
  o.ws_t0 = 100.0;
  assert(lw_motion_step(&o, 100.25, 0.016f)); /* halfway: still moving */
  assert(o.ws_smooth > 1.5f && o.ws_smooth < 2.5f);
  assert(!lw_motion_step(&o, 100.5, 0.016f));  /* done, exactly on the workspace */
  NEAR(o.ws_smooth, 3, 1e-6);
  /* parallax: a quarter screen per workspace from the first, panning toward higher workspaces */
  NEAR(o.view.parallax[0], 0.5, 1e-5);
  NEAR(o.view.parallax[1], 0, 1e-6);

  /* vertical slides pan down the screen (GL y is up, so negative) */
  set("400 bezier 0.23,1,0.32,1 0.2 v");
  lw_motion_step(&o, 200, 0.016f);
  NEAR(o.view.parallax[0], 0, 1e-6);
  NEAR(o.view.parallax[1], -0.4, 1e-5);

  /* Omarchy's second monitor has workspaces 11-20: its 11 shows the wallpaper unpanned, like 1 */
  set("500 bezier 0.65,0.05,0.36,1 0.25 h");
  o.ws_id = 11, o.ws_smooth = 11, o.ws_from = 11;
  lw_motion_step(&o, 300, 0.016f);
  NEAR(o.view.parallax[0], 0, 1e-6);
  o.ws_id = 15, o.ws_smooth = 15;
  lw_motion_step(&o, 300, 0.016f);
  NEAR(o.view.parallax[0], 1.0, 1e-5);

  /* pass-through from 1 to 5: one run of the switch curve (glide) over the whole trip, 600 ms */
  strcpy(o.name, "eDP-1");
  E.outputs = &o;
  set("500 bezier 0.65,0.05,0.36,1 0.25 h");
  o.ws_id = 1, o.ws_smooth = 1, o.ws_from = 1;
  clock_now = 50.0;
  lw_motion_travel("eDP-1 4 1 600 0.65,0.05,0.36,1");
  assert(o.travel.on && o.travel.n == 4);
  NEAR(o.travel.to, 5, 1e-6);
  lw_motion_switch(&o, 2); /* Hyprland's steps arrive: they do not restart anything */
  o.ws_id = 2;
  assert(o.travel.on);
  float last = 1;
  for (int i = 1; i <= 60; i++) { /* the whole trip follows the curve, never backwards */
    lw_motion_step(&o, 50.0 + i * 0.01, 0.016f);
    NEAR(o.ws_smooth, 1 + 4 * bezier(i / 60.0f), 1e-4);
    assert(o.ws_smooth >= last - 1e-5f);
    last = o.ws_smooth;
  }
  o.ws_id = 5;
  assert(!lw_motion_step(&o, 50.61, 0.016f)); /* landed on time */
  NEAR(o.ws_smooth, 5, 1e-6);
  NEAR(o.view.parallax[0], 1.0, 1e-5);
  /* an overshooting trip curve passes the target and comes back */
  o.ws_id = 1, o.ws_smooth = 1;
  clock_now = 55.0;
  lw_motion_travel("eDP-1 4 1 400 0.34,1.12,0.64,1");
  top = 0;
  for (int i = 1; i <= 40; i++) {
    lw_motion_step(&o, 55.0 + i * 0.01, 0.016f);
    top = fmaxf(top, o.ws_smooth);
  }
  assert(top > 5.01f);
  o.ws_id = 5;
  lw_motion_step(&o, 55.41, 0.016f);
  NEAR(o.ws_smooth, 5, 1e-6);
  /* called off midway: the wallpaper eases on from where it was */
  o.ws_id = 1, o.ws_smooth = 1;
  clock_now = 60.0;
  lw_motion_travel("eDP-1 4 1 600 0.65,0.05,0.36,1");
  lw_motion_step(&o, 60.25, 0.016f);
  float mid = o.ws_smooth;
  lw_motion_travel("eDP-1 0");
  assert(!o.travel.on);
  NEAR(o.ws_from, mid, 1e-6);
  /* a press mid-trip: the new trip names where it ends, which holds even when
   * the engine hasn't yet asked Hyprland about the step just taken (ws_id 1, Hyprland on 2) */
  lw_motion_travel("eDP-1 4 1 600 0.65,0.05,0.36,1 7");
  assert(o.travel.on);
  NEAR(o.travel.to, 7, 1e-6);
  lw_motion_travel("eDP-1 3 1 500 0.65,0.05,0.36,1 0");
  NEAR(o.travel.to, 4, 1e-6); /* no workspace named: n from where the engine is, as before */
  /* a switch outside the trip (say Super + Tab elsewhere) also ends it */
  lw_motion_travel("eDP-1 4 1 600 0.65,0.05,0.36,1");
  lw_motion_switch(&o, 9);
  assert(!o.travel.on);
  /* malformed or unknown: ignored */
  lw_motion_travel("eDP-1 4 1 600 2,0,0,1");
  assert(!o.travel.on);
  lw_motion_travel("eDP-1 4 1 600");
  assert(!o.travel.on);
  lw_motion_travel("HDMI-9 4 1 600 0,0,1,1");
  assert(!o.travel.on);
  E.outputs = NULL;

  /* window glass placement: where a workspace's windows are during a switch, as Hyprland shows them */
  {
    float dx, dy, al;
    set("400 bezier 0,0,1,1 0.25 h slide");
    struct lw_output w = {0};
    w.ws_id = 1, w.ws_smooth = 1;
    assert(lw_motion_workspace_place(&w, 1, &dx, &dy, &al) && dx == 0 && al == 1); /* at rest: only the current one */
    assert(!lw_motion_workspace_place(&w, 2, &dx, &dy, &al));
    lw_motion_switch(&w, 5); /* 1 -> 5: the old slides out left, the new in from the right, a screen apart */
    w.ws_id = 5, w.ws_t0 = 0;
    lw_motion_step(&w, 0.1, 0.016f); /* a quarter of the way (linear) */
    assert(lw_motion_workspace_place(&w, 5, &dx, &dy, &al));
    NEAR(dx, 0.75, 1e-3);
    NEAR(al, 1, 1e-6);
    assert(lw_motion_workspace_place(&w, 1, &dx, &dy, &al));
    NEAR(dx, -0.25, 1e-3);
    assert(!lw_motion_workspace_place(&w, 3, &dx, &dy, &al)); /* nothing between is shown */
    /* fade: in place, fading */
    set("400 bezier 0,0,1,1 0 h fade");
    lw_motion_workspace_place(&w, 5, &dx, &dy, &al);
    NEAR(dx, 0, 1e-6);
    NEAR(al, 0.25, 1e-3);
    /* slidefade 20%: a fifth of the way, fading */
    set("400 bezier 0,0,1,1 0.2 v slidefade:20");
    lw_motion_workspace_place(&w, 5, &dx, &dy, &al);
    NEAR(dx, 0, 1e-6);
    NEAR(dy, 0.15, 1e-3);
    NEAR(al, 0.25, 1e-3);
    /* a pass-through shows every workspace on the way, where the trip has got to */
    set("400 bezier 0,0,1,1 0.25 h slide");
    strcpy(w.name, "W");
    E.outputs = &w;
    w.ws_id = 1, w.ws_smooth = 1;
    clock_now = 70.0;
    lw_motion_travel("W 4 1 400 0,0,1,1");
    lw_motion_step(&w, 70.15, 0.016f); /* 1.5 workspaces along */
    assert(lw_motion_workspace_place(&w, 3, &dx, &dy, &al));
    NEAR(dx, 0.5, 1e-3);
    assert(lw_motion_workspace_place(&w, 2, &dx, &dy, &al));
    NEAR(dx, -0.5, 1e-3);
    assert(!lw_motion_workspace_place(&w, 4, &dx, &dy, &al));
    E.outputs = NULL;
    /* without Kinetic motion Hyprland switches at once: only the target shows */
    set("off");
    w.travel.on = false;
    w.ws_id = 2, w.ws_smooth = 1.5f;
    assert(lw_motion_workspace_place(&w, 2, &dx, &dy, &al) && dx == 0);
    assert(!lw_motion_workspace_place(&w, 1, &dx, &dy, &al));
    char out[256], *bad[] = {"motion", "400", "bezier", "0,0,1,1", "0.2", "h", "wobble"};
    lw_motion_command(7, bad, out, sizeof out);
    assert(!strncmp(out, "error", 5));
  }

  /* a spring arrives and stays */
  set("450 spring 1,120,14 0.3 h");
  o.ws_id = 1;
  o.ws_smooth = 1;
  lw_motion_switch(&o, 2);
  o.ws_id = 2;
  o.ws_t0 = 0;
  lw_motion_step(&o, 0.1, 0.016f);
  assert(o.ws_smooth > 1.2f);
  assert(!lw_motion_step(&o, M.spring_end + 0.01, 0.016f));
  NEAR(o.ws_smooth, 2, 1e-6);

  /* off: the old soft follow, and no pan */
  set("off");
  lw_motion_step(&o, 300, 0.016f);
  NEAR(o.view.parallax[0], 0, 1e-6);

  /* malformed settings are refused and change nothing */
  char out[256];
  char *bad[][6] = {{"motion", "500", "bezier", "2,0,0,1", "0.2", "h"}, {"motion", "500", "wobble", "1,2,3", "0.2", "h"},
                    {"motion", "x", "bezier", "0,0,1,1", "0.2", "h"}, {"motion", "500", "spring", "0,1,1", "0.2", "h"}};
  for (int i = 0; i < 4; i++) {
    lw_motion_command(6, bad[i], out, sizeof out);
    assert(!strncmp(out, "error", 5));
  }
  assert(!M.on);

  /* ---------- windows: Hyprland's window animations ride along in the line ----------
   * (Kinetic's motion module writes them for Hyprland; the engine checks them only:
   * window glass is drawn inside Hyprland by Kinetic's plugin) */
  set("450 bezier 0.65,0.05,0.36,1 0.2 h slide open=300/bezier/0,0,1,1/popin:50 move=200/bezier/0,0,1,1 close=off fade=on");
  assert(M.on && M.ms == 450);
  set("off open=300/bezier/0,0,1,1/slide move=380/spring/1,120,14 close=150/bezier/0,0,1,1/gnomed fade=off");
  assert(!M.on);
  /* malformed window settings are refused */
  char *badw[][3] = {{"motion", "off", "open=300/bezier"}, {"motion", "off", "wobble=1"},
                     {"motion", "off", "move=300/bezier/2,0,0,1"}, {"motion", "off", "close=300/bezier/0,0,1,1/popin:x"}};
  for (int i = 0; i < 4; i++) {
    lw_motion_command(3, badw[i], out, sizeof out);
    assert(!strncmp(out, "error", 5));
  }
  puts("motion tests passed");
  return 0;
}
