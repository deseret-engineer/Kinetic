/* Omarchy shell module: keeps Kinetic's wallpaper the one on screen.
 *
 * Three other things draw a desktop background on Omarchy:
 *  - the shell (Quickshell) draws the still on its own layer, omarchy-background,
 *    and shows covers under that name too: a black one over everything at login,
 *    one on the bottom level during theme changes and after a shell restart. Its
 *    covers fade only once its background has been up and ready;
 *  - owe (owed + owe-render) plays video backgrounds and the login intro on its
 *    layer, owe-background, and hands the still back to the shell when it is done.
 *
 * Whoever maps last is on top, and at login all three start within a second or
 * two of each other, in no fixed order: the shell before its IPC answers, owe's
 * intro after Kinetic, the shell's background again when the intro ends. So
 * nothing here is done once and assumed to have worked. Each time one of those
 * layers comes or goes (Hyprland's openlayer / closelayer events), and once
 * Kinetic has drawn its first frame, the guard reads what Hyprland actually
 * stacks (j/layers) and acts:
 *  - a shell cover is up: wait for it to fade (it needs the shell's background;
 *    one that lingers gets it back until it is gone);
 *  - the shell's background is up: suspend it (it costs frames even when hidden);
 *  - owe's layer is over Kinetic's: if owe is playing Omarchy's login or theme
 *    intro, let it finish; otherwise (a video background's player, a renderer
 *    left up) move Kinetic's layer back on top.
 * Then it looks again after a growing delay until things are right, a bounded
 * number of times, and logs what it saw and did, so a bad login can be read
 * back with `journalctl --user -u kinetic-wall`. Off, it hands everything back. */
#include "lw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool suspended; /* we asked the shell to suspend its background */

static void shell_answer(void *data, int status, const char *out) {
  const char *what = data;
  if (status == 0) return;
  lw_log("shell: asking it to %s its background failed (%s)", what,
         status == 124 ? "no answer within 3 s" : *out ? out : "omarchy-shell exited with an error");
}

static void shell_call(bool suspend) {
  char *argv[] = {"omarchy-shell", "background", "setSuspended", suspend ? "true" : "false", NULL};
  lw_run(argv, 3, shell_answer, suspend ? "suspend" : "restore");
}

static int guard_fd = -1;
static void guard_arm(double delay);
static void guard_reset(void);

void lw_shell_suspend(bool on) {
  if (on) {
    if (E.cfg.suspend_shell) lw_shell_check(0); /* only once it is known to be up, and with the guard's checks */
    return;
  }
  if (guard_fd >= 0) lw_debounce_arm(guard_fd, 0); /* nothing to guard any more */
  guard_reset();
  if (!suspended) return;
  suspended = false;
  shell_call(false);
  lw_log("shell: its background is back");
}

/* ---------- the guard ---------- */
static const double backoff[] = {1, 2, 4, 8, 15}; /* after acting: look again */
#define TRIES (int)(sizeof backoff / sizeof backoff[0])
static int tries;           /* actions in this episode that have not worked yet */
static int missing;         /* looks that did not find Kinetic's layer in Hyprland's list */
static bool acted;          /* this episode did something: say so when it is over */
static double cover_t0;     /* a shell cover has been up since then */
static bool cover_helped;   /* we gave the shell its background back so a cover could finish */
static double intro_t0;     /* owe has been playing an intro over Kinetic since then */
static bool owe_asking;     /* `owe status` is running */
static int owe_intro = -1;  /* its answer: 1 an intro is playing, 0 not, -1 not asked yet */
static double window_t0;    /* rate limit: actions in the last five minutes */
static int window_n;
static bool gave_up;

static bool allow_action(void) {
  double now = lw_now();
  if (now - window_t0 > 300) window_t0 = now, window_n = 0, gave_up = false;
  if (window_n++ < 20) return true;
  if (!gave_up) lw_log("guard: something keeps covering Kinetic's wallpaper; leaving it alone for a few minutes");
  gave_up = true;
  return false;
}

static void owe_answer(void *data, int status, const char *out) {
  owe_asking = false;
  owe_intro = status == 0 && strstr(out, "\"intro\":true") ? 1 : 0;
  guard_arm(0);
}

static void episode_over(void) {
  if (acted) lw_log("guard: Kinetic's wallpaper is on top%s", suspended ? ", the shell's background suspended" : "");
  tries = missing = 0;
  acted = cover_helped = false;
  cover_t0 = intro_t0 = 0;
  owe_intro = -1;
  for (struct lw_output *o = E.outputs; o; o = o->next) o->guard_raised = false;
}

static void guard_reset(void) {
  acted = false;
  episode_over();
}

/* Hyprland's layers cannot be read (another compositor, or Hyprland not reachable):
 * ask the shell and repeat it a few times, in case it was not answering yet. */
static void blind(void) {
  if (!E.cfg.suspend_shell || tries >= TRIES) return;
  if (!tries) lw_log("shell: Hyprland's layers cannot be read; suspending the shell's background without checking");
  suspended = true;
  shell_call(true);
  guard_arm(backoff[tries++]);
}

static void check(void) {
  if (!E.enabled) return;
  bool shown = false;
  for (struct lw_output *o = E.outputs; o; o = o->next) shown |= o->first_frame_done;
  if (!shown) return; /* the first frame looks again */
  char *json = lw_hypr_request("j/layers");
  if (!json) {
    blind();
    return;
  }
  double now = lw_now();
  bool any = false, cover = false, shell_bg = false, shell_above = false, owe_above = false;
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    struct lw_layer_scan s;
    o->guard_shell = o->guard_owe = false;
    if (!o->first_frame_done || !lw_layers_scan(json, o->name, (int)getpid(), &s) || !s.ours) continue;
    any = true;
    cover |= s.shell_cover;
    shell_bg |= s.shell_bg;
    shell_above |= s.shell_above;
    owe_above |= s.owe_above;
    o->guard_shell = s.shell_above;
    o->guard_owe = s.owe_above;
    if (s.other_above[0]) lw_dbg("guard: %s is over Kinetic's layer on %s", s.other_above, o->name);
  }
  free(json);

  if (!any) { /* drawn, but not in Hyprland's list yet (or listed under another pid) */
    if (missing < 4) {
      guard_arm(0.5 * (1 << missing++)); /* 0.5, 1, 2, 4 s */
    } else if (missing++ == 4 && E.cfg.suspend_shell) {
      lw_log("guard: Kinetic's layer is not in Hyprland's layer list; suspending the shell's background without checking");
      suspended = true;
      shell_call(true);
    }
    return;
  }
  missing = 0;

  /* 1. The shell is fading one of its covers in or out: it needs its background for that. */
  if (cover) {
    if (!cover_t0) {
      cover_t0 = now;
      lw_log("guard: the shell's cover is up (a login, a theme change or a shell restart); waiting for it to fade");
    }
    if (!shell_bg && suspended && !cover_helped && now - cover_t0 > 12) {
      /* a cover the shell lifts only once its background is ready, and it is suspended */
      lw_log("shell: its cover is waiting for its background; giving the background back until the cover is gone");
      cover_helped = acted = true;
      suspended = false;
      shell_call(false);
    }
    if (now - cover_t0 < 60) guard_arm(2); /* its closelayer event comes sooner, normally */
    else if (now - cover_t0 < 62) lw_log("guard: the shell's cover has been up for a minute; waiting for it to go");
    return;
  }
  cover_t0 = 0;

  /* 2. owe's layer is over Kinetic's: is it an intro? Ask first, act on the answer. */
  if (owe_above && owe_intro < 0) {
    if (!owe_asking) {
      owe_asking = true;
      char *argv[] = {"owe", "status", NULL};
      lw_run(argv, 2, owe_answer, NULL); /* comes back here */
    }
    return;
  }
  bool intro = owe_above && owe_intro == 1;
  owe_intro = -1; /* asked afresh next time */

  bool want = false, act = false;
  if ((shell_bg && E.cfg.suspend_shell) || shell_above || (owe_above && !intro)) {
    want = true;
    act = tries < TRIES && allow_action();
  }

  /* 3. The shell's own background is up: suspend it, or, when it is to stay, get above it. */
  if (act && shell_bg && E.cfg.suspend_shell) {
    if (!tries) lw_log("shell: its background is %s Kinetic's; suspending it", shell_above ? "over" : "under");
    else lw_log("shell: its background is still up (try %d of %d); asking again", tries + 1, TRIES);
    suspended = acted = true;
    shell_call(true);
  } else if (act && shell_above) {
    lw_log("shell: its background is over Kinetic's and is to stay (suspend_shell = false); moving Kinetic's layer up");
    acted = true;
    for (struct lw_output *o = E.outputs; o; o = o->next)
      if (o->guard_shell) lw_wl_raise(o);
  }

  /* 4. owe: an intro plays out (bounded); anything else goes under Kinetic. */
  if (intro) {
    if (!intro_t0) {
      intro_t0 = now;
      lw_log("guard: Omarchy's intro is playing over Kinetic (owe); the wallpaper shows once it ends");
    }
    if (now - intro_t0 < 45) {
      guard_arm(want ? backoff[tries < TRIES ? tries++ : TRIES - 1] : 3); /* normally the shell's background coming back ends it */
      return;
    }
    if (!want) act = tries < TRIES && allow_action();
    want = true;
  }
  if (act && owe_above && (!intro || now - intro_t0 >= 45)) {
    lw_log("guard: owe's layer is over Kinetic's (%s); moving Kinetic's layer up",
           intro ? "its intro has run for 45 s" : "a video background's player, or its renderer left up");
    acted = true;
    for (struct lw_output *o = E.outputs; o; o = o->next) {
      if (!o->guard_owe) continue;
      /* raised once already and still under (or it cannot change level): a new surface lands on top */
      if (o->guard_raised || !lw_wl_raise(o)) lw_wl_remap(o);
      o->guard_raised = true;
    }
  }

  if (!want) {
    episode_over();
    return;
  }
  if (tries < TRIES) {
    guard_arm(backoff[tries++]);
  } else if (tries == TRIES) {
    tries++;
    lw_log("guard: Kinetic's wallpaper is still covered after %d tries; trying again when the layers change", TRIES);
  }
}

static void guard_fire(void *data, uint32_t ev) {
  uint64_t n;
  if (read(guard_fd, &n, sizeof n) < 0) return;
  check();
}

static void guard_arm(double delay) {
  if (guard_fd < 0) {
    guard_fd = lw_debounce_fd();
    lw_loop_add(guard_fd, guard_fire, NULL);
  }
  lw_debounce_arm(guard_fd, delay > 0 ? delay : 0.001);
}

void lw_shell_check(double delay) {
  if (!E.enabled) return;
  if (tries > TRIES) tries = 0; /* gave up on this episode; something changed, so try again */
  guard_arm(delay);
}

static bool start(void) { return true; }

/* The engine is stopping: hand the background back and wait for the shell to take
 * it, since systemd ends whatever the service left running once the engine exits. */
static void stop(void) {
  if (guard_fd >= 0) close(guard_fd);
  guard_fd = -1;
  if (!suspended) return;
  suspended = false;
  char *argv[] = {"omarchy-shell", "background", "setSuspended", "false", NULL};
  char out[256];
  int status = lw_run_wait(argv, 3, out, sizeof out);
  if (status) lw_log("shell: giving its background back failed (%s)", *out ? out : "no answer");
  else lw_log("shell: its background is back");
}

struct lw_module lw_mod_shell = {"shell", start, stop, false};

#ifndef LW_AUDIO
void lw_audio_set_active(bool on) {}
void lw_audio_update(struct lw_sys *sys) {}
#endif
