/* kinetic-wall — the living wallpaper engine of Kinetic, for Omarchy.
 * One shared header: the engine is small enough that every part can see the whole. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <GLES3/gl3.h>
#include <EGL/egl.h>

#define LW_MAX_BUFFERS 8
#define LW_MAX_PASSES (LW_MAX_BUFFERS + 1)
#define LW_MAX_CHANNELS 8
#define LW_MAX_PARAMS 40
#define LW_MAX_PRESETS 16
#define LW_MAX_OPTIONS 12
#define LW_PATH 512
#define LW_AUDIO_BINS 512

/* ---------- util ---------- */
extern int lw_verbose;
double lw_now(void); /* monotonic seconds */
void lw_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void lw_dbg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *lw_read_file(const char *path, size_t *len);
const char *lw_home(void);
void lw_dir_config(char *out, const char *rel);  /* ~/.config/kinetic/<rel> */
void lw_dir_state(char *out, const char *rel);   /* ~/.local/state/kinetic/wall/<rel> */
void lw_dir_cache(char *out, const char *rel);   /* ~/.cache/kinetic/wall/<rel> */
void lw_dir_omarchy(char *out, const char *rel); /* ~/.local/state/omarchy/current/<rel> */
void lw_socket_path(char *out);
void lw_signal_path(char *out, const char *name);
int lw_mkdir_p(const char *path);
bool lw_parse_hex(const char *s, float rgb[3]);
void lw_spawn(char *const argv[]); /* fire and forget */
/* Runs argv without waiting (bounded by a timeout); cb gets its exit status (124: timed
 * out, 127: not found) and its output, stdout and stderr together, trimmed. */
typedef void (*lw_run_cb)(void *data, int status, const char *output);
void lw_run(char *const argv[], double timeout, lw_run_cb cb, void *data);
int lw_run_wait(char *const argv[], double timeout, char *out, size_t len); /* the same, waiting (shutdown only) */
float lw_clampf(float v, float lo, float hi);
struct lw_buf {
  char *s;
  size_t n, cap;
};
void lw_buf_printf(struct lw_buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void lw_buf_json(struct lw_buf *b, const char *str); /* appends a quoted, escaped JSON string */

/* ---------- just enough JSON for Hyprland's replies (json.c) ---------- */
const char *lw_json_skip(const char *p);                 /* the end of the value at p */
const char *lw_json_next(const char *p);                 /* past the value at p, its comma and spaces */
const char *lw_json_get(const char *obj, const char *key); /* obj at '{': the value of key, or NULL */
void lw_json_str(const char *v, char *out, size_t len);  /* a string value, unescaped enough for names */
/* Walk an object's members or an array's items: start *it at '{' or '[';
 * each call returns the next value (and its key), NULL at the end. */
const char *lw_json_member(const char **it, char *key, size_t keylen);
const char *lw_json_item(const char **it);

/* ---------- what Hyprland stacks over Kinetic's layer (layers.c) ---------- */
struct lw_layer_scan {
  bool ours;          /* Kinetic's layer (this process) is on the output */
  int level, index;   /* where: Hyprland's level (0 background, 1 bottom) and place in it */
  bool shell_bg;      /* the shell's wallpaper layer is up (omarchy-background, background level) */
  bool shell_above;   /* ...and drawn over Kinetic's */
  bool shell_cover;   /* one of the shell's startup or theme covers is up: it fades by itself */
  bool owe_above;     /* owe's layer (a video background, a login intro) is drawn over Kinetic's */
  char other_above[64]; /* some other layer over Kinetic's, for the log */
};
bool lw_layers_scan(const char *json, const char *output, int pid, struct lw_layer_scan *s);

/* ---------- toml subset ---------- */
enum { LW_T_STR, LW_T_NUM, LW_T_BOOL, LW_T_ARR };
struct lw_tval {
  int type;
  char *s;
  double n;
  struct lw_tval *arr;
  int len;
};
struct lw_toml;
struct lw_toml *lw_toml_parse(const char *text);
struct lw_toml *lw_toml_load(const char *path);
void lw_toml_free(struct lw_toml *t);
const struct lw_tval *lw_toml_get(const struct lw_toml *t, const char *table, const char *key);
const char *lw_toml_str(const struct lw_toml *t, const char *table, const char *key, const char *def);
double lw_toml_num(const struct lw_toml *t, const char *table, const char *key, double def);
bool lw_toml_bool(const struct lw_toml *t, const char *table, const char *key, bool def);
int lw_toml_count(const struct lw_toml *t, const char *array_table); /* number of [[name]] tables */
/* iterate the keys of one table; returns false when idx runs past the end */
const char *lw_toml_table(const struct lw_toml *t, int idx); /* distinct table names, in file order */
bool lw_toml_each(const struct lw_toml *t, const char *table, int idx, const char **key, const struct lw_tval **val);

/* ---------- images ---------- */
/* Returns malloc'd RGBA8, top row first. png, jpeg and webp. */
uint8_t *lw_image_load(const char *path, int *w, int *h);
bool lw_image_save_png(const char *path, const uint8_t *rgba, int w, int h, bool flip);

/* ---------- gfx helpers ---------- */
GLuint lw_gl_program(const char *vs, const char *fs, char *err, size_t errlen);
GLuint lw_gl_texture_rgba(const uint8_t *rgba, int w, int h, bool mipmap, bool repeat);
GLuint lw_gl_texture_file(const char *path, int *w, int *h, bool repeat, int max_dim);
void lw_gl_dim(float amount); /* darken the bound framebuffer */
void lw_gl_blit(GLuint tex, float alpha, bool flip_y, bool cover, float tex_aspect, float dst_aspect); /* draws to bound fbo */
bool lw_gl_has_ext(const char *name);
extern const char *lw_vs_fullscreen;
void lw_overlay_draw(const char *text, const char *corner, int fb_w, int fb_h);
int lw_engine_stats(char *out, size_t len);
int lw_ipc_query(const char *cmd, char *buf, size_t len);

/* ---------- system state shared with shaders ---------- */
struct lw_theme {
  char name[64];
  float bg[3], fg[3], accent[3];
  float palette[16][3];
  float dark; /* 1 = dark theme */
};

struct lw_sys {
  struct lw_theme theme;      /* what shaders see right now (interpolated) */
  struct lw_theme theme_from; /* start of a running theme transition */
  struct lw_theme theme_to;
  double theme_t0;
  bool theme_animating;

  bool has_battery, on_battery, charging;
  float battery; /* 0..1 */

  float audio[4]; /* bass, mid, treble, level */
  /* sound reactions: a loudness envelope and onsets ("kicks": beats, notification sounds) */
  float sound_energy, kick_strength;
  double kick_time;
  int kicks;
  GLuint audio_tex;
  bool audio_wanted, audio_running;

  GLuint bg_tex; /* the user's current Omarchy still, for "@background" channels */
  int bg_w, bg_h;
  bool bg_stale;

  double focus_time; /* monotonic time of the last active-window change */
};

/* Per-view frame inputs. */
struct lw_view {
  double time, dt;
  int frame;
  float mouse[4];     /* Shadertoy iMouse, pixels */
  float pointer[4];   /* smoothed x, y in 0..1, presence 0..1, seconds since click */
  float workspace[4]; /* smoothed index, seconds since switch, index, window count */
  float parallax[2];  /* how far the view has panned, in screens (motion.c) */
};

/* ---------- wallpapers ---------- */
enum { LW_P_FLOAT, LW_P_INT, LW_P_BOOL, LW_P_CHOICE, LW_P_BITS, LW_P_COLOR };
struct lw_param {
  char name[32], label[64], desc[160];
  int type;
  float value[3], def[3]; /* colors use all three; everything else value[0] */
  float min, max, step;
  int nopt, bits;
  bool restart; /* changing it restarts the wallpaper's simulation (seeds, rule sets) */
  char options[LW_MAX_OPTIONS][32];
};

struct lw_preset {
  char name[40];
  int n;
  char key[10][32], val[10][32];
};

struct lw_wallpaper;
struct lw_wallpaper_info {
  char id[64], dir[LW_PATH];
  char title[96], author[96], description[256];
  float fps;   /* 0 = static: redraw only when an input changes */
  float scale; /* preferred render scale */
  float def_fps, def_scale; /* what the manifest asked for, before user overrides */
  int npresets;
  struct lw_preset presets[LW_MAX_PRESETS];
  float preview_time;
  float preview_warmup; /* seconds of simulation before a preview is captured */
  bool audio, pointer, adaptive, background;
  bool fill, fill_reverse; /* [fill]: the engine watches how full the screen gets */
  float parallax; /* how much the image pass pans with workspace parallax: 0 = not at all (centered scenes, simulations) */
  int nparams;
  struct lw_param params[LW_MAX_PARAMS];
  int nbuffers;
};

bool lw_wallpaper_find(const char *id, char dir[LW_PATH]);
typedef void (*lw_wallpaper_cb)(const char *id, const char *dir, void *data);
void lw_wallpaper_each(lw_wallpaper_cb cb, void *data);
bool lw_wallpaper_read_info(const char *dir, struct lw_wallpaper_info *info, char *err, size_t errlen);
struct lw_wallpaper *lw_wallpaper_load(const char *id, char *err, size_t errlen);
void lw_wallpaper_free(struct lw_wallpaper *wp);
float lw_wallpaper_epoch(struct lw_wallpaper *wp);
void lw_wallpaper_set_epoch(struct lw_wallpaper *wp, float e);
void lw_wallpaper_shuffle(struct lw_wallpaper *wp); /* random uEpoch, so each load differs */
void lw_wallpaper_request_restart(struct lw_wallpaper *wp); /* start over (and forget saved state) on the next frame */
void lw_wallpaper_persist(struct lw_wallpaper *wp, const char *key); /* keep persistent buffers under this key */
struct lw_wallpaper_info *lw_wallpaper_info(struct lw_wallpaper *wp);
bool lw_wallpaper_follows(struct lw_wallpaper *wp); /* moves with workspaces, with no frame-to-frame state */
/* base_w/h: nominal full-quality size, used to size feedback buffers so that
 * adaptive scaling of the final pass never reallocates them. */
void lw_wallpaper_render(struct lw_wallpaper *wp, GLuint target_fbo, int w, int h, int base_w, int base_h,
                         const struct lw_view *view, struct lw_sys *sys);
bool lw_wallpaper_set_param(struct lw_wallpaper *wp, const char *name, const char *value);
float lw_wallpaper_sound(struct lw_wallpaper *wp);      /* effective sound reaction amount, 0 = off */
bool lw_wallpaper_take_reset(struct lw_wallpaper *wp);
float lw_wallpaper_coverage(struct lw_wallpaper *wp);   /* 0..1 from the fill monitor, -1 without one */  /* the fill monitor wants a fresh start */
void lw_wallpaper_restart(struct lw_wallpaper *wp);     /* reseed every buffer with a new epoch */
void lw_param_format(const struct lw_param *p, const float v[3], char out[32]);

/* ---------- outputs ---------- */
struct lw_output {
  struct lw_output *next;
  char name[64];
  uint32_t global;
  struct wl_output *wl;
  struct wl_surface *surface;
  struct zwlr_layer_surface_v1 *layer;
  struct wp_viewport *viewport;
  struct wp_fractional_scale_v1 *frac;
  struct wl_egl_window *window;
  EGLSurface egl;
  struct wl_callback *frame_cb;

  int lw, lh;         /* logical size */
  int bw, bh;         /* current buffer size */
  int base_w, base_h; /* buffer size before adaptive scaling */
  int int_scale;
  double frac_scale; /* 0 until the compositor reports one */
  int refresh_mhz;
  bool configured, mapped, dirty, first_frame_done;
  bool busy; /* a transient animation (crossfade, theme fade, pointer easing) is still running */
  double due, last_render;

  struct lw_wallpaper *wp;
  char wp_id[64];
  struct lw_view view;

  GLuint snap_tex, snap_fbo; /* frozen frame of the previous wallpaper, for crossfades */
  int snap_w, snap_h;
  double snap_t0;

  bool paused;
  const char *pause_reason;
  float fps;      /* effective cap right now */
  float adaptive; /* 0.35..1 factor applied on top of the configured scale */
  GLuint query;
  bool query_pending;
  float gpu_ms;
  double last_adapt;
  uint64_t frames;
  /* measured over half-second windows, for the FPS monitor and `kinetic wall stats` */
  double st_t0, st_cpu0, st_max_dt;
  int st_frames;
  float m_fps, m_frame_ms, m_max_ms, m_cpu;

  int ws_id, ws_windows;
  int ws_prev; /* the workspace the running switch left */
  bool ws_fullscreen;
  double ws_t0;
  float ws_smooth, ws_from; /* the eased workspace position, and where the running switch started */
  struct { /* a pass-through: Hyprland steps through every workspace between (see motion.c) */
    bool on;
    double t0;
    float from, to, secs; /* positions, and how long the whole trip takes */
    float p[4];           /* the trip's curve: the switch curve, as Hyprland plays it step by step */
    int n;
  } travel;

  bool ptr_inside, ptr_down;
  float ptr_x, ptr_y; /* 0..1, y up */
  float ptr_sx, ptr_sy, ptr_presence;
  double ptr_click;

  /* what the guard (mod_shell.c) last saw over this output's layer, and did */
  bool guard_shell, guard_owe, guard_raised;
  double shown_t0; /* when its layer surface was made: the first frame should follow soon */
  int closed_n;    /* the compositor closed its layer this many times in a row */
};

/* ---------- engine ---------- */
struct lw_config {
  char wallpaper[64];
  float fps, fps_occupied, render_scale, gpu_budget_ms;
  bool adaptive, pointer, suspend_shell, pause_fullscreen, pause_occupied, hot_reload;
  float transition_ms, theme_fade_ms, time_scale, dim, audio_gain;
  float shuffle_minutes;
  bool sound_react;
  float sound_strength;
  char shuffle_order[16]; /* next | random */
  char layer[16];
  char fps_monitor[16]; /* off | top-left | top-right | bottom-left | bottom-right */
  /* glass: the still background kept for it, and glass behind windows (see lens.c) */
  bool glass_still;
  char window_glass[16]; /* off | see-through | floating | all */
  char window_glass_apps[128]; /* see-through by their own config: classes, class* prefixes, Hyprland tags */
  bool window_hypr_blur; /* keep Hyprland's blur behind those windows too */
  /* selection light: a glow around windows, under them (light.c) */
  char light_mode[16];      /* off | focused | all */
  char light_color[16];     /* accent | foreground | red | green | yellow | blue | magenta | cyan | rainbow */
  char light_animation[16]; /* none | breathe | flow | shimmer */
  float light_strength, light_unfocused, light_outside, light_inside, light_edge, light_softness;
  float light_speed, light_sound, light_fade;
  bool light_urgent;
  float window_refract, window_bevel, window_curve, window_frost, window_dispersion;
  float window_saturation, window_brightness, window_specular, window_light, window_tint;
  char battery_mode[16]; /* play | throttle | pause */
  float battery_fps, battery_scale, battery_pause_below;
  struct lw_toml *doc; /* for [themes], [monitors], [params.*] lookups */
};

struct lw_engine {
  struct lw_config cfg;
  struct lw_sys sys;
  struct lw_output *outputs;
  bool running, enabled, manual_pause;
  bool still; /* "off", but drawing Omarchy's background itself so the glass can bend it (glass_still) */
  bool gl_ready;
  char current[64]; /* wallpaper used where no monitor override applies */
  char last_error[1024];
  int epfd, timerfd;
  double started;
};
extern struct lw_engine E;

typedef void (*lw_fd_cb)(void *data, uint32_t events);
void lw_loop_add(int fd, lw_fd_cb cb, void *data);
void lw_loop_del(int fd);

void lw_config_load(void);
void lw_state_load(void);
void lw_state_save(void);
const char *lw_state_get(const char *key);
void lw_state_set(const char *key, const char *value); /* value NULL removes */

const char *lw_engine_wallpaper_for(const struct lw_output *o);
bool lw_engine_set(const char *id, const char *monitor, char *err, size_t errlen);
bool lw_engine_cycle(int dir, char *err, size_t errlen); /* +1, -1, 0 = random */
void lw_engine_apply(void); /* (re)load wallpapers on every output that needs one */
void lw_engine_policy(void); /* recompute pause and fps caps */
void lw_engine_invalidate(struct lw_output *o); /* NULL = all */
void lw_engine_theme_changed(const struct lw_theme *t, bool animate);
void lw_engine_set_enabled(bool on);
void lw_engine_arm_timer(void);
void lw_engine_tick(void); /* per-frame shared state: theme fade, audio */
void lw_engine_apply_params(struct lw_wallpaper *wp);
bool lw_engine_param(const char *name, const char *value);
void lw_engine_param_reset(void);
bool lw_engine_preset(const char *name);
bool lw_config_set(const char *key, const char *value, char *err, size_t errlen); /* value NULL = back to default */
void lw_engine_config_changed(void);
void lw_config_list(struct lw_buf *b, const char *only);
void lw_engine_describe(struct lw_buf *b); /* everything a settings UI needs, as JSON */
bool lw_engine_reload(char *err, size_t errlen);
int lw_debounce_fd(void);
void lw_debounce_arm(int fd, double seconds);
int lw_engine_status_json(char *buf, size_t len);
void lw_engine_watch_wallpaper_dirs(void);

/* ---------- wayland backend ---------- */
bool lw_wl_init(void);
void lw_wl_fini(void);
void lw_wl_render(struct lw_output *o);
bool lw_wl_output_load(struct lw_output *o, const char *id, bool crossfade, char *err, size_t errlen);
void lw_wl_schedule(struct lw_output *o); /* decide when this output draws next */
void lw_wl_map_all(bool on);
void lw_wl_update_input(struct lw_output *o);
void lw_wl_resize(struct lw_output *o);
int lw_wl_level(void);                 /* the layer Kinetic's surfaces use: 0 background, 1 bottom */
bool lw_wl_raise(struct lw_output *o); /* move this output's layer to the top of its level */
void lw_wl_remap(struct lw_output *o); /* a new layer surface for this output (last resort: reloads the wallpaper) */
const char *lw_wl_display_name(void);

/* ---------- workspace motion in step with Hyprland (motion.c) ---------- */
bool lw_motion_step(struct lw_output *o, double now, float dt); /* advance; true while moving */
void lw_motion_switch(struct lw_output *o, int to);             /* a workspace switch begins */
bool lw_motion_moving(const struct lw_output *o);
/* Where workspace `ws` is on output `o` right now, as Hyprland shows it: offset in
 * screens (x right, y down) and opacity. false while it is not on screen. */
bool lw_motion_workspace_place(const struct lw_output *o, int ws, float *dx, float *dy, float *alpha);
bool lw_wl_drawable(const struct lw_output *o);                 /* not paused, or paused but following a slide */               /* a switch or pass-through is under way */
void lw_motion_travel(const char *args);                        /* Hyprland's `kinetic-travel` event */
int lw_motion_command(int argc, char **argv, char *out, size_t len);
void lw_motion_load(void);
float lw_motion_parallax(void);
float lw_motion_origin(int id); /* the first workspace of id's block of ten: where the pan starts */
#define LW_MOTION_ARGS 16 /* words in a motion line, "motion" included */
extern float lw_thumb_parallax[2]; /* thumbnail --parallax */

/* ---------- ipc ---------- */
bool lw_ipc_init(void);
void lw_ipc_fini(void);
int lw_ipc_client(const char *cmd, bool quiet);
void lw_ipc_publish(void); /* lw_signal_path(): "on" while showing, for the bar */

/* ---------- glass under the Kinetic bar (lens.c) ---------- */
int lw_lens_command(int argc, char **argv, char *out, size_t len);
void lw_lens_draw(struct lw_output *o); /* after the frame, into the bound default framebuffer */
bool lw_lens_active(const char *output);
void lw_lens_draw_offscreen(GLuint fbo, int w, int h); /* the "offscreen" slot */

/* ---------- Kinetic's Hyprland plugin: window glass and the selection light (plugin.c) ---------- */
void lw_plugin_sync(bool force); /* send the Glass and Light options (and the theme's colors) if they changed */
void lw_plugin_off(void);        /* the engine is going: glass and light off, Hyprland's blur back */
void lw_plugin_sound(void);      /* the sound level, for a light that reacts to it */
int lw_plugin_status(char *out, size_t len); /* what the plugin says, for describe/status */

/* ---------- offscreen tools ---------- */
bool lw_offscreen_init(void);
int lw_cmd_thumbnail(const char *id, const char *out, int w, int h, double t);
int lw_cmd_bench(const char *id, int w, int h, double seconds, float scale);
int lw_cmd_validate(const char *id);

/* ---------- modules ---------- */
struct lw_module {
  const char *name;
  bool (*start)(void);
  void (*stop)(void);
  bool running;
};
extern struct lw_module lw_mod_theme, lw_mod_hyprland, lw_mod_power, lw_mod_shell, lw_mod_audio;
void lw_theme_load(struct lw_theme *t);     /* read Omarchy's current colors.toml */
void lw_audio_set_active(bool on);          /* start/stop PipeWire capture on demand */
void lw_audio_update(struct lw_sys *sys);   /* refresh spectrum texture; call once per frame */
void lw_shell_suspend(bool on);             /* hide/show the Omarchy shell's own background layer */
void lw_shell_check(double delay);          /* look again soon at what covers Kinetic's layer (layer events, first frames) */
char *lw_hypr_request(const char *cmd);     /* one request on Hyprland's socket; malloc'd reply or NULL */
bool lw_hypr_connected(void);               /* following Hyprland's event socket */
void lw_power_refresh(void);
void lw_background_refresh(struct lw_sys *sys); /* (re)load the current Omarchy still into sys->bg_tex */
bool lw_background_is_video(void);              /* Omarchy's background is a video (or GIF): owe plays it */
void lw_engine_still_update(void);              /* re-decide still mode: the background or glass_still changed */
bool lw_engine_on(void);                        /* what the user sees: a live wallpaper, not off or still */
