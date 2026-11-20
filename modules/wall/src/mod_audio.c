/* Audio module: captures whatever is playing (the default sink's monitor)
 * through PipeWire and turns it into a spectrum texture plus band levels.
 * The capture stream only exists while a visible wallpaper asks for audio. */
#include "lw.h"
#include <math.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <stdatomic.h>
#include <string.h>

#define FFT_N 2048
#define RING 8192

static struct pw_thread_loop *loop;
static struct pw_stream *stream;
static float ring[RING];
static atomic_uint wpos;
static atomic_int channels = 2;
static bool pw_ready;
static float smooth[LW_AUDIO_BINS];

static void on_process(void *data) {
  struct pw_buffer *b = pw_stream_dequeue_buffer(stream);
  if (!b) return;
  struct spa_data *d = &b->buffer->datas[0];
  int ch = atomic_load(&channels);
  if (d->data && ch > 0) {
    const float *in = SPA_PTROFF(d->data, d->chunk->offset, const float);
    uint32_t frames = d->chunk->size / (sizeof(float) * (uint32_t)ch), w = atomic_load(&wpos);
    for (uint32_t i = 0; i < frames; i++) {
      float m = 0;
      for (int c = 0; c < ch; c++) m += in[i * (uint32_t)ch + (uint32_t)c];
      ring[w++ & (RING - 1)] = m / (float)ch;
    }
    atomic_store(&wpos, w);
  }
  pw_stream_queue_buffer(stream, b);
}

static void on_param(void *data, uint32_t id, const struct spa_pod *param) {
  struct spa_audio_info_raw info;
  if (!param || id != SPA_PARAM_Format) return;
  if (spa_format_audio_raw_parse(param, &info) >= 0 && info.channels > 0) atomic_store(&channels, (int)info.channels);
}

static const struct pw_stream_events events = {
    PW_VERSION_STREAM_EVENTS, .process = on_process, .param_changed = on_param};

void lw_audio_set_active(bool on) {
  if (on == (loop != NULL)) return;
  if (!on) {
    pw_thread_loop_stop(loop);
    pw_stream_destroy(stream);
    pw_thread_loop_destroy(loop);
    loop = NULL;
    stream = NULL;
    E.sys.audio_running = false;
    memset(E.sys.audio, 0, sizeof E.sys.audio);
    lw_dbg("audio: capture stopped");
    return;
  }
  if (!pw_ready) {
    pw_init(NULL, NULL);
    pw_ready = true;
  }
  loop = pw_thread_loop_new("kinetic-wall-audio", NULL);
  if (!loop) return;
  struct pw_properties *props =
      pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Music",
                        PW_KEY_STREAM_CAPTURE_SINK, "true", PW_KEY_NODE_NAME, "kinetic-wall", PW_KEY_NODE_DESCRIPTION,
                        "Kinetic wallpaper visualizer", PW_KEY_NODE_LATENCY, "1024/48000", NULL);
  stream = pw_stream_new_simple(pw_thread_loop_get_loop(loop), "kinetic-wall", props, &events, NULL);
  uint8_t buf[512];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
  const struct spa_pod *params[1] = {spa_format_audio_raw_build(
      &b, SPA_PARAM_EnumFormat, &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32, .rate = 48000, .channels = 2))};
  if (!stream ||
      pw_stream_connect(stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS, params, 1) < 0) {
    lw_log("audio: cannot open a PipeWire capture stream");
    if (stream) pw_stream_destroy(stream);
    pw_thread_loop_destroy(loop);
    loop = NULL;
    stream = NULL;
    return;
  }
  pw_thread_loop_start(loop);
  E.sys.audio_running = true;
  lw_dbg("audio: capture started");
}

static void fft(float *re, float *im) {
  static float cs[FFT_N / 2], sn[FFT_N / 2];
  static bool init;
  if (!init) {
    for (int i = 0; i < FFT_N / 2; i++) {
      cs[i] = cosf(-2 * (float)M_PI * i / FFT_N);
      sn[i] = sinf(-2 * (float)M_PI * i / FFT_N);
    }
    init = true;
  }
  for (int i = 1, j = 0; i < FFT_N; i++) {
    int bit = FFT_N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (int len = 2; len <= FFT_N; len <<= 1) {
    int step = FFT_N / len;
    for (int i = 0; i < FFT_N; i += len)
      for (int k = 0; k < len / 2; k++) {
        float wr = cs[k * step], wi = sn[k * step];
        int a = i + k, b2 = a + len / 2;
        float xr = re[b2] * wr - im[b2] * wi, xi = re[b2] * wi + im[b2] * wr;
        re[b2] = re[a] - xr; im[b2] = im[a] - xi;
        re[a] += xr; im[a] += xi;
      }
  }
}

static float band(const uint8_t *spec, int lo, int hi) {
  float s = 0;
  for (int i = lo; i < hi; i++) s += spec[i];
  return s / (255.0f * (float)(hi - lo));
}

void lw_audio_update(struct lw_sys *sys) {
  static float re[FFT_N], im[FFT_N], hann[FFT_N];
  static bool init;
  if (!loop) return;
  if (!init) {
    for (int i = 0; i < FFT_N; i++) hann[i] = 0.5f - 0.5f * cosf(2 * (float)M_PI * i / (FFT_N - 1));
    init = true;
  }
  uint32_t w = atomic_load(&wpos);
  float rms = 0;
  uint8_t tex[LW_AUDIO_BINS * 2];
  for (int i = 0; i < FFT_N; i++) {
    float s = ring[(w - FFT_N + (uint32_t)i) & (RING - 1)] * E.cfg.audio_gain;
    re[i] = s * hann[i];
    im[i] = 0;
    rms += s * s;
    if (i >= FFT_N - LW_AUDIO_BINS) tex[LW_AUDIO_BINS + i - (FFT_N - LW_AUDIO_BINS)] = (uint8_t)lw_clampf(128 + s * 127, 0, 255);
  }
  fft(re, im);
  for (int i = 0; i < LW_AUDIO_BINS; i++) {
    float mag = sqrtf(re[i] * re[i] + im[i] * im[i]) / (FFT_N / 2);
    smooth[i] = smooth[i] * 0.75f + mag * 0.25f;
    float db = 20 * log10f(smooth[i] + 1e-9f);
    tex[i] = (uint8_t)(lw_clampf((db + 90) / 70, 0, 1) * 255); /* -90 dB .. -20 dB, like an analyzer node */
  }
  /* 23.4 Hz per bin */
  sys->audio[0] = band(tex, 1, 11);
  sys->audio[1] = band(tex, 11, 86);
  sys->audio[2] = band(tex, 86, 400);
  sys->audio[3] = lw_clampf(sqrtf(rms / FFT_N) * 4, 0, 1);

  if (!sys->audio_tex) {
    glGenTextures(1, &sys->audio_tex);
    glBindTexture(GL_TEXTURE_2D, sys->audio_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, LW_AUDIO_BINS, 2, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glBindTexture(GL_TEXTURE_2D, sys->audio_tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, LW_AUDIO_BINS, 2, GL_RED, GL_UNSIGNED_BYTE, tex);
}

static bool start(void) { return true; }
static void stop(void) { lw_audio_set_active(false); }
struct lw_module lw_mod_audio = {"audio", start, stop, false};
