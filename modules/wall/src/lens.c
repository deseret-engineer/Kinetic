/* Rendered glass under the bar: the wallpaper, bent behind the pill's capsules.
 *
 * Hyprland can only blur what lies behind a surface. The wallpaper engine has
 * the actual pixels, so it can bend them. The bar sends its capsules over the
 * socket (`lens`), per output in logical px, with optics from the bar's options.
 * (Glass behind windows is drawn inside Hyprland by Kinetic's plugin,
 * modules/glass/plugin, which sees each window where it really is on every
 * frame; the engine only sends it its settings, plugin.c.)
 *
 * After a frame is drawn, the region behind the capsules is copied into a
 * texture and drawn back through rounded-rect lenses: refraction that grows
 * toward the rim on an adjustable profile, frost (mip blur), dispersion,
 * saturation and brightness, a tint toward the theme, and a rim light from a
 * chosen angle. The bar still draws its own tint and rim on top, crisp.
 *
 * Cost: one blit of the covered region, its mipmaps, and one scissored quad,
 * on frames the wallpaper draws anyway. Nothing at all while no glass is
 * registered. */
#include "lw.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LENS_SLOTS 16
#define LENS_RECTS 16

struct optics {
  float refract; /* rim displacement, logical px */
  float frost;   /* blur, as a mip level (0 = sharp) */
  float disp;    /* dispersion: 0..1 split of the color channels */
  float bevel;   /* how far in the glass curves, logical px; 0 = half the shape's thinner side */
  float curve;   /* edge profile: 1 a rounded edge, under 1 a soft slope, over 1 a sharp rim */
  float sat, bright, tint; /* saturation, brightness offset, mix toward the theme background */
  float spec, light;       /* rim light strength, and the angle it comes from (degrees, 0 = right, 90 = up) */
};

struct lens_slot {
  bool used;
  char output[64];
  int n;
  float rect[LENS_RECTS][5]; /* x, y, w, h, radius in logical px, y down */
  struct optics op;
  GLuint tex, fbo;
  int tw, th, levels;
};

static struct lens_slot slots[LENS_SLOTS];
static GLuint prog;
static GLint u_tex, u_box, u_rect, u_radius, u_count, u_optics, u_look, u_light, u_tint;

static const char *fs =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec4 uBox;\n"       /* copied region: x, y, w, h in buffer px (GL, y up) */
    "uniform vec4 uRect[16];\n"  /* center x, y and half size, buffer px */
    "uniform float uRadius[16];\n"
    "uniform int uCount;\n"
    "uniform vec4 uOptics;\n" /* displacement px, frost lod, dispersion, bevel px (0 = half the thinner side) */
    "uniform vec4 uLook;\n"   /* saturation, brightness, tint amount, edge profile exponent */
    "uniform vec4 uLight;\n"  /* light direction xy, rim light strength */
    "uniform vec3 uTint;\n"
    "out vec4 frag;\n"
    "float sdr(vec2 p, vec2 b, float r){ vec2 q = abs(p) - b + r;\n"
    "  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }\n"
    /* Frost: the copy's mip levels, read through a cubic B-spline (four bilinear taps:
     * GPU Gems 2, ch. 20). Plain bilinear on a level a quarter the size shows blocks;
     * the B-spline is smooth, close to a Gaussian, and fractional frost blends two levels. */
    "vec3 bspline(vec2 uv, int lod){\n"
    "  vec2 size = vec2(textureSize(uTex, lod)), p = uv * size - 0.5, i = floor(p), f = p - i;\n"
    "  vec2 w0 = (1.0 - f) * (1.0 - f) * (1.0 - f) / 6.0, w1 = (3.0 * f * f * f - 6.0 * f * f + 4.0) / 6.0;\n"
    "  vec2 w3 = f * f * f / 6.0, w2 = 1.0 - w0 - w1 - w3, g0 = w0 + w1, g1 = w2 + w3;\n"
    "  vec2 h0 = (i - 0.5 + w1 / g0) / size, h1 = (i + 1.5 + w3 / g1) / size;\n"
    "  float l = float(lod);\n"
    "  return g0.y * (g0.x * textureLod(uTex, h0, l).rgb + g1.x * textureLod(uTex, vec2(h1.x, h0.y), l).rgb)\n"
    "       + g1.y * (g0.x * textureLod(uTex, vec2(h0.x, h1.y), l).rgb + g1.x * textureLod(uTex, h1, l).rgb); }\n"
    "vec3 fetch(vec2 q){\n"
    "  vec2 uv = (q - uBox.xy) / uBox.zw;\n"
    "  if (uOptics.y <= 0.0) return textureLod(uTex, uv, 0.0).rgb;\n"
    "  int l0 = int(floor(uOptics.y)); float t = uOptics.y - float(l0);\n"
    "  vec3 a = bspline(uv, l0);\n"
    "  return t > 0.01 ? mix(a, bspline(uv, l0 + 1), t) : a; }\n"
    "void main(){\n"
    "  vec2 p = gl_FragCoord.xy;\n"
    "  float d = 1e9; vec2 c = vec2(0.0), b = vec2(1.0); float r = 0.0;\n"
    "  for (int i = 0; i < 16; i++) { if (i >= uCount) break;\n"
    "    float di = sdr(p - uRect[i].xy, uRect[i].zw, uRadius[i]);\n"
    "    if (di < d) { d = di; c = uRect[i].xy; b = uRect[i].zw; r = uRadius[i]; } }\n"
    "  float cover = clamp(0.5 - d, 0.0, 1.0);\n"
    "  if (cover <= 0.0) discard;\n"
    /* outward normal from the distance field */
    "  vec2 e = vec2(0.75, 0.0);\n"
    "  vec2 g = vec2(sdr(p + e.xy - c, b, r) - sdr(p - e.xy - c, b, r), sdr(p + e.yx - c, b, r) - sdr(p - e.yx - c, b, r));\n"
    "  vec2 n = dot(g, g) > 1e-8 ? normalize(g) : vec2(0.0);\n"
    /* the glass edge: a superellipse profile, flat inside the bevel, steep at the rim */
    "  float band = max(1.0, uOptics.w > 0.0 ? min(uOptics.w, min(b.x, b.y)) : min(b.x, b.y));\n"
    "  float t = clamp(-d / band, 0.0, 1.0), u = 1.0 - t, k = uLook.w;\n"
    "  float bend = 1.0 - pow(max(0.0, 1.0 - pow(u, k)), 1.0 / k);\n"
    "  vec2 off = -n * uOptics.x * bend;\n"
    "  vec3 col;\n"
    "  if (uOptics.z > 0.0 && dot(off, off) > 0.25) {\n" /* color fringes only where the glass bends */
    "    float s = uOptics.z * 0.5;\n"
    "    col = vec3(fetch(p + off * (1.0 - s)).r, fetch(p + off).g, fetch(p + off * (1.0 + s)).b);\n"
    "  } else col = fetch(p + off);\n"
    "  col = mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, uLook.x) + uLook.y;\n"
    "  col = mix(col, uTint, uLook.z);\n"
    /* light catching the curved rim: strongest where the edge faces the light */
    "  float rim = pow(u, 3.0) * max(0.0, dot(n, uLight.xy));\n"
    "  col += uLight.z * rim * 0.6;\n"
    "  frag = vec4(clamp(col, 0.0, 1.0) * cover, cover);\n"
    "}\n";

static struct optics bar_defaults(void) {
  return (struct optics){.refract = 4, .frost = 2.5, .disp = 0, .bevel = 0, .curve = 1, .sat = 1, .bright = 0, .tint = 0, .spec = 0, .light = 120};
}

static struct lens_slot *slot_for(const char *output, bool create) {
  for (int i = 0; i < LENS_SLOTS; i++)
    if (slots[i].used && !strcmp(slots[i].output, output)) return &slots[i];
  if (!create) return NULL;
  for (int i = 0; i < LENS_SLOTS; i++)
    if (!slots[i].used) {
      memset(&slots[i], 0, sizeof slots[i]);
      slots[i].used = true;
      snprintf(slots[i].output, sizeof slots[i].output, "%s", output);
      return &slots[i];
    }
  return NULL;
}

static void drop(struct lens_slot *s) {
  if (!s) return;
  if (s->tex) glDeleteTextures(1, &s->tex);
  if (s->fbo) glDeleteFramebuffers(1, &s->fbo);
  s->used = false;
}

static void redraw(const char *output) {
  for (struct lw_output *o = E.outputs; o; o = o->next) {
    if (strcmp(o->name, output) && strcmp(output, "*")) continue;
    if (o->paused && o->wp && o->configured) lw_wl_render(o); /* a paused wallpaper still shows the new glass */
    else lw_engine_invalidate(o);
  }
}

/* "x,y,w,h,r;x,y,w,h,r..." -> rects. Returns the count, or -1 if malformed. */
static int parse_rects(const char *spec, float out[LENS_RECTS][5]) {
  int n = 0;
  const char *s = spec;
  while (*s && n < LENS_RECTS) {
    char *end;
    for (int k = 0; k < 5; k++) {
      out[n][k] = strtof(s, &end);
      if (end == s || !isfinite(out[n][k])) return -1;
      s = end;
      if (k < 4) {
        if (*s != ',') return -1;
        s++;
      }
    }
    if (out[n][2] <= 0 || out[n][3] <= 0) return -1;
    n++;
    if (*s == ';') s++;
    else if (*s) return -1;
  }
  return n;
}

/* "bevel=24,curve=1.2,sat=1.1,bright=0,tint=0,spec=0.3,light=120": the optics past the first three. */
static bool parse_extras(const char *spec, struct optics *op, char *err, size_t errlen) {
  char buf[256];
  snprintf(buf, sizeof buf, "%s", spec);
  for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    char *eq = strchr(tok, '=');
    if (!eq) return snprintf(err, errlen, "bad optic '%s' (want key=value)", tok), false;
    *eq = 0;
    char *end;
    float v = strtof(eq + 1, &end);
    if (end == eq + 1 || *end || !isfinite(v)) return snprintf(err, errlen, "bad value for %s", tok), false;
    if (!strcmp(tok, "bevel")) op->bevel = lw_clampf(v, 0, 200);
    else if (!strcmp(tok, "curve")) op->curve = lw_clampf(v, 0.3f, 4);
    else if (!strcmp(tok, "sat")) op->sat = lw_clampf(v, 0, 2);
    else if (!strcmp(tok, "bright")) op->bright = lw_clampf(v, -0.5f, 0.5f);
    else if (!strcmp(tok, "tint")) op->tint = lw_clampf(v, 0, 1);
    else if (!strcmp(tok, "spec")) op->spec = lw_clampf(v, 0, 1);
    else if (!strcmp(tok, "light")) op->light = fmodf(fmodf(v, 360) + 360, 360);
    else return snprintf(err, errlen, "no optic named '%s' (bevel, curve, sat, bright, tint, spec, light)", tok), false;
  }
  return true;
}

/* lens <output> off
 * lens <output> <refract> <frost> <dispersion> <rects> [bevel=..,curve=..,sat=..,bright=..,tint=..,spec=..,light=..]
 * lens                      list what is registered */
int lw_lens_command(int argc, char **argv, char *out, size_t len) {
  if (argc < 2) {
    size_t n = 0;
    for (int i = 0; i < LENS_SLOTS && n + 200 < len; i++) {
      struct lens_slot *s = &slots[i];
      if (!s->used) continue;
      struct optics op = s->op;
      n += (size_t)snprintf(out + n, len - n,
                            "%s: %d %s, refract %g, frost %g, dispersion %g, bevel %g, curve %g, saturation %g, "
                            "brightness %g, tint %g, rim light %g at %g°\n",
                            s->output, s->n, s->n == 1 ? "capsule" : "capsules",
                            op.refract, op.frost, op.disp, op.bevel, op.curve, op.sat, op.bright, op.tint, op.spec, op.light);
    }
    if (!n) n = (size_t)snprintf(out, len, "(no glass registered)\n");
    return (int)n;
  }
  if (argc == 3 && !strcmp(argv[2], "off")) {
    drop(slot_for(argv[1], false));
    redraw(argv[1]);
    return snprintf(out, len, "ok\n");
  }
  if (argc < 6)
    return snprintf(out, len, "error: usage: lens <output> <refract> <frost> <dispersion> <x,y,w,h,r;...> [bevel=..,curve=..,sat=..,bright=..,tint=..,spec=..,light=..]\n");
  float rects[LENS_RECTS][5];
  int n = parse_rects(argv[5], rects);
  if (n < 0) return snprintf(out, len, "error: bad rectangles '%s'\n", argv[5]);
  struct optics op = bar_defaults();
  op.refract = lw_clampf(strtof(argv[2], NULL), 0, 60);
  op.frost = lw_clampf(strtof(argv[3], NULL), 0, 6);
  op.disp = lw_clampf(strtof(argv[4], NULL), 0, 1);
  char err[160];
  if (argc > 6 && !parse_extras(argv[6], &op, err, sizeof err)) return snprintf(out, len, "error: %s\n", err);
  struct lens_slot *s = slot_for(argv[1], true);
  if (!s) return snprintf(out, len, "error: too many outputs\n");
  s->n = n;
  memcpy(s->rect, rects, sizeof rects);
  s->op = op;
  redraw(argv[1]);
  return snprintf(out, len, "ok %d\n", n);
}

static bool ensure_target(struct lens_slot *s, int w, int h, int levels) {
  if (s->tex && s->tw == w && s->th == h && s->levels == levels) return true;
  if (s->tex) glDeleteTextures(1, &s->tex);
  if (!s->fbo) glGenFramebuffers(1, &s->fbo);
  glGenTextures(1, &s->tex);
  glBindTexture(GL_TEXTURE_2D, s->tex);
  glTexStorage2D(GL_TEXTURE_2D, levels, GL_RGBA8, w, h);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->tex, 0);
  bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  s->tw = w;
  s->th = h;
  s->levels = levels;
  return ok;
}

bool lw_lens_active(const char *output) {
  struct lens_slot *s = slot_for(output, false);
  return s && s->n > 0;
}

static bool load_program(void) {
  if (prog) return true;
  char err[512] = "";
  prog = lw_gl_program(lw_vs_fullscreen, fs, err, sizeof err);
  if (!prog) {
    lw_log("lens: %s", err);
    return false;
  }
  u_tex = glGetUniformLocation(prog, "uTex");
  u_box = glGetUniformLocation(prog, "uBox");
  u_rect = glGetUniformLocation(prog, "uRect");
  u_radius = glGetUniformLocation(prog, "uRadius");
  u_count = glGetUniformLocation(prog, "uCount");
  u_optics = glGetUniformLocation(prog, "uOptics");
  u_look = glGetUniformLocation(prog, "uLook");
  u_light = glGetUniformLocation(prog, "uLight");
  u_tint = glGetUniformLocation(prog, "uTint");
  return true;
}

/* Draws the glass into `target`, which holds the finished frame: bw x bh pixels
 * showing a lw x lh logical output. */
static void draw(struct lens_slot *s, GLuint target, int bw, int bh, int lw, int lh) {
  if (!s || s->n == 0 || lw <= 0 || lh <= 0 || !load_program()) return;
  struct optics op = s->op;
  float sx = (float)bw / lw, sy = (float)bh / lh;
  float rect[LENS_RECTS * 4], radius[LENS_RECTS];
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  int n = 0;
  for (int i = 0; i < s->n; i++) {
    float *r = s->rect[i];
    float w = r[2] * sx, h = r[3] * sy;
    float cx = (r[0] + r[2] * 0.5f) * sx, cy = bh - (r[1] + r[3] * 0.5f) * sy;
    if (cx + w * 0.5f < 0 || cx - w * 0.5f > bw || cy + h * 0.5f < 0 || cy - h * 0.5f > bh) continue;
    rect[n * 4 + 0] = cx;
    rect[n * 4 + 1] = cy;
    rect[n * 4 + 2] = w * 0.5f;
    rect[n * 4 + 3] = h * 0.5f;
    radius[n] = fminf(r[4] * fminf(sx, sy), fminf(w, h) * 0.5f);
    x0 = fminf(x0, cx - w * 0.5f);
    x1 = fmaxf(x1, cx + w * 0.5f);
    y0 = fminf(y0, cy - h * 0.5f);
    y1 = fmaxf(y1, cy + h * 0.5f);
    n++;
  }
  if (!n) return;
  int bx0 = (int)fmaxf(0, floorf(x0) - 2), by0 = (int)fmaxf(0, floorf(y0) - 2);
  int bx1 = (int)fminf(bw, ceilf(x1) + 2), by1 = (int)fminf(bh, ceilf(y1) + 2);
  int w = bx1 - bx0, h = by1 - by0;
  if (w < 4 || h < 4) return;
  int levels = 1;
  if (op.frost > 0) {
    levels = 2 + (int)floorf(op.frost); /* the B-spline blends the level below the frost and the one above */
    int most = 1 + (int)floorf(log2f((float)(w < h ? w : h)));
    if (levels > most) levels = most;
  }
  if (!ensure_target(s, w, h, levels)) return;

  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, target);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s->fbo);
  glBlitFramebuffer(bx0, by0, bx1, by1, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, target);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, s->tex);
  if (levels > 1) glGenerateMipmap(GL_TEXTURE_2D);

  glViewport(0, 0, bw, bh);
  glEnable(GL_SCISSOR_TEST);
  glScissor(bx0, by0, w, h);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(prog);
  glUniform1i(u_tex, 0);
  glUniform4f(u_box, (float)bx0, (float)by0, (float)w, (float)h);
  glUniform4fv(u_rect, n, rect);
  glUniform1fv(u_radius, n, radius);
  glUniform1i(u_count, n);
  float scale = fminf(sx, sy), rad = op.light * 3.14159265f / 180;
  glUniform4f(u_optics, op.refract * scale, fminf(op.frost, (float)(levels - 2 > 0 ? levels - 2 : 0) + 0.999f), op.disp, op.bevel * scale);
  glUniform4f(u_look, op.sat, op.bright, op.tint, 2 * op.curve); /* curve 1: exponent 2, a circular edge */
  glUniform4f(u_light, cosf(rad), sinf(rad), op.spec, 0);
  glUniform3fv(u_tint, 1, E.sys.theme.bg);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
}

void lw_lens_draw(struct lw_output *o) { draw(slot_for(o->name, false), 0, o->bw, o->bh, o->lw, o->lh); }

/* For `thumbnail --glass`: preview the glass offscreen, in a w x h frame. */
void lw_lens_draw_offscreen(GLuint fbo, int w, int h) { draw(slot_for("offscreen", false), fbo, w, h, w, h); }
