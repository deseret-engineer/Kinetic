#include "lw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* One oversized triangle from gl_VertexID: no vertex buffers at all. */
const char *lw_vs_fullscreen =
    "#version 300 es\n"
    "void main(){ vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";

static GLuint compile(GLenum type, const char *src, char *err, size_t errlen) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    if (err) glGetShaderInfoLog(s, (GLsizei)errlen, NULL, err);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

GLuint lw_gl_program(const char *vs, const char *fs, char *err, size_t errlen) {
  GLuint v = compile(GL_VERTEX_SHADER, vs, err, errlen);
  if (!v) return 0;
  GLuint f = compile(GL_FRAGMENT_SHADER, fs, err, errlen);
  if (!f) {
    glDeleteShader(v);
    return 0;
  }
  GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    if (err) glGetProgramInfoLog(p, (GLsizei)errlen, NULL, err);
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

GLuint lw_gl_texture_rgba(const uint8_t *rgba, int w, int h, bool mipmap, bool repeat) {
  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
  if (mipmap) glGenerateMipmap(GL_TEXTURE_2D);
  return tex;
}

/* Halve an RGBA image with a 2x2 box filter, in place. */
static void halve(uint8_t *px, int *w, int *h) {
  int nw = *w / 2, nh = *h / 2;
  for (int y = 0; y < nh; y++) {
    const uint8_t *a = px + (size_t)(y * 2) * *w * 4, *b = a + (size_t)*w * 4;
    uint8_t *d = px + (size_t)y * nw * 4;
    for (int x = 0; x < nw; x++)
      for (int c = 0; c < 4; c++)
        d[x * 4 + c] = (a[x * 8 + c] + a[x * 8 + 4 + c] + b[x * 8 + c] + b[x * 8 + 4 + c] + 2) / 4;
  }
  *w = nw;
  *h = nh;
}

GLuint lw_gl_texture_file(const char *path, int *w, int *h, bool repeat, int max_dim) {
  int iw, ih;
  uint8_t *px = lw_image_load(path, &iw, &ih);
  if (!px) return 0;
  while (max_dim > 0 && (iw > max_dim * 2 || ih > max_dim * 2) && iw > 2 && ih > 2) halve(px, &iw, &ih);
  /* GL wants the bottom row first */
  size_t stride = (size_t)iw * 4;
  uint8_t *tmp = malloc(stride);
  for (int y = 0; y < ih / 2; y++) {
    uint8_t *a = px + y * stride, *b = px + (ih - 1 - y) * stride;
    memcpy(tmp, a, stride);
    memcpy(a, b, stride);
    memcpy(b, tmp, stride);
  }
  free(tmp);
  GLuint tex = lw_gl_texture_rgba(px, iw, ih, true, repeat);
  free(px);
  if (w) *w = iw;
  if (h) *h = ih;
  return tex;
}

static GLuint blit_prog;
static GLint blit_alpha, blit_xf;

void lw_gl_blit(GLuint tex, float alpha, bool flip_y, bool cover, float tex_aspect, float dst_aspect) {
  if (!blit_prog) {
    const char *fs =
        "#version 300 es\nprecision mediump float;\n"
        "uniform sampler2D t; uniform float a; uniform vec4 xf; out vec4 o;\n"
        "void main(){ vec2 uv = gl_FragCoord.xy * xf.xy; uv = (uv - 0.5) * xf.zw + 0.5;\n"
        "  o = vec4(texture(t, uv).rgb, a); }\n";
    blit_prog = lw_gl_program(lw_vs_fullscreen, fs, NULL, 0);
    blit_alpha = glGetUniformLocation(blit_prog, "a");
    blit_xf = glGetUniformLocation(blit_prog, "xf");
  }
  GLint vp[4];
  glGetIntegerv(GL_VIEWPORT, vp);
  float sx = 1, sy = flip_y ? -1 : 1;
  if (cover && tex_aspect > 0 && dst_aspect > 0) {
    if (tex_aspect > dst_aspect)
      sx *= dst_aspect / tex_aspect;
    else
      sy *= tex_aspect / dst_aspect;
  }
  glUseProgram(blit_prog);
  glUniform1f(blit_alpha, alpha);
  glUniform4f(blit_xf, 1.0f / vp[2], 1.0f / vp[3], sx, sy);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  if (alpha < 1.0f) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDisable(GL_BLEND);
}

void lw_gl_dim(float amount) {
  static GLuint black;
  if (!black) {
    uint8_t px[4] = {0, 0, 0, 255};
    black = lw_gl_texture_rgba(px, 1, 1, false, false);
  }
  lw_gl_blit(black, amount, false, false, 0, 0);
}

bool lw_gl_has_ext(const char *name) {
  GLint n = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &n);
  for (GLint i = 0; i < n; i++) {
    const char *e = (const char *)glGetStringi(GL_EXTENSIONS, i);
    if (e && !strcmp(e, name)) return true;
  }
  return false;
}

bool lw_background_is_video(void) {
  char link[LW_PATH], real[LW_PATH];
  lw_dir_omarchy(link, "background");
  if (!realpath(link, real)) return false;
  const char *dot = strrchr(real, '.');
  if (!dot) return false;
  static const char *video[] = {".mp4", ".webm", ".mkv", ".mov", ".avi", ".m4v", ".gif", NULL};
  for (int i = 0; video[i]; i++)
    if (!strcasecmp(dot, video[i])) return true;
  return false;
}

void lw_background_refresh(struct lw_sys *sys) {
  char link[LW_PATH];
  lw_dir_omarchy(link, "background");
  if (sys->bg_tex) glDeleteTextures(1, &sys->bg_tex);
  sys->bg_tex = lw_gl_texture_file(link, &sys->bg_w, &sys->bg_h, false, 2560);
  if (!sys->bg_tex) { /* a video or nothing at all: fall back to the theme's background color */
    uint8_t px[4] = {(uint8_t)(sys->theme_to.bg[0] * 255), (uint8_t)(sys->theme_to.bg[1] * 255),
                     (uint8_t)(sys->theme_to.bg[2] * 255), 255};
    sys->bg_tex = lw_gl_texture_rgba(px, 1, 1, false, false);
    sys->bg_w = sys->bg_h = 1;
  }
  sys->bg_stale = false;
}
