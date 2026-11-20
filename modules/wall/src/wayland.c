/* Wayland backend: one layer-shell surface per output, drawn with GLES 3.
 *
 * What keeps this cheap:
 *  - Frames are driven by the compositor's frame callbacks, so nothing is drawn
 *    for an output that is off, locked, or hidden behind a fullscreen window.
 *  - Reduced-resolution rendering goes straight to the swapchain and the
 *    compositor upscales it through wp_viewporter: no extra blit pass.
 *  - A GPU timer query feeds a controller that trims the render resolution
 *    whenever a wallpaper exceeds its per-frame GPU budget.
 */
#include "lw.h"
#include <time.h>
#include <EGL/eglext.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include "cursor-shape-v1-protocol.h"
#include "fractional-scale-v1-protocol.h"
#include "viewporter-protocol.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

#ifndef GL_TIME_ELAPSED_EXT
#define GL_TIME_ELAPSED_EXT 0x88BF
#endif

static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_compositor *compositor;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wp_viewporter *viewporter;
static struct wp_fractional_scale_manager_v1 *frac_manager;
static struct wp_cursor_shape_manager_v1 *cursor_manager;
static struct wl_seat *seat;
static struct wl_pointer *pointer;
static struct lw_output *pointer_output;

static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLConfig egl_config;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface current_surface = EGL_NO_SURFACE;
static bool have_timer_query, checked_ext;

static void create_surface(struct lw_output *o);
static void destroy_surface(struct lw_output *o);
static void watch_arm(double seconds);
static char display_name[64];

struct wl_display *lw_wl_display(void) { return display; }
const char *lw_wl_display_name(void) { return display_name; }

static void make_current(struct lw_output *o) {
  EGLSurface s = o && o->egl ? o->egl : EGL_NO_SURFACE;
  if (s == current_surface && s != EGL_NO_SURFACE) return;
  eglMakeCurrent(egl_display, s, s, egl_context);
  current_surface = s;
  if (!checked_ext) {
    checked_ext = true;
    have_timer_query = lw_gl_has_ext("GL_EXT_disjoint_timer_query");
    E.gl_ready = true;
    lw_dbg("GL: %s / %s, timer query: %d", glGetString(GL_RENDERER), glGetString(GL_VERSION), have_timer_query);
  }
}

/* ---------- sizing ---------- */
void lw_wl_resize(struct lw_output *o) {
  if (!o->configured || !o->surface || o->lw <= 0 || o->lh <= 0) return;
  double scale = o->frac_scale > 0 ? o->frac_scale : (o->int_scale > 0 ? o->int_scale : 1);
  double q = E.cfg.render_scale;
  if (o->wp) q *= lw_wallpaper_info(o->wp)->scale;
  if (E.sys.on_battery && !strcmp(E.cfg.battery_mode, "throttle")) q *= E.cfg.battery_scale;
  if (q > 1) q = 1;
  int base_w = (int)fmax(1, round(o->lw * scale * q)), base_h = (int)fmax(1, round(o->lh * scale * q));
  int bw = (int)fmax(1, round(base_w * o->adaptive)), bh = (int)fmax(1, round(base_h * o->adaptive));
  o->base_w = base_w;
  o->base_h = base_h;
  if (o->window && bw == o->bw && bh == o->bh) return;
  o->bw = bw;
  o->bh = bh;
  if (!o->window) {
    o->window = wl_egl_window_create(o->surface, bw, bh);
    o->egl = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)o->window, NULL);
    make_current(o);
    eglSwapInterval(egl_display, 0); /* never block in swap: frame callbacks pace us */
  } else {
    wl_egl_window_resize(o->window, bw, bh, 0, 0);
  }
  wp_viewport_set_destination(o->viewport, o->lw, o->lh);
  struct wl_region *r = wl_compositor_create_region(compositor);
  wl_region_add(r, 0, 0, o->lw, o->lh);
  wl_surface_set_opaque_region(o->surface, r);
  wl_region_destroy(r);
  o->dirty = true;
}

void lw_wl_update_input(struct lw_output *o) {
  if (!o->surface) return;
  bool wants = E.cfg.pointer && o->wp && lw_wallpaper_info(o->wp)->pointer;
  if (wants) {
    wl_surface_set_input_region(o->surface, NULL);
  } else {
    struct wl_region *r = wl_compositor_create_region(compositor);
    wl_surface_set_input_region(o->surface, r);
    wl_region_destroy(r);
  }
}

/* ---------- wallpaper swap ---------- */
static void drop_snapshot(struct lw_output *o) {
  if (o->snap_tex) glDeleteTextures(1, &o->snap_tex);
  if (o->snap_fbo) glDeleteFramebuffers(1, &o->snap_fbo);
  o->snap_tex = o->snap_fbo = 0;
}

static void take_snapshot(struct lw_output *o) {
  drop_snapshot(o);
  o->snap_w = o->bw;
  o->snap_h = o->bh;
  glGenTextures(1, &o->snap_tex);
  glBindTexture(GL_TEXTURE_2D, o->snap_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, o->bw, o->bh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &o->snap_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, o->snap_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, o->snap_tex, 0);
  struct lw_view v = o->view;
  v.dt = 0;
  lw_wallpaper_render(o->wp, o->snap_fbo, o->bw, o->bh, o->base_w, o->base_h, &v, &E.sys);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  o->snap_t0 = lw_now();
}

bool lw_wl_output_load(struct lw_output *o, const char *id, bool crossfade, char *err, size_t errlen) {
  if (!o->configured || !o->egl) {
    snprintf(err, errlen, "output %s is not ready", o->name);
    return false;
  }
  make_current(o);
  struct lw_wallpaper *wp = lw_wallpaper_load(id, err, errlen);
  if (!wp) return false;
  bool same = o->wp && !strcmp(o->wp_id, lw_wallpaper_info(wp)->id);
  if (same) lw_wallpaper_set_epoch(wp, lw_wallpaper_epoch(o->wp)); /* a reload keeps the same map / seed */
  else lw_wallpaper_shuffle(wp);
  lw_wallpaper_persist(wp, o->name);
  lw_engine_apply_params(wp);
  if (crossfade && o->wp && o->first_frame_done && E.cfg.transition_ms > 0) take_snapshot(o);
  lw_wallpaper_free(o->wp);
  o->wp = wp;
  snprintf(o->wp_id, sizeof o->wp_id, "%s", lw_wallpaper_info(wp)->id);
  if (!same) {
    o->view.time = 0;
    o->adaptive = 1;
    o->gpu_ms = 0;
  }
  lw_wl_resize(o);
  lw_wl_update_input(o);
  o->dirty = true;
  return true;
}

/* ---------- drawing ---------- */
static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
  struct lw_output *o = data;
  wl_callback_destroy(cb);
  o->frame_cb = NULL;
  lw_wl_schedule(o);
}

static const struct wl_callback_listener frame_listener = {frame_done};

static void adapt(struct lw_output *o, double now) {
  struct lw_wallpaper_info *info = lw_wallpaper_info(o->wp);
  if (!E.cfg.adaptive || !info->adaptive || !have_timer_query || o->gpu_ms <= 0) return;
  if (now - o->last_adapt < 1.0 || o->frames < 20) return;
  float budget = E.cfg.gpu_budget_ms, before = o->adaptive;
  if (o->gpu_ms > budget * 1.15f && o->adaptive > 0.4f) {
    /* fragment cost follows pixel count, so scale each axis by the square root */
    o->adaptive = fmaxf(0.4f, o->adaptive * lw_clampf(sqrtf(budget / o->gpu_ms), 0.7f, 0.95f));
  } else if (o->gpu_ms < budget * 0.55f && o->adaptive < 1.0f) {
    o->adaptive = fminf(1.0f, o->adaptive * 1.1f);
  }
  if (o->adaptive != before) {
    o->last_adapt = now;
    lw_dbg("%s: gpu %.2fms, adaptive scale %.2f -> %.2f", o->name, o->gpu_ms, before, o->adaptive);
    lw_wl_resize(o);
  }
}

static double cpu_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Frame rate, average and worst frame interval, and the daemon's CPU use,
 * over half-second windows. */
static void update_stats(struct lw_output *o, double now, struct lw_wallpaper_info *info) {
  (void)info;
  if (o->st_t0 <= 0 || now - o->st_t0 > 2.0) { /* first frame, or resuming from a pause */
    o->st_t0 = now, o->st_cpu0 = cpu_seconds(), o->st_frames = 0, o->st_max_dt = 0;
    return;
  }
  double dt = now - o->last_render;
  if (o->last_render > 0 && dt > o->st_max_dt && dt < 2.0) o->st_max_dt = dt;
  o->st_frames++;
  double span = now - o->st_t0;
  if (span < 0.5) return;
  double cpu = cpu_seconds();
  o->m_fps = (float)(o->st_frames / span);
  o->m_frame_ms = (float)(span * 1000 / o->st_frames);
  o->m_max_ms = (float)(o->st_max_dt * 1000);
  o->m_cpu = (float)((cpu - o->st_cpu0) / span * 100);
  o->st_t0 = now, o->st_cpu0 = cpu, o->st_frames = 0, o->st_max_dt = 0;
}

void lw_wl_render(struct lw_output *o) {
  if (!o->configured || !o->wp || !o->egl || !E.enabled) return;
  struct lw_wallpaper_info *info = lw_wallpaper_info(o->wp);
  double now = lw_now();
  double dt = o->last_render > 0 ? now - o->last_render : 0;
  double maxdt = o->fps > 0 ? fmax(0.1, 1.5 / o->fps) : 0.1;
  if (dt > maxdt) dt = maxdt; /* time does not jump across a pause */
  double wall_dt = dt;
  dt *= o->paused ? 0 : E.cfg.time_scale; /* drawn while paused only to follow a slide: the scene holds still */
  /* Sound makes every wallpaper's clock surge: louder is faster, and an onset
   * (a beat, a notification sound) gives a short kick. */
  float react = lw_wallpaper_sound(o->wp);
  if (react > 0) {
    float since = (float)(now - E.sys.kick_time);
    dt *= 1 + react * (0.9f * E.sys.sound_energy + 1.6f * E.sys.kick_strength * expf(-since * 4));
  }
  make_current(o);
  lw_engine_tick();

  struct lw_view *v = &o->view;
  v->time += dt;
  v->dt = dt;
  bool moving = lw_motion_step(o, now, (float)wall_dt);
  float kp = 1 - expf(-(float)wall_dt * 12), target = o->ptr_inside ? 1.0f : 0.0f;
  o->ptr_sx += (o->ptr_x - o->ptr_sx) * kp;
  o->ptr_sy += (o->ptr_y - o->ptr_sy) * kp;
  o->ptr_presence += (target - o->ptr_presence) * (1 - expf(-(float)wall_dt * 5));
  v->mouse[0] = o->ptr_x;
  v->mouse[1] = o->ptr_y;
  v->mouse[2] = o->ptr_down ? o->ptr_x : -o->ptr_x;
  v->mouse[3] = o->ptr_down ? o->ptr_y : -o->ptr_y;
  v->pointer[0] = o->ptr_sx;
  v->pointer[1] = o->ptr_sy;
  v->pointer[2] = o->ptr_presence;
  v->pointer[3] = o->ptr_click > 0 ? (float)(now - o->ptr_click) : 1e4f;
  /* counted within the monitor's block (1-10 on every monitor), so a reaction to a
   * switch reads the same on each; how far a wallpaper moves is uParallax's job */
  float base = lw_motion_origin(o->ws_id) - 1;
  v->workspace[0] = o->ws_smooth - base;
  v->workspace[1] = (float)(now - o->ws_t0);
  v->workspace[2] = (float)o->ws_id - base;
  v->workspace[3] = (float)o->ws_windows;

  if (have_timer_query) {
    if (!o->query) glGenQueries(1, &o->query);
    if (o->query_pending) {
      GLuint ready = 0;
      glGetQueryObjectuiv(o->query, GL_QUERY_RESULT_AVAILABLE, &ready);
      if (ready) {
        GLuint ns = 0;
        glGetQueryObjectuiv(o->query, GL_QUERY_RESULT, &ns);
        float ms = ns * 1e-6f;
        o->gpu_ms = o->gpu_ms > 0 ? o->gpu_ms * 0.85f + ms * 0.15f : ms;
        o->query_pending = false;
      }
    }
  }
  bool timing = have_timer_query && !o->query_pending;
  if (timing) glBeginQuery(GL_TIME_ELAPSED_EXT, o->query);

  lw_wallpaper_render(o->wp, 0, o->bw, o->bh, o->base_w, o->base_h, v, &E.sys);
  if (lw_wallpaper_take_reset(o->wp)) { /* the screen filled up: crossfade into a fresh start */
    if (E.cfg.transition_ms > 0) take_snapshot(o);
    lw_wallpaper_restart(o->wp);
  }

  if (E.cfg.dim > 0) lw_gl_dim(E.cfg.dim);
  bool fading = false;
  if (o->snap_tex) {
    float t = (float)((now - o->snap_t0) * 1000.0 / (E.cfg.transition_ms > 0 ? E.cfg.transition_ms : 1));
    if (t >= 1) {
      drop_snapshot(o);
    } else {
      t = t * t * (3 - 2 * t);
      lw_gl_blit(o->snap_tex, 1 - t, false, false, 0, 0);
      fading = true;
    }
  }
  lw_lens_draw(o);
  if (timing) {
    glEndQuery(GL_TIME_ELAPSED_EXT);
    o->query_pending = true;
  }

  update_stats(o, now, info);
  if (strcmp(E.cfg.fps_monitor, "off")) {
    char text[256];
    snprintf(text, sizeof text, "%s\n%.0f FPS  (CAP %g)\nFRAME %.2f MS  WORST %.2f\nGPU %.2f MS  CPU %.1f%%\n%dx%d  SCALE %.2f",
             info->title, o->m_fps, o->fps, o->m_frame_ms, o->m_max_ms, o->gpu_ms, o->m_cpu, o->bw, o->bh,
             (double)o->bw / fmax(1, o->lw * (o->frac_scale > 0 ? o->frac_scale : o->int_scale > 0 ? o->int_scale : 1)));
    lw_overlay_draw(text, E.cfg.fps_monitor, o->bw, o->bh);
  }

  o->frame_cb = wl_surface_frame(o->surface);
  wl_callback_add_listener(o->frame_cb, &frame_listener, o);
  eglSwapBuffers(egl_display, o->egl);

  o->last_render = now;
  o->frames++;
  o->dirty = false;
  o->due = 0;
  bool pointer_moving = info->pointer && (fabsf(o->ptr_x - o->ptr_sx) + fabsf(o->ptr_y - o->ptr_sy) > 0.0015f ||
                                          fabsf(target - o->ptr_presence) > 0.01f);
  o->busy = fading || E.sys.theme_animating || pointer_moving || moving;
  if (!o->first_frame_done) {
    o->first_frame_done = true;
    o->closed_n = 0;
    lw_log("%s: showing %s (%dx%d%s), %.1f s after the engine started", o->name, info->id, o->bw, o->bh,
           o->paused ? ", paused" : "", now - E.started);
    lw_shell_check(0.2); /* now it is on screen: is anything over it, is the shell's background to go? */
  }
  adapt(o, now);
}

/* A paused wallpaper still follows a workspace slide (its scene frozen), unless
 * it is hidden behind a fullscreen window. Only wallpapers that move with
 * workspaces and keep no state between frames: a simulation would step on. */
bool lw_wl_drawable(const struct lw_output *o) {
  if (!o->paused || !o->first_frame_done) return true; /* paused from the start: one frame, so it is not blank */
  bool hidden = !o->pause_reason || !strcmp(o->pause_reason, "fullscreen") || !strcmp(o->pause_reason, "disabled");
  if (hidden) return false;
  return lw_motion_moving(o) && lw_wallpaper_follows(o->wp);
}

void lw_wl_schedule(struct lw_output *o) {
  if (!o->configured || !o->wp || !E.enabled || o->frame_cb || !lw_wl_drawable(o)) return;
  bool animate = (o->fps > 0 && !o->paused) || o->busy || lw_motion_moving(o);
  if (!animate && !o->dirty) {
    o->due = 0;
    return;
  }
  float fps = o->busy || lw_motion_moving(o) ? fmaxf(o->fps, 60) : o->paused ? 0 : o->fps;
  if (fps <= 0 || o->last_render <= 0) {
    lw_wl_render(o);
    return;
  }
  double due = o->last_render + 1.0 / fps, now = lw_now();
  if (now >= due - 0.25 / fps) {
    lw_wl_render(o);
  } else if (o->due != due) {
    o->due = due;
    lw_engine_arm_timer();
  }
}

/* ---------- layer surface ---------- */
static void layer_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial, uint32_t w, uint32_t h) {
  struct lw_output *o = data;
  zwlr_layer_surface_v1_ack_configure(ls, serial);
  if (!w || !h) return;
  bool first = !o->configured;
  o->lw = (int)w;
  o->lh = (int)h;
  o->configured = true;
  lw_wl_resize(o);
  if (first || !o->wp)
    lw_engine_apply();
  lw_engine_invalidate(o);
}

/* The compositor took the layer away (an output being reconfigured, say). If the
 * output is still there, a new one is made shortly; a few times in a row at most. */
static void layer_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
  struct lw_output *o = data;
  destroy_surface(o);
  if (o->closed_n++ < 3) {
    lw_log("%s: the compositor closed Kinetic's layer; making a new one", o->name);
    watch_arm(0.5);
  } else {
    lw_log("%s: the compositor keeps closing Kinetic's layer; leaving this output alone", o->name);
  }
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {layer_configure, layer_closed};

static void frac_scale(void *data, struct wp_fractional_scale_v1 *f, uint32_t scale120) {
  struct lw_output *o = data;
  o->frac_scale = scale120 / 120.0;
  lw_wl_resize(o);
  lw_engine_invalidate(o);
}

static const struct wp_fractional_scale_v1_listener frac_listener = {frac_scale};

static void create_surface(struct lw_output *o) {
  if (o->surface || !layer_shell || !compositor) return;
  uint32_t layer = !strcmp(E.cfg.layer, "bottom") ? ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM : ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
  o->surface = wl_compositor_create_surface(compositor);
  o->layer = zwlr_layer_shell_v1_get_layer_surface(layer_shell, o->surface, o->wl, layer, "kinetic-wall");
  zwlr_layer_surface_v1_set_size(o->layer, 0, 0);
  zwlr_layer_surface_v1_set_anchor(o->layer, 15);
  zwlr_layer_surface_v1_set_exclusive_zone(o->layer, -1);
  zwlr_layer_surface_v1_set_keyboard_interactivity(o->layer, 0);
  zwlr_layer_surface_v1_add_listener(o->layer, &layer_listener, o);
  o->viewport = wp_viewporter_get_viewport(viewporter, o->surface);
  if (frac_manager) {
    o->frac = wp_fractional_scale_manager_v1_get_fractional_scale(frac_manager, o->surface);
    wp_fractional_scale_v1_add_listener(o->frac, &frac_listener, o);
  }
  o->adaptive = 1;
  lw_wl_update_input(o);
  wl_surface_commit(o->surface);
  o->mapped = true;
  o->shown_t0 = lw_now();
  watch_arm(5);
}

static void destroy_surface(struct lw_output *o) {
  if (!o->surface) return;
  if (egl_context != EGL_NO_CONTEXT) {
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context);
    current_surface = EGL_NO_SURFACE;
    lw_wallpaper_free(o->wp);
    drop_snapshot(o);
    if (o->query) glDeleteQueries(1, &o->query);
  }
  o->wp = NULL;
  o->wp_id[0] = 0;
  o->query = 0;
  o->query_pending = false;
  if (o->frame_cb) wl_callback_destroy(o->frame_cb);
  if (o->egl) eglDestroySurface(egl_display, o->egl);
  if (o->window) wl_egl_window_destroy(o->window);
  if (o->frac) wp_fractional_scale_v1_destroy(o->frac);
  if (o->viewport) wp_viewport_destroy(o->viewport);
  if (o->layer) zwlr_layer_surface_v1_destroy(o->layer);
  wl_surface_destroy(o->surface);
  o->frame_cb = NULL, o->egl = NULL, o->window = NULL, o->frac = NULL, o->viewport = NULL, o->layer = NULL, o->surface = NULL;
  o->configured = o->mapped = o->first_frame_done = o->busy = false;
  o->due = o->last_render = 0;
  if (pointer_output == o) pointer_output = NULL;
}

int lw_wl_level(void) { return !strcmp(E.cfg.layer, "bottom") ? 1 : 0; }

/* A layer joins the end of its level's list (the top) when it changes level, so a
 * hop to the next level and back puts it over what mapped after it, without a new
 * surface and without dropping the wallpaper. Both commits reach Hyprland together. */
bool lw_wl_raise(struct lw_output *o) {
  if (!o->layer || !o->configured || zwlr_layer_surface_v1_get_version(o->layer) < 2) return false;
  uint32_t mine = lw_wl_level() ? ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM : ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
  uint32_t hop = mine == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND ? ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM : ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
  zwlr_layer_surface_v1_set_layer(o->layer, hop);
  wl_surface_commit(o->surface);
  zwlr_layer_surface_v1_set_layer(o->layer, mine);
  wl_surface_commit(o->surface);
  wl_display_flush(display);
  return true;
}

void lw_wl_remap(struct lw_output *o) {
  lw_log("%s: making Kinetic's layer afresh", o->name);
  destroy_surface(o);
  if (E.enabled) create_surface(o);
}

/* One timer for what the compositor may leave undone: a layer it closed on an
 * output that is still there, or a layer with no first frame a few seconds on
 * (never configured, or no wallpaper would load). Bounded, and every step logged. */
static int watch_fd = -1, remade;

static void watch_fire(void *data, uint32_t ev) {
  uint64_t n;
  if (read(watch_fd, &n, sizeof n) < 0 || !E.enabled) return;
  double now = lw_now();
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    if (!o->surface) {
      if (o->closed_n > 0 && o->closed_n <= 3) create_surface(o);
      continue;
    }
    if (o->first_frame_done || now - o->shown_t0 < 4.5) continue;
    if (!o->configured) {
      lw_log("%s: the compositor has not configured Kinetic's layer after %.0f s", o->name, now - o->shown_t0);
      if (remade++ < 3) lw_wl_remap(o);
    } else if (!o->wp) {
      lw_log("%s: no wallpaper would load (%s); showing Omarchy's background instead", o->name,
             E.last_error[0] ? E.last_error : "no error given");
      char err[512];
      if (!lw_wl_output_load(o, "still", false, err, sizeof err)) lw_log("%s: still: %s", o->name, err);
    } else {
      lw_log("%s: %s is loaded but no frame has been drawn yet (%s)", o->name, o->wp_id,
             o->paused ? o->pause_reason : o->frame_cb ? "waiting for the compositor's frame callback" : "not scheduled");
      lw_engine_invalidate(o);
    }
  }
}

static void watch_arm(double seconds) {
  if (watch_fd < 0) {
    watch_fd = lw_debounce_fd();
    lw_loop_add(watch_fd, watch_fire, NULL);
  }
  lw_debounce_arm(watch_fd, seconds);
}

void lw_wl_map_all(bool on) {
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    if (on)
      create_surface(o);
    else
      destroy_surface(o);
  }
}

/* ---------- outputs ---------- */
static void out_geometry(void *d, struct wl_output *w, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sub,
                         const char *make, const char *model, int32_t tr) {}
static void out_mode(void *d, struct wl_output *w, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
  struct lw_output *o = d;
  if (flags & WL_OUTPUT_MODE_CURRENT) o->refresh_mhz = refresh;
}
static void out_done(void *d, struct wl_output *w) {
  struct lw_output *o = d;
  if (E.enabled) create_surface(o);
}
static void out_scale(void *d, struct wl_output *w, int32_t scale) {
  struct lw_output *o = d;
  o->int_scale = scale;
  if (o->frac_scale <= 0) lw_wl_resize(o);
}
static void out_name(void *d, struct wl_output *w, const char *name) {
  struct lw_output *o = d;
  snprintf(o->name, sizeof o->name, "%s", name);
}
static void out_desc(void *d, struct wl_output *w, const char *desc) {}

static const struct wl_output_listener output_listener = {out_geometry, out_mode, out_done, out_scale, out_name, out_desc};

/* ---------- pointer ---------- */
static struct lw_output *output_for(struct wl_surface *s) {
  for (struct lw_output *o = E.outputs; o; o = o->next)
    if (o->surface == s) return o;
  return NULL;
}

static void ptr_move(struct lw_output *o, wl_fixed_t x, wl_fixed_t y) {
  if (!o || o->lw <= 0) return;
  o->ptr_x = lw_clampf((float)(wl_fixed_to_double(x) / o->lw), 0, 1);
  o->ptr_y = lw_clampf(1.0f - (float)(wl_fixed_to_double(y) / o->lh), 0, 1);
  lw_engine_invalidate(o);
}

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y) {
  pointer_output = output_for(s);
  if (cursor_manager) {
    struct wp_cursor_shape_device_v1 *dev = wp_cursor_shape_manager_v1_get_pointer(cursor_manager, p);
    wp_cursor_shape_device_v1_set_shape(dev, serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
    wp_cursor_shape_device_v1_destroy(dev);
  }
  if (!pointer_output) return;
  pointer_output->ptr_inside = true;
  ptr_move(pointer_output, x, y);
}
static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s) {
  if (pointer_output) {
    pointer_output->ptr_inside = pointer_output->ptr_down = false;
    lw_engine_invalidate(pointer_output);
  }
  pointer_output = NULL;
}
static void ptr_motion(void *d, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
  ptr_move(pointer_output, x, y);
}
static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
  if (!pointer_output) return;
  pointer_output->ptr_down = state == WL_POINTER_BUTTON_STATE_PRESSED;
  if (pointer_output->ptr_down) pointer_output->ptr_click = lw_now();
  lw_engine_invalidate(pointer_output);
}
static void ptr_axis(void *d, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t v) {}
static void ptr_frame(void *d, struct wl_pointer *p) {}
static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t s) {}
static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {}
static void ptr_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {}

static const struct wl_pointer_listener pointer_listener = {
    ptr_enter, ptr_leave, ptr_motion, ptr_button, ptr_axis, ptr_frame, ptr_axis_source, ptr_axis_stop, ptr_axis_discrete};

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps) {
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer && E.cfg.pointer) {
    pointer = wl_seat_get_pointer(s);
    wl_pointer_add_listener(pointer, &pointer_listener, NULL);
  } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && pointer) {
    wl_pointer_destroy(pointer);
    pointer = NULL;
  }
}
static void seat_name(void *d, struct wl_seat *s, const char *name) {}
static const struct wl_seat_listener seat_listener = {seat_caps, seat_name};

/* ---------- registry ---------- */
static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version) {
  if (!strcmp(iface, wl_compositor_interface.name)) {
    compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
  } else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
    layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, version < 4 ? version : 4);
  } else if (!strcmp(iface, wp_viewporter_interface.name)) {
    viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
  } else if (!strcmp(iface, wp_fractional_scale_manager_v1_interface.name)) {
    frac_manager = wl_registry_bind(r, name, &wp_fractional_scale_manager_v1_interface, 1);
  } else if (!strcmp(iface, wp_cursor_shape_manager_v1_interface.name)) {
    cursor_manager = wl_registry_bind(r, name, &wp_cursor_shape_manager_v1_interface, 1);
  } else if (!strcmp(iface, wl_seat_interface.name) && !seat) {
    seat = wl_registry_bind(r, name, &wl_seat_interface, version < 5 ? version : 5);
    wl_seat_add_listener(seat, &seat_listener, NULL);
  } else if (!strcmp(iface, wl_output_interface.name) && version >= 4) {
    struct lw_output *o = calloc(1, sizeof *o);
    o->global = name;
    o->int_scale = 1;
    o->adaptive = 1;
    o->ws_id = 1;
    o->ws_smooth = 1;
    o->ptr_x = o->ptr_y = o->ptr_sx = o->ptr_sy = 0.5f;
    o->wl = wl_registry_bind(r, name, &wl_output_interface, 4);
    wl_output_add_listener(o->wl, &output_listener, o);
    struct lw_output **tail = &E.outputs;
    while (*tail) tail = &(*tail)->next;
    *tail = o;
  }
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t name) {
  for (struct lw_output **p = &E.outputs; *p; p = &(*p)->next) {
    struct lw_output *o = *p;
    if (o->global != name) continue;
    destroy_surface(o);
    wl_output_destroy(o->wl);
    *p = o->next;
    free(o);
    lw_engine_arm_timer();
    return;
  }
}

static const struct wl_registry_listener registry_listener = {reg_global, reg_remove};

/* Under uwsm the service starts once the compositor is up and its variables are
 * exported, but a session started another way may get here first: wait for the
 * display a little (about 20 s, backing off) before giving up to systemd's restart. */
static struct wl_display *connect_display(void) {
  double waited = 0, step = 0.1;
  for (int attempt = 0;; attempt++) {
    const char *name = getenv("WAYLAND_DISPLAY");
    struct wl_display *d = wl_display_connect(NULL);
    if (d) {
      snprintf(display_name, sizeof display_name, "%s", name ? name : "wayland-0");
      if (attempt) lw_log("connected to Wayland display %s after %.1f s", display_name, waited);
      return d;
    }
    int err = errno;
    if (waited >= 20) {
      lw_log("cannot connect to Wayland display %s (%s), gave up after %.0f s", name ? name : "(WAYLAND_DISPLAY is not set)",
             strerror(err), waited);
      return NULL;
    }
    if (!attempt)
      lw_log("Wayland display %s is not there yet (%s); waiting for the compositor", name ? name : "(WAYLAND_DISPLAY is not set)",
             strerror(err));
    struct timespec ts = {(time_t)step, (long)((step - (time_t)step) * 1e9)};
    nanosleep(&ts, NULL);
    waited += step;
    step = fmin(step * 2, 2);
  }
}

bool lw_wl_init(void) {
  display = connect_display();
  if (!display) return false;
  egl_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display, NULL);
  if (egl_display == EGL_NO_DISPLAY || !eglInitialize(egl_display, NULL, NULL) || !eglBindAPI(EGL_OPENGL_ES_API)) {
    lw_log("EGL initialization failed");
    return false;
  }
  const EGLint cfg[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8,
                        EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
                        EGL_NONE};
  EGLint n = 0;
  if (!eglChooseConfig(egl_display, cfg, &egl_config, 1, &n) || n < 1) {
    lw_log("no usable EGL config");
    return false;
  }
  const EGLint ctx[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, ctx);
  if (egl_context == EGL_NO_CONTEXT) {
    lw_log("cannot create a GLES 3 context");
    return false;
  }
  registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(display);
  if (!compositor || !layer_shell || !viewporter) {
    lw_log("compositor lacks wl_compositor, wlr-layer-shell or wp_viewporter");
    return false;
  }
  wl_display_roundtrip(display); /* output names and modes; surfaces are created on wl_output.done */
  int outputs = 0;
  for (struct lw_output *o = E.outputs; o; o = o->next) outputs++;
  if (!outputs) lw_log("the compositor lists no outputs yet; Kinetic's layers come as outputs appear");
  return true;
}

void lw_wl_fini(void) {
  if (!display) return;
  for (struct lw_output *o = E.outputs; o; o = o->next) destroy_surface(o);
  if (egl_context != EGL_NO_CONTEXT) {
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(egl_display, egl_context);
  }
  wl_display_flush(display);
  eglTerminate(egl_display);
  wl_display_disconnect(display);
  display = NULL;
}
