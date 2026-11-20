/* Offscreen tools: thumbnails, validation and benchmarking on a surfaceless
 * EGL context, so none of them touch the running desktop. */
#include "lw.h"
#include <EGL/eglext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static EGLDisplay dpy = EGL_NO_DISPLAY;

bool lw_offscreen_init(void) {
  if (dpy != EGL_NO_DISPLAY) return true;
  dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL) || !eglBindAPI(EGL_OPENGL_ES_API)) {
    lw_log("offscreen: surfaceless EGL is unavailable");
    return false;
  }
  const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
  if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
    lw_log("offscreen: cannot create a GLES 3 context");
    return false;
  }
  struct lw_theme t;
  lw_theme_load(&t);
  E.sys.theme = E.sys.theme_to = t;
  lw_power_refresh();
  E.sys.focus_time = lw_now();
  return true;
}

static void fake_audio(double t) {
  uint8_t tex[LW_AUDIO_BINS * 2];
  for (int i = 0; i < LW_AUDIO_BINS; i++) {
    float x = i / (float)LW_AUDIO_BINS;
    float v = expf(-x * 3.2f) * (0.62f + 0.38f * sinf(i * 0.37f + (float)t * 3) * sinf(i * 0.093f - (float)t * 1.7f));
    tex[i] = (uint8_t)(lw_clampf(v + 0.08f, 0, 1) * 255);
    tex[LW_AUDIO_BINS + i] = (uint8_t)(128 + 90 * sinf(i * 0.11f + (float)t * 9) * sinf(i * 0.023f));
  }
  if (!E.sys.audio_tex) {
    glGenTextures(1, &E.sys.audio_tex);
    glBindTexture(GL_TEXTURE_2D, E.sys.audio_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, LW_AUDIO_BINS, 2, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glBindTexture(GL_TEXTURE_2D, E.sys.audio_tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, LW_AUDIO_BINS, 2, GL_RED, GL_UNSIGNED_BYTE, tex);
  E.sys.audio[0] = 0.62f + 0.25f * sinf((float)t * 5);
  E.sys.audio[1] = 0.5f, E.sys.audio[2] = 0.35f, E.sys.audio[3] = 0.5f;
}

static GLuint make_target(int w, int h, GLuint *tex) {
  GLuint fbo;
  glGenTextures(1, tex);
  glBindTexture(GL_TEXTURE_2D, *tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
  return fbo;
}

float lw_thumb_parallax[2];

static struct lw_view default_view(void) {
  struct lw_view v = {0};
  v.parallax[0] = lw_thumb_parallax[0];
  v.parallax[1] = lw_thumb_parallax[1];
  v.mouse[0] = v.mouse[1] = 0.5f;
  v.mouse[2] = v.mouse[3] = -0.5f;
  v.pointer[0] = v.pointer[1] = 0.5f;
  v.pointer[3] = 1e4f;
  v.workspace[0] = v.workspace[2] = 1;
  v.workspace[1] = 1e4f;
  return v;
}

int lw_cmd_validate(const char *id) {
  if (!lw_offscreen_init()) return 2;
  char err[2048] = "";
  struct lw_wallpaper *wp = lw_wallpaper_load(id, err, sizeof err);
  if (!wp) {
    fprintf(stderr, "%s: FAILED\n%s\n", id, err);
    return 1;
  }
  GLuint tex, fbo = make_target(64, 64, &tex);
  struct lw_view v = default_view();
  lw_wallpaper_render(wp, fbo, 64, 64, 64, 64, &v, &E.sys);
  GLenum glerr = glGetError();
  struct lw_wallpaper_info *info = lw_wallpaper_info(wp);
  printf("%s: ok (%d buffer%s, %d param%s%s%s%s)%s\n", info->id, info->nbuffers, info->nbuffers == 1 ? "" : "s",
         info->nparams, info->nparams == 1 ? "" : "s", info->audio ? ", audio" : "", info->pointer ? ", pointer" : "",
         info->background ? ", uses background" : "", glerr ? " [GL error raised]" : "");
  lw_wallpaper_free(wp);
  return glerr ? 1 : 0;
}

int lw_cmd_thumbnail(const char *id, const char *out, int w, int h, double t) {
  if (!lw_offscreen_init()) return 2;
  char err[2048] = "";
  struct lw_wallpaper *wp = lw_wallpaper_load(id, err, sizeof err);
  if (!wp) {
    fprintf(stderr, "%s: %s\n", id, err);
    return 1;
  }
  struct lw_wallpaper_info *info = lw_wallpaper_info(wp);
  if (t < 0) t = info->preview_time;
  GLuint tex, fbo = make_target(w, h, &tex);
  struct lw_view v = default_view();
  /* feedback wallpapers need history: simulate the last few seconds */
  int steps = info->nbuffers ? (int)(info->preview_warmup * 60) + 1 : 1;
  for (int i = steps - 1; i >= 0; i--) {
    v.time = t - i / 60.0;
    if (v.time < 0) v.time = 0;
    v.dt = 1 / 60.0;
    if (info->audio) fake_audio(v.time);
    lw_wallpaper_render(wp, fbo, w, h, w, h, &v, &E.sys);
    if (lw_wallpaper_take_reset(wp)) lw_wallpaper_restart(wp);
    if (i % 30 == 0) glFinish(); /* stay in step with the GPU, like a paced display would */
  }
  lw_lens_draw_offscreen(fbo, w, h);
  uint8_t *px = malloc((size_t)w * h * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
  bool ok = lw_image_save_png(out, px, w, h, true);
  free(px);
  lw_wallpaper_free(wp);
  if (!ok) fprintf(stderr, "cannot write %s\n", out);
  return ok ? 0 : 1;
}

int lw_cmd_bench(const char *id, int w, int h, double seconds, float scale) {
  if (!lw_offscreen_init()) return 2;
  char err[2048] = "";
  struct lw_wallpaper *wp = lw_wallpaper_load(id, err, sizeof err);
  if (!wp) {
    fprintf(stderr, "%s: %s\n", id, err);
    return 1;
  }
  struct lw_wallpaper_info *info = lw_wallpaper_info(wp);
  float q = scale > 0 ? scale : info->scale;
  int rw = (int)fmax(1, round(w * q)), rh = (int)fmax(1, round(h * q));
  GLuint tex, fbo = make_target(rw, rh, &tex);
  struct lw_view v = default_view();
  uint8_t px[4];
  for (int i = 0; i < 10; i++) { /* warm up: shader variants, buffer allocation */
    v.time += 1 / 60.0;
    lw_wallpaper_render(wp, fbo, rw, rh, rw, rh, &v, &E.sys);
  }
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  double t0 = lw_now(), now;
  int frames = 0;
  do {
    v.time += 1 / 60.0;
    v.dt = 1 / 60.0;
    if (info->audio) fake_audio(v.time);
    lw_wallpaper_render(wp, fbo, rw, rh, rw, rh, &v, &E.sys);
    if (++frames % 8 == 0) glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px); /* force the GPU to finish */
    now = lw_now();
  } while (now - t0 < seconds);
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  double ms = (lw_now() - t0) * 1000 / frames;
  float fps = info->fps > 0 ? info->fps : 60;
  printf("%-14s %4dx%-4d (scale %.2f)  %6.3f ms/frame  %5.2f%% GPU duty at %g fps\n", info->id, rw, rh, q, ms,
         ms * fps / 10.0, fps);
  lw_wallpaper_free(wp);
  return 0;
}
