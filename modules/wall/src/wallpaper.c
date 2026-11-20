/* Wallpaper packages: a directory holding wallpaper.toml plus GLSL fragment
 * shaders written against a Shadertoy-compatible interface, extended with
 * Omarchy uniforms (theme colors, workspace, battery, audio, pointer). */
#include "lw.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { CH_NONE, CH_TEXTURE, CH_PASS, CH_AUDIO, CH_BACKGROUND };

/* Buffer formats. Fewer channels and bits mean less memory bandwidth, which is
 * what most simulations are limited by; 32-bit floats cannot be filtered. */
static const struct {
  const char *name;
  GLenum ifmt;
  int kind; /* 0 = 8-bit, 1 = half float, 2 = float */
} formats[] = {
    {"rgba8", GL_RGBA8, 0},     {"rg8", GL_RG8, 0},         {"r8", GL_R8, 0},
    {"rgba16f", GL_RGBA16F, 1}, {"rg16f", GL_RG16F, 1},     {"r16f", GL_R16F, 1},
    {"rgba32f", GL_RGBA32F, 2}, {"rg32f", GL_RG32F, 2},     {"r32f", GL_R32F, 2},
};
#define NFORMATS (int)(sizeof formats / sizeof formats[0])

struct channel {
  int kind, pass;
  GLuint tex;
  int w, h;
};

/* What a pass draws: a fullscreen triangle (the Shadertoy model) or geometry
 * whose vertices a mainVertex() function places: points, lines or triangles. */
enum { DRAW_FULLSCREEN, DRAW_POINTS, DRAW_LINES, DRAW_LINE_STRIP, DRAW_TRIANGLES, DRAW_TRIANGLE_STRIP };
static const char *draw_names[] = {"fullscreen", "points", "lines", "line_strip", "triangles", "triangle_strip"};
static const GLenum draw_modes[] = {GL_TRIANGLES, GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP};

enum { BLEND_NONE, BLEND_ADD, BLEND_ALPHA, BLEND_MULTIPLY, BLEND_MAX, BLEND_MIN };
static const char *blend_names[] = {"none", "add", "alpha", "multiply", "max", "min"};

/* A setting that is a constant or tracks a wallpaper parameter live. */
struct live {
  float v;
  int param;
};

struct pass {
  char name[32];
  GLuint prog;
  GLint u_res, u_time, u_dt, u_frame, u_mouse, u_date, u_chanres, u_pass, u_src, u_srcsize, u_count;
  GLint u_bg, u_fg, u_accent, u_pal, u_dark, u_ws, u_focus, u_batt, u_audio, u_day, u_ptr;
  GLint u_sound, u_reverse, u_epoch, u_parallax, u_shift;
  GLint u_param[LW_MAX_PARAMS];
  struct channel ch[LW_MAX_CHANNELS];
  float scale;
  int fixed_w, fixed_h; /* size = [w, h]: e.g. one texel per particle */
  int format; /* index into formats[] */
  bool nearest, repeat, mipmap, depth;
  bool persist; /* saved to disk, restored on the next load: gardens that keep growing */
  int kind;     /* the precision actually allocated (0 8-bit, 1 half, 2 float) */
  bool fresh;   /* targets were just (re)created */
  int draw, blend, src_pass;
  bool accumulate; /* one persistent target that is drawn onto, instead of ping-pong */
  struct live iterations, every, fade, count;
  GLuint fbo[2], tex[2], depth_rb;
  int cur, w, h, levels;
  double last_time;
  /* uniform cache: theme and parameters only change occasionally */
  float theme_cache[58], param_cache[LW_MAX_PARAMS][3];
  bool cache_valid;
};

struct lw_wallpaper {
  struct lw_wallpaper_info info;
  struct pass pass[LW_MAX_PASSES];
  int npass; /* the last pass draws to the screen */
  int frame;
  GLuint fade_prog;
  char *common; /* common.glsl, shared by every stage */
  float epoch;
  double clock; /* the wallpaper's own time at the last render */
  char state[LW_PATH]; /* where persistent buffers live; empty = nothing persists */
  double last_save;
  float away; /* seconds between the last save and this restore (for catching up) */
  /* The fill monitor: every so often a 32x32 coverage map of one buffer is
   * rendered and read back through a pixel buffer and a fence, so the CPU never
   * waits for the GPU. */
  struct {
    int pass, channel, valid, param, measured;
    float above, full, low, empty, coverage, rev, rev_s;
    float settle, settle_min, peak, rev_from, ramp, grace;
    double start; /* clock at the last (re)start: nothing happens for `grace` seconds */ /* "settled": stopped spreading for `settle` seconds */
    bool reversing, reset_pending;
    double rev_since, peak_time;
    GLuint prog, fbo, tex, pbo;
    GLsync fence;
  } fill;
};

static const char *preamble =
    "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
    "uniform vec3 iResolution;\nuniform float iTime;\nuniform float iTimeDelta;\nuniform int iFrame;\n"
    "uniform vec4 iMouse;\nuniform vec4 iDate;\n"
    "uniform sampler2D iChannel0;\nuniform sampler2D iChannel1;\nuniform sampler2D iChannel2;\nuniform sampler2D iChannel3;\n"
    "uniform sampler2D iChannel4;\nuniform sampler2D iChannel5;\nuniform sampler2D iChannel6;\nuniform sampler2D iChannel7;\n"
    "uniform vec3 iChannelResolution[8];\n"
    "uniform int iPass;\n"       /* iteration index within a pass that runs several times a frame */
    "uniform int iCount;\n"      /* vertices drawn by a geometry pass */
    "uniform vec3 uBackground;\nuniform vec3 uForeground;\nuniform vec3 uAccent;\nuniform vec3 uPalette[16];\n"
    "uniform float uDark;\n"
    "uniform vec4 uWorkspace;\n" /* smoothed index, seconds since switch, index, windows */
    "uniform float uFocus;\n"    /* seconds since the active window changed */
    "uniform vec4 uBattery;\n"   /* level, charging, on battery, present */
    "uniform vec4 uAudio;\n"     /* bass, mid, treble, level */
    "uniform float uDayTime;\n"  /* local hours, 0..24 */
    "uniform vec4 uPointer;\n"   /* smoothed xy 0..1, presence, seconds since click */
    "uniform vec4 uSound;\n"     /* loudness, seconds since the last onset, its strength, onset count; 0/1e4 when off */
    "uniform float uReverse;\n"  /* 0..1: the fill monitor asks the simulation to run back */
    "uniform float uEpoch;\n"    /* increments on every automatic restart: mix it into seeds */
    "uniform vec2 uParallax;\n"  /* workspace parallax: how far the view has panned, in screens */
    "uniform vec2 lw_Shift;\n"   /* the same in pixels, applied to the image pass of wallpapers that pan */
    /* Layered parallax: how far a layer at `depth` (0 still, 1 moves with the view)
     * has panned, in screen heights: add it to a position like vec2(uv.x * aspect, uv.y). */
    "vec2 lw_pan(float depth){ return uParallax * vec2(iResolution.x / iResolution.y, 1.0) * depth; }\n"
    /* A small standard library, so most wallpapers need no boilerplate.
     * lw_hash: the short fract/dot hash popularized by The Art of Code (Martijn Steinrucken). */
    "float lw_hash(vec2 p){ p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
    "vec2 lw_hash2(vec2 p){ float n = lw_hash(p); return vec2(n, lw_hash(p + n + 17.17)); }\n"
    /* integer hash: exact and uncorrelated for particle ids and seeds. Chris Wellons'
     * lowbias32, from hash-prospector (public domain). See THIRD-PARTY.md */
    "uint lw_uhash(uint x){ x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }\n"
    "float lw_rand(int id, int salt){ return float(lw_uhash(uint(id) * 1664525u + lw_uhash(uint(salt)))) / 4294967295.0; }\n"
    "float lw_noise(vec2 p);\n"
    "float lw_fbm(vec2 p){ float a = 0.5, s = 0.0; for (int i = 0; i < 5; i++) { s += a * lw_noise(p);\n"
    "  p = p * 2.02 + vec2(17.3, 9.1); a *= 0.5; } return s; }\n"
    "vec3 lw_dither(vec3 c, vec2 fc){ return c + (lw_hash(fc + fract(iTime)) - 0.5) / 255.0; }\n"
    "float lw_luma(vec3 c){ return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
    /* what one frame adds to a `fade` trail, scaled so the trail holds the same light at
       any frame rate (the fade itself is per second): 1 at 60 fps, more at fewer */
    "float lw_trail_gain(float fade){ return fade <= 0.0 || fade >= 1.0 ? 1.0 :"
    " (1.0 - pow(fade, min(iTimeDelta, 0.25) * 60.0)) / (1.0 - fade); }\n"
    /* glow on dark themes, ink on light ones */
    "vec3 lw_blend(vec3 base, vec3 c, float a){ return mix(mix(base, c, clamp(a, 0.0, 1.0)), base + c * a, uDark); }\n"
    /* aspect-correct 'cover' coordinates for a texture of the given size */
    "vec2 lw_cover(vec2 uv, vec2 tex, vec2 res){ float ta = tex.x / tex.y, ra = res.x / res.y;\n"
    "  vec2 s = ta > ra ? vec2(ra / ta, 1.0) : vec2(1.0, ta / ra); return (uv - 0.5) * s + 0.5; }\n"
    /* smooth palette gradient through theme colors 1..6 */
    "vec3 lw_palette(float t){ t = fract(t) * 6.0; int i = int(t); float f = smoothstep(0.0, 1.0, fract(t));\n"
    "  return mix(uPalette[1 + i], uPalette[1 + (i + 1) % 6], f); }\n"
    /* perspective projection into the uv-homogeneous position mainVertex() writes */
    "vec4 lw_perspective(vec3 p, float focal, float aspect){ float w = max(p.z, 1e-4);\n"
    "  return vec4((p.x * focal / aspect + w) * 0.5, (p.y * focal + w) * 0.5, clamp(p.z / 100.0, 0.0, 1.0) * w, w); }\n";

static const char *search_dirs(int i, char buf[LW_PATH]) {
  const char *env = getenv("KINETIC_WALL_PATH");
  switch (i) {
  case 0:
    if (env && *env) return env;
    return "";
  case 1:
    lw_dir_config(buf, "wallpapers");
    return buf;
  case 2: {
    const char *d = getenv("XDG_DATA_HOME");
    if (d && *d)
      snprintf(buf, LW_PATH, "%s/kinetic/wallpapers", d);
    else
      snprintf(buf, LW_PATH, "%s/.local/share/kinetic/wallpapers", lw_home());
    return buf;
  }
  case 3: return "/usr/local/share/kinetic/wallpapers";
  case 4: return "/usr/share/kinetic/wallpapers";
  }
  return NULL;
}

static bool is_wallpaper_dir(const char *dir) {
  char p[LW_PATH + 32];
  struct stat st;
  snprintf(p, sizeof p, "%s/wallpaper.toml", dir);
  return stat(p, &st) == 0;
}

bool lw_wallpaper_find(const char *id, char dir[LW_PATH]) {
  if (strchr(id, '/')) {
    if (!realpath(id, dir)) snprintf(dir, LW_PATH, "%s", id);
    return is_wallpaper_dir(dir);
  }
  char buf[LW_PATH];
  const char *base;
  for (int i = 0; (base = search_dirs(i, buf)); i++) {
    if (!*base) continue;
    if (snprintf(dir, LW_PATH, "%s/%s", base, id) >= LW_PATH) continue; /* too long to be one */
    if (is_wallpaper_dir(dir)) return true;
  }
  return false;
}

void lw_wallpaper_each(lw_wallpaper_cb cb, void *data) {
  char seen[128][64];
  int nseen = 0;
  char buf[LW_PATH];
  const char *base;
  for (int i = 0; (base = search_dirs(i, buf)); i++) {
    if (!*base) continue;
    struct dirent **list;
    int n = scandir(base, &list, NULL, alphasort);
    for (int j = 0; j < n; j++) {
      const char *name = list[j]->d_name;
      char dir[LW_PATH * 2];
      snprintf(dir, sizeof dir, "%s/%s", base, name);
      bool dup = false;
      for (int k = 0; k < nseen; k++) dup |= !strcmp(seen[k], name);
      /* a name that would not fit in seen[] could not be told from a longer one: skipped */
      if (name[0] != '.' && !dup && strlen(name) < sizeof seen[0] && strlen(dir) < LW_PATH && is_wallpaper_dir(dir) &&
          nseen < 128) {
        memcpy(seen[nseen++], name, strlen(name) + 1); /* fits: checked above */
        cb(name, dir, data);
      }
      free(list[j]);
    }
    if (n >= 0) free(list);
  }
}

static void tval_str(const struct lw_tval *v, char *out, size_t len) {
  if (v->type == LW_T_STR)
    snprintf(out, len, "%s", v->s);
  else
    snprintf(out, len, "%g", v->n);
}

static struct lw_param *add_param(struct lw_wallpaper_info *info, const char *name) {
  if (info->nparams >= LW_MAX_PARAMS) return NULL;
  struct lw_param *p = &info->params[info->nparams++];
  memset(p, 0, sizeof *p);
  snprintf(p->name, sizeof p->name, "%s", name);
  snprintf(p->label, sizeof p->label, "%s", name);
  for (char *c = p->label; *c; c++)
    if (*c == '_') *c = ' ';
  if (p->label[0] >= 'a' && p->label[0] <= 'z') p->label[0] -= 32;
  p->max = 1;
  return p;
}

/* Parameters come in a short form, `[params] speed = [1.0, 0.0, 4.0]`, and a
 * long form, `[param.rule] type = "choice" ...`, for everything richer. */
static void read_params(const struct lw_toml *t, struct lw_wallpaper_info *info) {
  const char *key, *table;
  const struct lw_tval *v;
  struct lw_param *p;
  for (int i = 0; lw_toml_each(t, "params", i, &key, &v) && (p = add_param(info, key)); i++) {
    if (v->type == LW_T_BOOL) {
      p->type = LW_P_BOOL;
      p->def[0] = (float)v->n;
    } else if (v->type == LW_T_ARR && v->len >= 1) {
      p->def[0] = (float)v->arr[0].n;
      if (v->len >= 3) p->min = (float)v->arr[1].n, p->max = (float)v->arr[2].n;
    } else {
      p->def[0] = (float)v->n;
      p->max = p->def[0] > 1 ? p->def[0] * 4 : 1;
    }
  }
  for (int i = 0; (table = lw_toml_table(t, i)); i++) {
    if (strncmp(table, "param.", 6) || !(p = add_param(info, table + 6))) continue;
    const char *type = lw_toml_str(t, table, "type", "float");
    p->type = !strcmp(type, "int") ? LW_P_INT : !strcmp(type, "bool") ? LW_P_BOOL : !strcmp(type, "choice") ? LW_P_CHOICE
            : !strcmp(type, "bits") ? LW_P_BITS : !strcmp(type, "color") ? LW_P_COLOR : LW_P_FLOAT;
    snprintf(p->label, sizeof p->label, "%s", lw_toml_str(t, table, "label", p->label));
    snprintf(p->desc, sizeof p->desc, "%s", lw_toml_str(t, table, "description", ""));
    p->min = (float)lw_toml_num(t, table, "min", 0);
    p->max = (float)lw_toml_num(t, table, "max", 1);
    p->step = (float)lw_toml_num(t, table, "step", p->type == LW_P_INT ? 1 : 0);
    p->def[0] = (float)lw_toml_num(t, table, "default", 0);
    p->restart = lw_toml_bool(t, table, "restart", false);
    const struct lw_tval *opts = lw_toml_get(t, table, "options");
    if (p->type == LW_P_CHOICE && opts && opts->type == LW_T_ARR) {
      const char *def = lw_toml_str(t, table, "default", NULL);
      for (int o = 0; o < opts->len && o < LW_MAX_OPTIONS; o++) {
        tval_str(&opts->arr[o], p->options[o], sizeof p->options[o]);
        if (def && !strcmp(def, p->options[o])) p->def[0] = (float)o;
        p->nopt++;
      }
      p->min = 0, p->max = (float)(p->nopt > 0 ? p->nopt - 1 : 0);
    } else if (p->type == LW_P_BITS) {
      p->bits = (int)lw_clampf((float)lw_toml_num(t, table, "bits", 9), 1, 16);
      p->min = 0, p->max = (float)((1 << p->bits) - 1);
    } else if (p->type == LW_P_BOOL) {
      p->min = 0, p->max = 1;
    } else if (p->type == LW_P_COLOR) {
      if (!lw_parse_hex(lw_toml_str(t, table, "default", "#ffffff"), p->def)) p->def[0] = p->def[1] = p->def[2] = 1;
    }
  }
  for (int i = 0; i < info->nparams; i++) memcpy(info->params[i].value, info->params[i].def, sizeof info->params[i].def);

  /* [preset.<Name>] tables set several parameters at once */
  for (int i = 0; (table = lw_toml_table(t, i)); i++) {
    if (strncmp(table, "preset.", 7) || info->npresets >= LW_MAX_PRESETS) continue;
    struct lw_preset *pr = &info->presets[info->npresets++];
    snprintf(pr->name, sizeof pr->name, "%s", table + 7);
    for (int k = 0; pr->n < 10 && lw_toml_each(t, table, k, &key, &v); k++) {
      snprintf(pr->key[pr->n], 32, "%s", key);
      tval_str(v, pr->val[pr->n++], 32);
    }
  }
}

/* Engine features that every wallpaper gets, as ordinary parameters so the
 * panel, presets and saved state handle them like any other. */
static void inject_params(const struct lw_toml *t, struct lw_wallpaper_info *info) {
  struct lw_param *p;
  if (info->fill && (p = add_param(info, "lw_on_full"))) {
    p->type = LW_P_CHOICE;
    snprintf(p->label, sizeof p->label, "When the screen fills");
    snprintf(p->desc, sizeof p->desc, "%s", info->fill_reverse ? "Keep going, start over with a new seed, or run it back until the screen clears"
                                                                : "Keep going, or start over with a new seed");
    const char *opts[] = {"continue", "restart", "reverse"};
    p->nopt = info->fill_reverse ? 3 : 2;
    for (int i = 0; i < p->nopt; i++) snprintf(p->options[i], sizeof p->options[i], "%s", opts[i]);
    p->max = (float)(p->nopt - 1);
    const char *def = lw_toml_str(t, "fill", "default", info->fill_reverse ? "reverse" : "restart");
    for (int i = 0; i < p->nopt; i++)
      if (!strcmp(def, opts[i])) p->def[0] = (float)i;
    p->value[0] = p->def[0];
  }
  if (!info->audio && (p = add_param(info, "lw_sound"))) { /* audio wallpapers always listen */
    p->type = LW_P_CHOICE;
    snprintf(p->label, sizeof p->label, "React to sound");
    snprintf(p->desc, sizeof p->desc, "global follows the Sound setting in the Engine tab");
    const char *opts[] = {"global", "off", "on"};
    p->nopt = 3;
    for (int i = 0; i < 3; i++) snprintf(p->options[i], sizeof p->options[i], "%s", opts[i]);
    p->max = 2;
  }
}

static bool uses_channel(const struct lw_toml *t, const char *what) {
  char table[32];
  int nb = lw_toml_count(t, "buffer");
  for (int i = -1; i < nb; i++) {
    if (i < 0)
      snprintf(table, sizeof table, "image");
    else
      snprintf(table, sizeof table, "buffer.%d", i);
    const struct lw_tval *ch = lw_toml_get(t, table, "channels");
    for (int c = 0; ch && ch->type == LW_T_ARR && c < ch->len; c++)
      if (ch->arr[c].type == LW_T_STR && !strcmp(ch->arr[c].s, what)) return true;
  }
  return false;
}

static struct lw_toml *read_manifest(const char *dir, struct lw_wallpaper_info *info, char *err, size_t errlen) {
  char path[LW_PATH + 32];
  snprintf(path, sizeof path, "%s/wallpaper.toml", dir);
  struct lw_toml *t = lw_toml_load(path);
  if (!t) {
    snprintf(err, errlen, "cannot read %s", path);
    return NULL;
  }
  memset(info, 0, sizeof *info);
  const char *slash = strrchr(dir, '/');
  snprintf(info->id, sizeof info->id, "%s", slash ? slash + 1 : dir);
  snprintf(info->dir, sizeof info->dir, "%s", dir);
  snprintf(info->title, sizeof info->title, "%s", lw_toml_str(t, "", "name", info->id));
  snprintf(info->author, sizeof info->author, "%s", lw_toml_str(t, "", "author", ""));
  snprintf(info->description, sizeof info->description, "%s", lw_toml_str(t, "", "description", ""));
  info->fps = info->def_fps = (float)lw_toml_num(t, "", "fps", 60);
  info->scale = info->def_scale = lw_clampf((float)lw_toml_num(t, "", "scale", 1), 0.1f, 2.0f);
  info->preview_time = (float)lw_toml_num(t, "", "preview_time", 12);
  info->preview_warmup = lw_clampf((float)lw_toml_num(t, "", "preview_warmup", 6), 0, 600);
  info->pointer = lw_toml_bool(t, "", "pointer", false);
  info->adaptive = lw_toml_bool(t, "", "adaptive", true);
  info->parallax = lw_clampf((float)lw_toml_num(t, "", "parallax", 0), 0, 4);
  info->audio = lw_toml_bool(t, "", "audio", false) || uses_channel(t, "@audio");
  info->background = uses_channel(t, "@background");
  info->nbuffers = lw_toml_count(t, "buffer");
  info->fill = lw_toml_get(t, "fill", "buffer") != NULL;
  info->fill_reverse = info->fill && lw_toml_bool(t, "fill", "reverse", false);
  read_params(t, info);
  inject_params(t, info);
  int want = 2; /* room for the engine's own (When the screen fills, React to sound) */
  for (int i = 0; lw_toml_each(t, "params", i, &(const char *){0}, &(const struct lw_tval *){0}); i++) want++;
  for (int i = 0; lw_toml_table(t, i); i++) want += !strncmp(lw_toml_table(t, i), "param.", 6);
  if (want > LW_MAX_PARAMS) {
    snprintf(err, errlen, "%d parameters: at most %d (with the engine's own %d)", want - 2, LW_MAX_PARAMS - 2, 2);
    lw_toml_free(t);
    return NULL;
  }
  return t;
}

bool lw_wallpaper_read_info(const char *dir, struct lw_wallpaper_info *info, char *err, size_t errlen) {
  struct lw_toml *t = read_manifest(dir, info, err, errlen);
  lw_toml_free(t);
  return t != NULL;
}

/* Value noise is the hot spot of most wallpapers. On GLES 3.1+ one
 * textureGather of a random texture replaces four ALU hashes per octave;
 * interpolation stays in full-precision ALU, so there is no filtering banding. */
static const char *noise_gather =
    "uniform sampler2D lw_Noise;\n"
    "float lw_noise(vec2 p){ vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "  vec4 g = textureGather(lw_Noise, (i + 1.0) / 256.0, 0);\n"
    "  return mix(mix(g.w, g.z, f.x), mix(g.x, g.y, f.x), f.y); }\n";
static const char *noise_hash =
    "float lw_noise(vec2 p){ vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "  return mix(mix(lw_hash(i), lw_hash(i + vec2(1.0, 0.0)), f.x),\n"
    "             mix(lw_hash(i + vec2(0.0, 1.0)), lw_hash(i + vec2(1.0, 1.0)), f.x), f.y); }\n";
#define NOISE_UNIT (LW_MAX_CHANNELS + 1)

static bool gles31(void) {
  static int cached = -1;
  if (cached < 0) {
    int major = 0, minor = 0;
    const char *v = (const char *)glGetString(GL_VERSION);
    cached = v && sscanf(v, "OpenGL ES %d.%d", &major, &minor) == 2 && (major > 3 || (major == 3 && minor >= 1));
  }
  return cached;
}

static GLuint noise_texture(void) {
  static GLuint tex;
  if (!tex) {
    uint8_t *px = malloc(256 * 256 * 4);
    uint32_t x = 0x9e3779b9u;
    for (int i = 0; i < 256 * 256 * 4; i++) {
      x ^= x << 13, x ^= x >> 17, x ^= x << 5;
      px[i] = (uint8_t)(x >> 24);
    }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 256, 256);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    free(px);
  }
  return tex;
}

/* version line + shared declarations + the noise implementation for this GPU */
static bool use_gather(void) { return gles31() && !getenv("KINETIC_HASH_NOISE"); }

static size_t write_preamble(char *src, size_t cap) {
  bool g = use_gather();
  return (size_t)snprintf(src, cap, "#version %s es\n%s%s", g ? "310" : "300", preamble, g ? noise_gather : noise_hash);
}

static char *read_shader(struct lw_wallpaper *wp, const char *file, char *err, size_t errlen) {
  char path[LW_PATH + 128];
  snprintf(path, sizeof path, "%s/%s", wp->info.dir, file);
  char *src = lw_read_file(path, NULL);
  if (!src) snprintf(err, errlen, "cannot read shader %s", path);
  return src;
}

/* Parameters become uniforms, then common.glsl, then the stage's own code. */
static size_t shared_decls(struct lw_wallpaper *wp, char *src, size_t cap) {
  size_t n = 0;
  for (int i = 0; i < wp->info.nparams; i++)
    n += (size_t)snprintf(src + n, cap - n, "uniform %s %s;\n", wp->info.params[i].type == LW_P_COLOR ? "vec3" : "float",
                          wp->info.params[i].name);
  if (wp->common) n += (size_t)snprintf(src + n, cap - n, "#line 1 1\n%s\n", wp->common);
  return n;
}

/* Geometry passes: the vertex stage fetches this vertex's texel from the
 * source buffer (particle state, a mesh baked into a buffer...) and hands it
 * to mainVertex(), which places it. `pos` is in uv space (0..1) with depth in
 * z and a homogeneous w, so perspective is just a division by w. */
static const char *geom_vs_head =
    "uniform sampler2D lw_Source;\nuniform ivec2 lw_SourceSize;\n"
    "out vec4 lw_Color;\nout vec4 lw_State;\nout vec4 lw_Data;\n"
    "vec4 lw_data = vec4(0.0);\n"; /* anything extra for the fragment stage */
static const char *geom_vs_main =
    "void main(){ int id = gl_VertexID; vec4 state = vec4(0.0);\n"
    "  if (lw_SourceSize.x > 0) state = texelFetch(lw_Source, ivec2(id % lw_SourceSize.x, id / lw_SourceSize.x), 0);\n"
    "  vec4 pos = vec4(state.xy, 0.0, 1.0); float size = 1.0; vec4 color = vec4(1.0);\n"
    "  mainVertex(id, state, pos, size, color);\n"
    "  lw_State = state; lw_Color = color; lw_Data = lw_data;\n"
    "  gl_Position = size > 0.0 ? vec4(pos.xy * 2.0 - pos.w, pos.z * 2.0 - pos.w, pos.w) : vec4(2.0, 2.0, 2.0, 1.0);\n"
    "  gl_PointSize = max(size, 1.0); }\n";
static const char *geom_vs_default =
    "void mainVertex(int id, vec4 state, inout vec4 pos, inout float size, inout vec4 color) {}\n";
static const char *points_fs_default =
    "void mainImage(out vec4 c, in vec2 fc){ vec2 q = gl_PointCoord * 2.0 - 1.0; c = lw_Color * max(0.0, 1.0 - dot(q, q)); }\n";
static const char *geom_fs_default = "void mainImage(out vec4 c, in vec2 fc){ c = lw_Color; }\n";

static GLuint build_program(struct lw_wallpaper *wp, struct pass *p, const char *file, const char *vfile, bool final,
                            char *err, size_t errlen) {
  bool geom = p->draw != DRAW_FULLSCREEN;
  char *user = file ? read_shader(wp, file, err, errlen) : strdup(p->draw == DRAW_POINTS ? points_fs_default : geom_fs_default);
  if (!user) return 0;
  char *vuser = NULL;
  if (geom && !(vuser = vfile ? read_shader(wp, vfile, err, errlen) : strdup(geom_vs_default))) {
    free(user);
    return 0;
  }
  size_t cap = strlen(preamble) + strlen(user) + (vuser ? strlen(vuser) : 0) + (wp->common ? strlen(wp->common) : 0) + 8192;
  char *src = malloc(cap), *vsrc = NULL;
  size_t n = write_preamble(src, cap);
  n += (size_t)snprintf(src + n, cap - n, "%sout vec4 lw_FragColor;\n",
                        geom ? "in vec4 lw_Color;\nin vec4 lw_State;\nin vec4 lw_Data;\n" : "");
  n += shared_decls(wp, src + n, cap - n);
  snprintf(src + n, cap - n,
           "#line 1 0\n%s\nvoid main(){ vec4 c = vec4(0.0, 0.0, 0.0, 1.0); mainImage(c, gl_FragCoord.xy%s); %s lw_FragColor = c; }\n",
           user, final && !geom ? " + lw_Shift" : "", final ? "c.a = 1.0;" : "");
  if (geom) {
    vsrc = malloc(cap);
    n = write_preamble(vsrc, cap);
    n += (size_t)snprintf(vsrc + n, cap - n, "%s", geom_vs_head);
    n += shared_decls(wp, vsrc + n, cap - n);
    snprintf(vsrc + n, cap - n, "#line 1 0\n%s\n%s", vuser, geom_vs_main);
  }
  char log[900] = "";
  char fs_vs[256]; /* the fullscreen vertex shader, at the same language version */
  snprintf(fs_vs, sizeof fs_vs, "#version %s es\n%s", use_gather() ? "310" : "300", strchr(lw_vs_fullscreen, '\n') + 1);
  GLuint prog = lw_gl_program(vsrc ? vsrc : fs_vs, src, log, sizeof log);
  if (!prog) snprintf(err, errlen, "%s (source 1 = common.glsl): %s", vsrc ? (vfile ? vfile : "vertex") : file, log);
  free(src);
  free(vsrc);
  free(user);
  free(vuser);
  return prog;
}

static void locate(struct lw_wallpaper *wp, struct pass *p) {
  GLuint g = p->prog;
#define L(field, name) p->field = glGetUniformLocation(g, name)
  L(u_res, "iResolution"); L(u_time, "iTime"); L(u_dt, "iTimeDelta"); L(u_frame, "iFrame");
  L(u_mouse, "iMouse"); L(u_date, "iDate"); L(u_chanres, "iChannelResolution"); L(u_pass, "iPass"); L(u_count, "iCount");
  L(u_bg, "uBackground"); L(u_fg, "uForeground"); L(u_accent, "uAccent"); L(u_pal, "uPalette");
  L(u_dark, "uDark"); L(u_ws, "uWorkspace"); L(u_focus, "uFocus"); L(u_batt, "uBattery");
  L(u_audio, "uAudio"); L(u_day, "uDayTime"); L(u_ptr, "uPointer");
  L(u_src, "lw_Source"); L(u_srcsize, "lw_SourceSize");
  L(u_sound, "uSound"); L(u_reverse, "uReverse"); L(u_epoch, "uEpoch");
  L(u_parallax, "uParallax"); L(u_shift, "lw_Shift");
#undef L
  glUseProgram(g);
  for (int i = 0; i < LW_MAX_CHANNELS; i++) {
    char name[16];
    snprintf(name, sizeof name, "iChannel%d", i);
    GLint loc = glGetUniformLocation(g, name);
    if (loc >= 0) glUniform1i(loc, i);
  }
  if (p->u_src >= 0) glUniform1i(p->u_src, LW_MAX_CHANNELS);
  GLint noise = glGetUniformLocation(g, "lw_Noise");
  if (noise >= 0) glUniform1i(noise, NOISE_UNIT);
  for (int i = 0; i < wp->info.nparams; i++) p->u_param[i] = glGetUniformLocation(g, wp->info.params[i].name);
  p->cache_valid = false;
}

static int param_index(struct lw_wallpaper *wp, const char *name) {
  for (int i = 0; i < wp->info.nparams; i++)
    if (!strcmp(wp->info.params[i].name, name)) return i;
  return -1;
}

static struct live read_live(struct lw_wallpaper *wp, const struct lw_toml *t, const char *table, const char *key, float def) {
  const struct lw_tval *v = lw_toml_get(t, table, key);
  struct live l = {def, -1};
  if (v && v->type == LW_T_STR) l.param = param_index(wp, v->s);
  else if (v && (v->type == LW_T_NUM || v->type == LW_T_BOOL)) l.v = (float)v->n;
  return l;
}

static float live(struct lw_wallpaper *wp, struct live l) { return l.param >= 0 ? wp->info.params[l.param].value[0] : l.v; }

static int name_index(const char *s, const char **names, int n, int def) {
  for (int i = 0; s && i < n; i++)
    if (!strcmp(s, names[i])) return i;
  return def;
}

static bool setup_pass(struct lw_wallpaper *wp, const struct lw_toml *t, int idx, const char *table, bool final,
                       char *err, size_t errlen) {
  struct pass *p = &wp->pass[idx];
  snprintf(p->name, sizeof p->name, "%s", final ? "image" : lw_toml_str(t, table, "name", table));
  p->scale = lw_clampf((float)lw_toml_num(t, table, "scale", 1), 0.02f, 2.0f);
  const char *fmt = lw_toml_str(t, table, "format", "rgba8");
  p->format = -1;
  for (int i = 0; i < NFORMATS; i++)
    if (!strcmp(fmt, formats[i].name)) p->format = i;
  if (p->format < 0) {
    snprintf(err, errlen, "[%s] format = \"%s\": expected rgba8, rg8, r8, rgba16f, rg16f, r16f, rgba32f, rg32f or r32f", table, fmt);
    return false;
  }
  bool f32 = formats[p->format].kind == 2;
  p->nearest = f32 || !strcmp(lw_toml_str(t, table, "filter", "linear"), "nearest");
  p->repeat = !strcmp(lw_toml_str(t, table, "wrap", "clamp"), "repeat");
  p->mipmap = !f32 && lw_toml_bool(t, table, "mipmap", false);
  p->persist = !final && lw_toml_bool(t, table, "persist", false);
  const struct lw_tval *size = lw_toml_get(t, table, "size");
  if (!final && size && size->type == LW_T_ARR && size->len == 2) {
    p->fixed_w = (int)lw_clampf((float)size->arr[0].n, 1, 4096);
    p->fixed_h = (int)lw_clampf((float)size->arr[1].n, 1, 4096);
  }
  p->iterations = read_live(wp, t, table, "iterations", 1);
  p->every = read_live(wp, t, table, "every", 1);
  p->src_pass = -1;
  if (!final) {
    const char *draw = lw_toml_str(t, table, "draw", "fullscreen");
    p->draw = name_index(draw, draw_names, 6, -1);
    if (p->draw < 0) {
      snprintf(err, errlen, "[%s] draw = \"%s\": expected fullscreen, points, lines, line_strip, triangles or triangle_strip",
               table, draw);
      return false;
    }
    p->blend = name_index(lw_toml_str(t, table, "blend", p->draw == DRAW_FULLSCREEN ? "none" : "add"), blend_names, 6, 1);
    p->depth = p->draw != DRAW_FULLSCREEN && lw_toml_bool(t, table, "depth", false);
    p->count = read_live(wp, t, table, "count", 0);
    p->fade = read_live(wp, t, table, "fade", 0);
    p->accumulate = p->draw != DRAW_FULLSCREEN || p->blend != BLEND_NONE;
    if (p->accumulate && !wp->fade_prog) {
      char log[256];
      wp->fade_prog = lw_gl_program(lw_vs_fullscreen,
                                    "#version 300 es\nprecision mediump float;\nout vec4 c;\nvoid main(){ c = vec4(0.0); }\n",
                                    log, sizeof log);
    }
  }
  const char *shader = lw_toml_str(t, table, "shader", final ? "main.frag" : NULL);
  if (!shader && p->draw == DRAW_FULLSCREEN) {
    snprintf(err, errlen, "[%s] has no shader", table);
    return false;
  }
  p->prog = build_program(wp, p, shader, lw_toml_str(t, table, "vertex", NULL), final, err, errlen);
  if (!p->prog) return false;
  locate(wp, p);
  return true;
}

/* Channels are resolved after every pass is known so buffers can reference each other freely. */
static bool setup_channels(struct lw_wallpaper *wp, const struct lw_toml *t, int idx, const char *table, char *err,
                           size_t errlen) {
  struct pass *p = &wp->pass[idx];
  const char *src = p->draw != DRAW_FULLSCREEN ? lw_toml_str(t, table, "source", NULL) : NULL;
  if (src) {
    for (int j = 0; j < wp->npass - 1; j++)
      if (!strcmp(wp->pass[j].name, src)) p->src_pass = j;
    if (p->src_pass < 0 || p->src_pass == idx) {
      snprintf(err, errlen, "[%s] source '%s' is not another buffer", table, src);
      return false;
    }
  } else if (p->draw != DRAW_FULLSCREEN && p->count.v <= 0 && p->count.param < 0) {
    snprintf(err, errlen, "[%s] draws %s but has neither a source buffer nor a count", table, draw_names[p->draw]);
    return false;
  }
  const struct lw_tval *ch = lw_toml_get(t, table, "channels");
  for (int c = 0; ch && ch->type == LW_T_ARR && c < ch->len && c < LW_MAX_CHANNELS; c++) {
    const char *s = ch->arr[c].type == LW_T_STR ? ch->arr[c].s : "";
    struct channel *k = &p->ch[c];
    if (!*s) continue;
    if (!strcmp(s, "@audio")) {
      k->kind = CH_AUDIO;
      continue;
    }
    if (!strcmp(s, "@background")) {
      k->kind = CH_BACKGROUND;
      continue;
    }
    int j;
    for (j = 0; j < wp->npass - 1; j++)
      if (!strcmp(wp->pass[j].name, s)) break;
    if (j < wp->npass - 1) {
      if (j == idx && p->accumulate) {
        snprintf(err, errlen, "[%s] cannot read itself: it draws onto its own target (use fade for trails)", table);
        return false;
      }
      k->kind = CH_PASS;
      k->pass = j;
      continue;
    }
    char path[LW_PATH + 128];
    snprintf(path, sizeof path, "%s/%s", wp->info.dir, s);
    k->tex = lw_gl_texture_file(path, &k->w, &k->h, true, 0);
    if (!k->tex) {
      snprintf(err, errlen, "[%s] channel %d: '%s' is not a buffer name or a readable image", table, c, s);
      return false;
    }
    k->kind = CH_TEXTURE;
  }
  return true;
}

static bool setup_fill(struct lw_wallpaper *wp, const struct lw_toml *t, char *err, size_t errlen) {
  const char *buf = lw_toml_str(t, "fill", "buffer", "");
  wp->fill.pass = -1;
  for (int j = 0; j < wp->npass - 1; j++)
    if (!strcmp(wp->pass[j].name, buf) && !wp->pass[j].accumulate) wp->fill.pass = j;
  if (wp->fill.pass < 0) {
    snprintf(err, errlen, "[fill] buffer '%s' is not a fullscreen buffer", buf);
    return false;
  }
  wp->fill.channel = (int)lw_clampf((float)lw_toml_num(t, "fill", "channel", 0), 0, 3);
  /* optional: only texels whose `valid` channel is non-zero belong to the picture */
  wp->fill.valid = (int)lw_clampf((float)lw_toml_num(t, "fill", "valid", -1), -1, 3);
  wp->fill.above = (float)lw_toml_num(t, "fill", "above", 0.5);
  wp->fill.full = (float)lw_toml_num(t, "fill", "full", 0.6);
  wp->fill.low = (float)lw_toml_num(t, "fill", "low", 0.3); /* run back to this fraction of the peak */
  wp->fill.settle = (float)lw_toml_num(t, "fill", "settle", 0);
  wp->fill.grace = (float)lw_toml_num(t, "fill", "grace", 5);
  wp->fill.ramp = lw_clampf((float)lw_toml_num(t, "fill", "ramp", 1), 0.05f, 60); /* seconds for uReverse to build */
  wp->fill.settle_min = (float)lw_toml_num(t, "fill", "settle_min", 0.1);
  wp->fill.empty = (float)lw_toml_num(t, "fill", "empty", 0);
  wp->fill.param = param_index(wp, "lw_on_full");
  const char *fs =
      "#version 300 es\nprecision highp float;\nuniform highp sampler2D t;\nuniform vec4 mask, vmask;\nuniform float above;\n"
      "out vec4 o;\nvoid main(){ ivec2 sz = textureSize(t, 0), cell = ivec2(gl_FragCoord.xy); float n = 0.0, v = 0.0;\n"
      "  for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {\n"
      "    vec4 s = texelFetch(t, ((cell * 4 + ivec2(x, y)) * sz + sz / 256) / 128, 0);\n"
      "    float ok = dot(vmask, vmask) > 0.0 ? step(1e-6, abs(dot(s, vmask))) : 1.0;\n"
      "    n += ok * step(above, dot(s, mask)); v += ok; }\n"
      "  o = vec4(n / 16.0, v / 16.0, 0.0, 1.0); }\n";
  char log[512] = "";
  wp->fill.prog = lw_gl_program(lw_vs_fullscreen, fs, log, sizeof log);
  if (!wp->fill.prog) {
    snprintf(err, errlen, "fill monitor: %s", log);
    return false;
  }
  glGenTextures(1, &wp->fill.tex);
  glBindTexture(GL_TEXTURE_2D, wp->fill.tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 32, 32);
  glGenFramebuffers(1, &wp->fill.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, wp->fill.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, wp->fill.tex, 0);
  glGenBuffers(1, &wp->fill.pbo);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, wp->fill.pbo);
  glBufferData(GL_PIXEL_PACK_BUFFER, 32 * 32 * 4, NULL, GL_STREAM_READ);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  return true;
}

struct lw_wallpaper *lw_wallpaper_load(const char *id, char *err, size_t errlen) {
  char dir[LW_PATH];
  if (!lw_wallpaper_find(id, dir)) {
    snprintf(err, errlen, "no wallpaper named '%s'", id);
    return NULL;
  }
  struct lw_wallpaper *wp = calloc(1, sizeof *wp);
  struct lw_toml *t = read_manifest(dir, &wp->info, err, errlen);
  if (!t) {
    free(wp);
    return NULL;
  }
  const char *common = lw_toml_str(t, "", "common", NULL);
  bool ok = !common || (wp->common = read_shader(wp, common, err, errlen));
  int nb = wp->info.nbuffers;
  if (nb > LW_MAX_BUFFERS) nb = wp->info.nbuffers = LW_MAX_BUFFERS;
  wp->npass = nb + 1;
  char table[32];
  for (int i = 0; ok && i < nb; i++) {
    snprintf(table, sizeof table, "buffer.%d", i);
    ok = setup_pass(wp, t, i, table, false, err, errlen);
  }
  ok = ok && setup_pass(wp, t, nb, "image", true, err, errlen);
  for (int i = 0; ok && i < nb; i++) {
    snprintf(table, sizeof table, "buffer.%d", i);
    ok = setup_channels(wp, t, i, table, err, errlen);
  }
  ok = ok && setup_channels(wp, t, nb, "image", err, errlen);
  if (ok && wp->info.fill) ok = setup_fill(wp, t, err, errlen);
  lw_toml_free(t);
  if (!ok) {
    lw_wallpaper_free(wp);
    return NULL;
  }
  return wp;
}

static void save_state(struct lw_wallpaper *wp);

static void free_targets(struct pass *p) {
  if (p->fbo[0]) glDeleteFramebuffers(2, p->fbo);
  if (p->tex[0]) glDeleteTextures(2, p->tex);
  if (p->depth_rb) glDeleteRenderbuffers(1, &p->depth_rb);
  p->fbo[0] = p->fbo[1] = p->tex[0] = p->tex[1] = p->depth_rb = 0;
}

void lw_wallpaper_free(struct lw_wallpaper *wp) {
  if (!wp) return;
  save_state(wp);
  for (int i = 0; i < wp->npass; i++) {
    struct pass *p = &wp->pass[i];
    if (p->prog) glDeleteProgram(p->prog);
    free_targets(p);
    for (int c = 0; c < LW_MAX_CHANNELS; c++)
      if (p->ch[c].kind == CH_TEXTURE && p->ch[c].tex) glDeleteTextures(1, &p->ch[c].tex);
  }
  if (wp->fade_prog) glDeleteProgram(wp->fade_prog);
  if (wp->fill.prog) {
    glDeleteProgram(wp->fill.prog);
    glDeleteFramebuffers(1, &wp->fill.fbo);
    glDeleteTextures(1, &wp->fill.tex);
    glDeleteBuffers(1, &wp->fill.pbo);
    if (wp->fill.fence) glDeleteSync(wp->fill.fence);
  }
  free(wp->common);
  free(wp);
}

/* Whether the wallpaper moves with workspace switches (the engine pans its image,
 * or a shader reads uParallax) and is a pure function of time and view: no pass
 * carries state from frame to frame. Such a wallpaper can follow a switch while
 * paused without anything else stepping on. */
bool lw_wallpaper_follows(struct lw_wallpaper *wp) {
  bool moves = wp->info.parallax > 0;
  for (int i = 0; i < wp->npass; i++) {
    const struct pass *p = &wp->pass[i];
    if (p->tex[1] || p->accumulate || p->persist) return false;
    moves |= p->u_parallax >= 0;
  }
  return moves;
}

struct lw_wallpaper_info *lw_wallpaper_info(struct lw_wallpaper *wp) { return &wp->info; }

bool lw_wallpaper_set_param(struct lw_wallpaper *wp, const char *name, const char *value) {
  for (int i = 0; i < wp->info.nparams; i++) {
    struct lw_param *p = &wp->info.params[i];
    if (strcmp(p->name, name)) continue;
    if (p->type == LW_P_COLOR) return lw_parse_hex(value, p->value);
    float v = strtof(value, NULL);
    if (p->type == LW_P_BOOL && (!strcmp(value, "true") || !strcmp(value, "on"))) v = 1;
    if (p->type == LW_P_CHOICE)
      for (int o = 0; o < p->nopt; o++)
        if (!strcasecmp(value, p->options[o])) v = (float)o;
    if (p->type != LW_P_FLOAT) v = roundf(v);
    v = lw_clampf(v, p->min, p->max);
    if (p->restart && v != p->value[0]) wp->frame = 0; /* buffers reseed themselves on frame 0 */
    p->value[0] = v;
    return true;
  }
  return false;
}

void lw_param_format(const struct lw_param *p, const float v[3], char out[32]) {
  if (p->type == LW_P_COLOR)
    snprintf(out, 32, "#%02x%02x%02x", (int)(v[0] * 255 + 0.5f), (int)(v[1] * 255 + 0.5f), (int)(v[2] * 255 + 0.5f));
  else
    snprintf(out, 32, "%g", v[0]);
}

static bool float_targets(void) {
  static int cached = -1;
  if (cached < 0)
    cached = lw_gl_has_ext("GL_EXT_color_buffer_float") || lw_gl_has_ext("GL_EXT_color_buffer_half_float");
  return cached;
}

static bool float32_targets(void) {
  static int cached = -1;
  if (cached < 0) cached = lw_gl_has_ext("GL_EXT_color_buffer_float");
  return cached;
}

static void ensure_targets(struct lw_wallpaper *wp, struct pass *p, int base_w, int base_h) {
  int w = p->fixed_w ? p->fixed_w : (int)(base_w * p->scale + 0.5f);
  int h = p->fixed_h ? p->fixed_h : (int)(base_h * p->scale + 0.5f);
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (p->fbo[0] && p->w == w && p->h == h) return;
  free_targets(p);
  p->w = w;
  p->h = h;
  /* fall back gracefully where float targets are not renderable */
  int f = p->format, kind = formats[f].kind;
  if (kind == 2 && !float32_targets()) kind = 1, f -= 3;
  if (kind == 1 && !float_targets()) kind = 0, f -= 3;
  GLenum ifmt = formats[f].ifmt;
  p->levels = 1;
  if (p->mipmap)
    for (int m = w > h ? w : h; m > 1; m >>= 1) p->levels++;
  p->kind = kind;
  p->fresh = true;
  int ntex = p->accumulate ? 1 : 2; /* accumulating passes draw onto one target */
  glGenFramebuffers(ntex, p->fbo);
  glGenTextures(ntex, p->tex);
  if (p->depth) {
    glGenRenderbuffers(1, &p->depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, p->depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  }
  for (int i = 0; i < ntex; i++) {
    glBindTexture(GL_TEXTURE_2D, p->tex[i]);
    glTexStorage2D(GL_TEXTURE_2D, p->levels, ifmt, w, h); /* immutable: no driver revalidation */
    GLint filter = p->nearest ? GL_NEAREST : GL_LINEAR, wrap = p->repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, p->mipmap ? GL_LINEAR_MIPMAP_LINEAR : filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glBindFramebuffer(GL_FRAMEBUFFER, p->fbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->tex[i], 0);
    if (p->depth) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, p->depth_rb);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    if (p->mipmap) glGenerateMipmap(GL_TEXTURE_2D);
  }
  if (ntex == 1) p->fbo[1] = p->tex[1] = 0;
  p->cur = 0;
  p->last_time = -1;
  wp->frame = 0; /* feedback shaders seed themselves on frame 0 */
}

static GLuint black_texture(void) {
  static GLuint tex;
  if (!tex) {
    uint8_t px[4] = {0, 0, 0, 255};
    tex = lw_gl_texture_rgba(px, 1, 1, false, false);
  }
  return tex;
}

/* Tiled GPUs (Adreno, Mali, Apple) otherwise load the old contents of a
 * target into tile memory before drawing: skip that when we overwrite it all. */
static void discard(GLuint fbo, bool depth_only) {
  static const GLenum fb_color[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT}, def_color[] = {GL_COLOR};
  if (fbo) glInvalidateFramebuffer(GL_FRAMEBUFFER, depth_only ? 1 : 1, depth_only ? fb_color + 1 : fb_color);
  else glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, def_color);
}

struct frame_ctx {
  const struct lw_view *v;
  struct lw_sys *sys;
  float date[4], daytime;
  float sound[4];
};

static void set_uniforms(struct lw_wallpaper *wp, struct pass *p, int w, int h, const struct frame_ctx *f, float dt,
                         int iter, int count) {
  const struct lw_view *v = f->v;
  struct lw_sys *sys = f->sys;
  float chanres[LW_MAX_CHANNELS * 3] = {0};
  for (int c = 0; c < LW_MAX_CHANNELS; c++) {
    struct channel *k = &p->ch[c];
    GLuint tex = 0;
    int cw = 1, ch = 1;
    switch (k->kind) {
    case CH_TEXTURE: tex = k->tex, cw = k->w, ch = k->h; break;
    case CH_PASS: {
      struct pass *src = &wp->pass[k->pass];
      tex = src->tex[src->cur], cw = src->w, ch = src->h;
      break;
    }
    case CH_AUDIO: tex = sys->audio_tex ? sys->audio_tex : black_texture(), cw = LW_AUDIO_BINS, ch = 2; break;
    case CH_BACKGROUND:
      if (!sys->bg_tex || sys->bg_stale) lw_background_refresh(sys);
      tex = sys->bg_tex, cw = sys->bg_w, ch = sys->bg_h;
      break;
    default: continue;
    }
    glActiveTexture(GL_TEXTURE0 + c);
    glBindTexture(GL_TEXTURE_2D, tex);
    chanres[c * 3] = (float)cw, chanres[c * 3 + 1] = (float)ch, chanres[c * 3 + 2] = 1;
  }
  glUniform3f(p->u_res, (float)w, (float)h, 1.0f);
  glUniform1f(p->u_time, (float)v->time);
  glUniform1f(p->u_dt, dt);
  glUniform1i(p->u_frame, wp->frame);
  glUniform1i(p->u_pass, iter);
  glUniform1i(p->u_count, count);
  glUniform4f(p->u_mouse, v->mouse[0] * w, v->mouse[1] * h, v->mouse[2] * w, v->mouse[3] * h);
  glUniform4fv(p->u_date, 1, f->date);
  glUniform3fv(p->u_chanres, LW_MAX_CHANNELS, chanres);
  glUniform4fv(p->u_ws, 1, v->workspace);
  glUniform1f(p->u_focus, (float)(lw_now() - sys->focus_time));
  glUniform4f(p->u_batt, sys->battery, sys->charging, sys->on_battery, sys->has_battery);
  glUniform4fv(p->u_audio, 1, sys->audio);
  glUniform1f(p->u_day, f->daytime);
  glUniform4fv(p->u_ptr, 1, v->pointer);
  glUniform4fv(p->u_sound, 1, f->sound);
  glUniform1f(p->u_reverse, wp->fill.rev_s);
  glUniform1f(p->u_epoch, wp->epoch);
  glUniform2fv(p->u_parallax, 1, v->parallax);
  glUniform2f(p->u_shift, v->parallax[0] * wp->info.parallax * w, v->parallax[1] * wp->info.parallax * h);

  /* Theme and parameter uniforms are program state: upload only on change. */
  const struct lw_theme *th = &sys->theme;
  float tc[58];
  memcpy(tc, th->bg, 12), memcpy(tc + 3, th->fg, 12), memcpy(tc + 6, th->accent, 12);
  memcpy(tc + 9, th->palette, 192), tc[57] = th->dark;
  if (!p->cache_valid || memcmp(tc, p->theme_cache, sizeof tc)) {
    glUniform3fv(p->u_bg, 1, th->bg);
    glUniform3fv(p->u_fg, 1, th->fg);
    glUniform3fv(p->u_accent, 1, th->accent);
    glUniform3fv(p->u_pal, 16, &th->palette[0][0]);
    glUniform1f(p->u_dark, th->dark);
    memcpy(p->theme_cache, tc, sizeof tc);
  }
  for (int i = 0; i < wp->info.nparams; i++) {
    struct lw_param *pr = &wp->info.params[i];
    if (p->u_param[i] < 0 || (p->cache_valid && !memcmp(p->param_cache[i], pr->value, sizeof pr->value))) continue;
    if (pr->type == LW_P_COLOR) glUniform3fv(p->u_param[i], 1, pr->value);
    else glUniform1f(p->u_param[i], pr->value[0]);
    memcpy(p->param_cache[i], pr->value, sizeof pr->value);
  }
  p->cache_valid = true;
}

static void set_blend(int blend) {
  if (blend == BLEND_NONE) {
    glDisable(GL_BLEND);
    return;
  }
  glEnable(GL_BLEND);
  glBlendEquation(blend == BLEND_MAX ? GL_MAX : blend == BLEND_MIN ? GL_MIN : GL_FUNC_ADD);
  switch (blend) {
  case BLEND_ADD: glBlendFunc(GL_ONE, GL_ONE); break;
  case BLEND_ALPHA: glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break; /* premultiplied */
  case BLEND_MULTIPLY: glBlendFunc(GL_DST_COLOR, GL_ZERO); break;
  default: glBlendFunc(GL_ONE, GL_ONE); break;
  }
}

/* Accumulating passes draw onto one persistent target: cleared each frame, or
 * faded with a blend (no read pass) so whatever they draw leaves trails. */
static void run_accumulate(struct lw_wallpaper *wp, struct pass *p, const struct frame_ctx *f, float dt) {
  glBindFramebuffer(GL_FRAMEBUFFER, p->fbo[0]);
  glViewport(0, 0, p->w, p->h);
  float fade = live(wp, p->fade);
  if (fade <= 0 || wp->frame == 0) {
    discard(p->fbo[0], false);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | (p->depth ? GL_DEPTH_BUFFER_BIT : 0));
  } else {
    if (p->depth) glClear(GL_DEPTH_BUFFER_BIT);
    if (fade < 1) {
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      glBlendFunc(GL_ZERO, GL_CONSTANT_ALPHA);
      glBlendColor(0, 0, 0, powf(fade, fminf(dt, 0.25f) * 60.0f)); /* frame-rate independent */
      glUseProgram(wp->fade_prog);
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }
  }
  int count = (int)live(wp, p->count);
  if (p->src_pass >= 0) {
    struct pass *src = &wp->pass[p->src_pass];
    int texels = src->w * src->h;
    count = count > 0 && count < texels ? count : texels;
  }
  glUseProgram(p->prog);
  set_uniforms(wp, p, p->w, p->h, f, dt, 0, count);
  if (p->draw != DRAW_FULLSCREEN) {
    glActiveTexture(GL_TEXTURE0 + LW_MAX_CHANNELS);
    if (p->src_pass >= 0) {
      struct pass *src = &wp->pass[p->src_pass];
      glBindTexture(GL_TEXTURE_2D, src->tex[src->cur]);
      glUniform2i(p->u_srcsize, src->w, src->h);
    } else {
      glBindTexture(GL_TEXTURE_2D, black_texture());
      glUniform2i(p->u_srcsize, 0, 0);
    }
  }
  set_blend(p->blend);
  if (p->depth) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
  }
  if (count > 0 || p->draw == DRAW_FULLSCREEN) glDrawArrays(draw_modes[p->draw], 0, p->draw == DRAW_FULLSCREEN ? 3 : count);
  if (p->depth) {
    glDisable(GL_DEPTH_TEST);
    discard(p->fbo[0], true); /* depth never needs to leave tile memory */
  }
  glDisable(GL_BLEND);
}

static void fill_monitor(struct lw_wallpaper *wp);


/* Persistent buffers: a header and the raw texels (RGBA, float or bytes). */
struct state_header {
  char magic[4];
  int w, h, kind, frame;
  float epoch;
  double saved; /* wall-clock seconds */
};

static void state_file(struct lw_wallpaper *wp, struct pass *p, char out[LW_PATH + 64]) {
  snprintf(out, LW_PATH + 64, "%s/%s.bin", wp->state, p->name);
}

static bool any_persist(struct lw_wallpaper *wp) {
  for (int i = 0; i < wp->npass - 1; i++)
    if (wp->pass[i].persist) return true;
  return false;
}

void lw_wallpaper_persist(struct lw_wallpaper *wp, const char *key) {
  if (!any_persist(wp)) return;
  char rel[160];
  snprintf(rel, sizeof rel, "%s/%s", wp->info.id, key);
  lw_dir_state(wp->state, rel);
}

static void save_state(struct lw_wallpaper *wp) {
  if (!wp->state[0] || wp->frame < 2) return;
  lw_mkdir_p(wp->state);
  for (int i = 0; i < wp->npass - 1; i++) {
    struct pass *p = &wp->pass[i];
    if (!p->persist || !p->fbo[0]) continue;
    size_t texel = p->kind ? 16 : 4, n = (size_t)p->w * p->h * texel;
    void *px = malloc(n);
    if (!px) continue;
    glBindFramebuffer(GL_FRAMEBUFFER, p->fbo[p->cur]);
    glReadPixels(0, 0, p->w, p->h, GL_RGBA, p->kind ? GL_FLOAT : GL_UNSIGNED_BYTE, px);
    struct state_header hd = {{'L', 'W', 'S', '1'}, p->w, p->h, p->kind, wp->frame, wp->epoch, (double)time(NULL)};
    char path[LW_PATH + 64], tmp[LW_PATH + 80];
    state_file(wp, p, path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fwrite(&hd, sizeof hd, 1, f) == 1 && fwrite(px, n, 1, f) == 1;
    if (f) ok = fclose(f) == 0 && ok;
    if (ok) rename(tmp, path);
    else unlink(tmp);
    free(px);
    lw_dbg("%s: saved %s (%dx%d, frame %d)%s", wp->info.id, p->name, p->w, p->h, wp->frame, ok ? "" : " FAILED");
  }
  wp->last_save = lw_now();
}

static void forget_state(struct lw_wallpaper *wp) {
  if (!wp->state[0]) return;
  char path[LW_PATH + 64];
  for (int i = 0; i < wp->npass - 1; i++)
    if (wp->pass[i].persist) {
      state_file(wp, &wp->pass[i], path);
      unlink(path);
    }
}

/* After targets are (re)created: load what was saved, if it still fits. */
static void restore_state(struct lw_wallpaper *wp) {
  int frame = 0;
  for (int i = 0; i < wp->npass - 1; i++) {
    struct pass *p = &wp->pass[i];
    if (!p->persist || !p->fresh) continue;
    char path[LW_PATH + 64];
    state_file(wp, p, path);
    size_t len;
    char *data = lw_read_file(path, &len);
    if (!data) continue;
    struct state_header hd;
    size_t texel = p->kind ? 16 : 4, n = (size_t)p->w * p->h * texel;
    memcpy(&hd, data, len >= sizeof hd ? sizeof hd : 0);
    if (len == sizeof hd + n && !memcmp(hd.magic, "LWS1", 4) && hd.w == p->w && hd.h == p->h && hd.kind == p->kind) {
      /* repack RGBA into the texture's own component count */
      int comps = formats[p->format].name[1] == 'g' && formats[p->format].name[2] == 'b' ? 4
                  : formats[p->format].name[1] == 'g'                                    ? 2
                                                                                          : 1;
      size_t cs = p->kind ? 4 : 1;
      char *src = data + sizeof hd;
      for (size_t k = 0, m = (size_t)p->w * p->h; k < m; k++) memmove(src + k * cs * comps, src + k * cs * 4, cs * comps);
      GLenum fmt = comps == 4 ? GL_RGBA : comps == 2 ? GL_RG : GL_RED;
      glBindTexture(GL_TEXTURE_2D, p->tex[p->cur]);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, p->w, p->h, fmt, p->kind ? GL_FLOAT : GL_UNSIGNED_BYTE, src);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
      if (p->mipmap) glGenerateMipmap(GL_TEXTURE_2D);
      if (hd.frame > frame) frame = hd.frame;
      wp->epoch = hd.epoch;
      wp->away = (float)fmax(0, (double)time(NULL) - hd.saved);
      lw_dbg("%s: restored %s (frame %d, away %.0fs)", wp->info.id, p->name, hd.frame, wp->away);
    }
    free(data);
  }
  if (frame) wp->frame = frame; /* past the seeding frames: shaders keep what was restored */
  for (int i = 0; i < wp->npass - 1; i++) wp->pass[i].fresh = false;
  wp->last_save = lw_now();
}

void lw_wallpaper_render(struct lw_wallpaper *wp, GLuint target_fbo, int w, int h, int base_w, int base_h,
                         const struct lw_view *view, struct lw_sys *sys) {
  struct timespec ts;
  struct tm tm;
  clock_gettime(CLOCK_REALTIME, &ts);
  localtime_r(&ts.tv_sec, &tm);
  float secs = tm.tm_hour * 3600.0f + tm.tm_min * 60.0f + tm.tm_sec + ts.tv_nsec * 1e-9f;
  struct frame_ctx f = {view, sys, {tm.tm_year + 1900.0f, (float)tm.tm_mon, (float)tm.tm_mday, secs}, secs / 3600.0f,
                        {0, 1e4f, 0, 0}};
  float amount = lw_wallpaper_sound(wp);
  if (amount > 0 || wp->info.audio) {
    float a = amount > 0 ? amount : 1;
    f.sound[0] = sys->sound_energy * a;
    f.sound[1] = sys->kick_time > 0 ? (float)(lw_now() - sys->kick_time) : 1e4f;
    f.sound[2] = sys->kick_strength * a;
    f.sound[3] = (float)sys->kicks;
  }
  if (wp->fill.prog) { /* the reverse signal builds up over `ramp` seconds and falls four times as fast */
    float step = (float)fmin(view->dt, 0.25) / wp->fill.ramp; /* a linear ramp over `ramp` seconds */
    wp->fill.rev_s = lw_clampf(wp->fill.rev_s + (wp->fill.reversing ? step : -step * 4), 0, 1);
  }

  glDisable(GL_BLEND);
  glActiveTexture(GL_TEXTURE0 + NOISE_UNIT);
  glBindTexture(GL_TEXTURE_2D, noise_texture());
  bool fresh = false;
  for (int i = 0; i < wp->npass - 1; i++) {
    ensure_targets(wp, &wp->pass[i], base_w, base_h);
    fresh |= wp->pass[i].fresh;
  }
  if (fresh) {
    if (wp->state[0]) restore_state(wp);
    else for (int i = 0; i < wp->npass - 1; i++) wp->pass[i].fresh = false;
  }
  for (int i = 0; i < wp->npass - 1; i++) {
    struct pass *p = &wp->pass[i];
    /* every = N: update this pass on every Nth frame only (slow fields, costly sims) */
    int every = (int)lw_clampf(live(wp, p->every), 1, 120);
    if (wp->frame > 0 && every > 1 && (wp->frame + i) % every) continue;
    float dt = p->last_time >= 0 ? (float)(view->time - p->last_time) : (float)view->dt;
    p->last_time = view->time;
    if (p->accumulate) {
      run_accumulate(wp, p, &f, dt);
    } else {
      int iters = (int)lw_clampf(live(wp, p->iterations), 1, 128);
      glUseProgram(p->prog);
      glViewport(0, 0, p->w, p->h);
      for (int k = 0; k < iters; k++) {
        glBindFramebuffer(GL_FRAMEBUFFER, p->fbo[1 - p->cur]);
        discard(p->fbo[1 - p->cur], false);
        set_uniforms(wp, p, p->w, p->h, &f, dt, k, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        p->cur = 1 - p->cur;
      }
    }
    if (p->mipmap) {
      glBindTexture(GL_TEXTURE_2D, p->tex[p->cur]);
      glGenerateMipmap(GL_TEXTURE_2D);
    }
  }
  struct pass *img = &wp->pass[wp->npass - 1];
  glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
  discard(target_fbo, false);
  glViewport(0, 0, w, h);
  glUseProgram(img->prog);
  set_uniforms(wp, img, w, h, &f, (float)view->dt, 0, 0);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  if (wp->frame == 0) wp->fill.start = wp->fill.peak_time = view->time;
  wp->frame++;
  wp->clock = view->time;
  if (wp->fill.prog) fill_monitor(wp);
  if (wp->state[0] && lw_now() - wp->last_save > 300) save_state(wp); /* every five minutes */
  if (wp->away > 0 && wp->frame > 1) wp->away = 0;
}

float lw_wallpaper_coverage(struct lw_wallpaper *wp) { return wp->fill.prog ? wp->fill.coverage : -1; }

float lw_wallpaper_sound(struct lw_wallpaper *wp) {
  int i = param_index(wp, "lw_sound");
  if (i < 0) return 0;
  int mode = (int)wp->info.params[i].value[0]; /* global, off, on */
  bool on = mode == 2 || (mode == 0 && E.cfg.sound_react);
  return on ? E.cfg.sound_strength : 0;
}

void lw_wallpaper_restart(struct lw_wallpaper *wp) {
  forget_state(wp);
  wp->epoch += 1;
  wp->frame = 0;
  wp->fill.reversing = false;
  wp->fill.rev_s = 0;
  wp->fill.measured = 0;
  wp->fill.peak = 0;
  wp->fill.peak_time = wp->fill.start = wp->clock;
}

/* A random starting epoch: every load is a fresh roll (maps, seeds, soups), while
 * thumbnails and benchmarks, which never call this, stay deterministic. */
void lw_wallpaper_shuffle(struct lw_wallpaper *wp) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  unsigned x = (unsigned)ts.tv_nsec ^ (unsigned)ts.tv_sec * 2654435761u ^ (unsigned)getpid() * 40503u;
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
  wp->epoch = (float)(1 + x % 9999);
}

float lw_wallpaper_epoch(struct lw_wallpaper *wp) { return wp->epoch; }
void lw_wallpaper_set_epoch(struct lw_wallpaper *wp, float e) { wp->epoch = e; }

void lw_wallpaper_request_restart(struct lw_wallpaper *wp) { wp->fill.reset_pending = true; }

bool lw_wallpaper_take_reset(struct lw_wallpaper *wp) {
  bool r = wp->fill.reset_pending;
  wp->fill.reset_pending = false;
  return r;
}

/* Decide what to do with a fresh coverage reading. The screen counts as full
 * when coverage passes `full`, or when it has stopped spreading for `settle`
 * seconds while covering at least `settle_min` (a grown pattern rarely covers
 * every pixel: a finished coral is about a fifth of them). */
static void fill_act(struct lw_wallpaper *wp) {
  int mode = wp->fill.param >= 0 ? (int)wp->info.params[wp->fill.param].value[0] : 1; /* continue, restart, reverse */
  float c = wp->fill.coverage;
  double now = wp->clock; /* the wallpaper's clock: pauses and speed settings apply */
  if (wp->fill.measured++ == 0 || c > wp->fill.peak + 0.01f) wp->fill.peak = fmaxf(c, wp->fill.peak), wp->fill.peak_time = now;
  if (mode != 2) wp->fill.reversing = false;
  if (mode == 0 || now - wp->fill.start < wp->fill.grace) return; /* let a fresh start settle first */
  bool settled = wp->fill.settle > 0 && c >= wp->fill.settle_min && now - wp->fill.peak_time > wp->fill.settle;
  bool full = c >= wp->fill.full || settled;
  bool empty = wp->fill.empty > 0 && c <= wp->fill.empty && (wp->fill.measured > 4 || wp->fill.reversing);
  if (mode == 2 && !empty) {
    if (!wp->fill.reversing && full) {
      wp->fill.reversing = true;
      wp->fill.rev_since = now;
      wp->fill.rev_from = c;
      lw_dbg("%s: %.0f%% %s, running back", wp->info.id, c * 100, settled ? "and settled" : "full");
    } else if (wp->fill.reversing && c <= wp->fill.rev_from * wp->fill.low) {
      wp->fill.reversing = false;
      wp->fill.peak = c, wp->fill.peak_time = now; /* a new growth phase */
      lw_dbg("%s: down to %.0f%%, growing again", wp->info.id, c * 100);
    } else if (wp->fill.reversing && now - wp->fill.rev_since > 90) {
      wp->fill.reset_pending = true; /* it is not clearing: start over instead */
    }
    return;
  }
  if (full || empty) {
    lw_dbg("%s: %.0f%% %s, restarting", wp->info.id, c * 100, empty ? "(died out)" : "full");
    wp->fill.reset_pending = true;
  }
}

static void fill_monitor(struct lw_wallpaper *wp) {
  if (wp->fill.fence) { /* collect the last measurement, if the GPU has finished it */
    if (glClientWaitSync(wp->fill.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0) == GL_TIMEOUT_EXPIRED) return;
    glDeleteSync(wp->fill.fence);
    wp->fill.fence = 0;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, wp->fill.pbo);
    const uint8_t *px = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, 32 * 32 * 4, GL_MAP_READ_BIT);
    if (px) {
      unsigned filled = 0, valid = 0;
      for (int i = 0; i < 32 * 32; i++) filled += px[i * 4], valid += px[i * 4 + 1];
      wp->fill.coverage = valid ? (float)filled / (float)valid : 0;
      glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      lw_dbg("%s: coverage %.3f (frame %d, rev %.2f)", wp->info.id, wp->fill.coverage, wp->frame, wp->fill.rev_s);
      if (wp->frame > 30) fill_act(wp);
    } else {
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
    return;
  }
  if (wp->frame % 30) return; /* about once a second */
  struct pass *src = &wp->pass[wp->fill.pass];
  glBindFramebuffer(GL_FRAMEBUFFER, wp->fill.fbo);
  glViewport(0, 0, 32, 32);
  glUseProgram(wp->fill.prog);
  float mask[4] = {0}, vmask[4] = {0};
  mask[wp->fill.channel] = 1;
  if (wp->fill.valid >= 0) vmask[wp->fill.valid] = 1;
  glUniform4fv(glGetUniformLocation(wp->fill.prog, "mask"), 1, mask);
  glUniform4fv(glGetUniformLocation(wp->fill.prog, "vmask"), 1, vmask);
  glUniform1f(glGetUniformLocation(wp->fill.prog, "above"), wp->fill.above);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, src->tex[src->cur]);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, wp->fill.pbo);
  glReadPixels(0, 0, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE, 0);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  wp->fill.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}
