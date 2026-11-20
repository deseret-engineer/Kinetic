// Kinetic's window glass and selection light, drawn inside Hyprland.
//
// The wallpaper engine used to draw both into the wallpaper, guessing where
// each window was from Hyprland's animation curves and redrawing the whole
// wallpaper at display rate while windows moved. Here they are drawn in
// Hyprland's own frame, from the window's real position on that frame, so they
// cannot lag or drift, and only the pixels behind each window are touched.
//
// How a frame goes, per monitor:
//   - preChecks: the light's fades advance, and the light's area outside each
//     window is damaged when it moves or changes (the window's own area is
//     damaged by Hyprland). Damage that touches a covered window is widened to
//     all of it, so everything behind it is drawn fresh this frame.
//   - after the background layer (RENDER_POST_WALLPAPER): one pass element
//     draws the lights into the frame, screen-blended, under every window.
//   - each covered window: a decoration under the window (DECORATION_LAYER_
//     BOTTOM) copies what is behind it out of the frame (wallpaper, lights and
//     any windows below) and draws its glass from that: refraction toward the
//     rim, frost (mipmaps, B-spline), dispersion, saturation, brightness, tint,
//     rim light. The window is drawn over it, as Hyprland's blur would be.
//
// Kinetic drives it with `hyprctl kinetic-glass set key=value ...` (the
// engine sends its Glass and Light options whenever they or the theme change,
// and when this plugin announces itself with the `kinetic-glass>>ready` event).
// `hyprctl kinetic-glass status` says what it is doing.
//
// Built by modules/glass/plugin/build.sh against the running Hyprland's headers;
// Hyprland refuses it after an update until it is rebuilt (the hash check below).

#define WLR_USE_UNSTABLE

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/EventManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include <GLES3/gl32.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Render::GL;

static HANDLE PHANDLE = nullptr;
static const char* NAME = "Kinetic glass";

// ---------------------------------------------------------------- settings

struct SSettings {
    // glass
    std::string glass = "off"; // off | see-through | floating | all
    std::vector<std::string> apps = {"terminal"};
    float refract = 18, bevel = 36, curve = 1, frost = 2, disp = 0.25f;
    float sat = 1.1f, bright = 0, tint = 0.15f, spec = 0.3f, angle = 120;
    float tintRGB[3] = {0.1f, 0.1f, 0.1f};
    // light
    std::string light = "off"; // off | focused | all
    std::string anim  = "none"; // none | breathe | flow | shimmer
    float color[3] = {1, 1, 1}, urgentRGB[3] = {1, 0.3f, 0.3f}, pal[18] = {};
    bool  rainbow = false, urgent = true;
    float strength = 0.45f, unfocused = 0.3f, outside = 26, inside = 80, edge = 1.5f, softness = 1;
    float speed = 1, sound = 0, fade = 260;
};
static SSettings S;

static bool glassOn() {
    return S.glass != "off";
}
static bool lightOn() {
    return S.light != "off";
}

static void rgb(const std::string& v, float* out, int n = 1) {
    std::stringstream ss(v);
    std::string       part;
    for (int i = 0; i < 3 * n && std::getline(ss, part, i % 3 == 2 ? ';' : ','); i++) {
        try {
            out[i] = std::clamp(std::stof(part), 0.f, 1.f);
        } catch (...) {}
    }
}

static std::string applySetting(const std::string& key, const std::string& v) {
    auto num = [&](float& f, float lo, float hi) -> std::string {
        try {
            f = std::clamp(std::stof(v), lo, hi);
        } catch (...) { return key + ": not a number"; }
        return "";
    };
    auto pick = [&](std::string& s, std::initializer_list<const char*> ok) -> std::string {
        for (auto o : ok)
            if (v == o) {
                s = v;
                return "";
            }
        return key + ": unknown value " + v;
    };
    if (key == "glass") return pick(S.glass, {"off", "see-through", "floating", "all"});
    if (key == "apps") {
        S.apps.clear();
        std::stringstream ss(v);
        std::string       a;
        while (std::getline(ss, a, ','))
            if (!a.empty()) S.apps.push_back(a);
        return "";
    }
    if (key == "refract") return num(S.refract, 0, 200);
    if (key == "bevel") return num(S.bevel, 0, 400);
    if (key == "curve") return num(S.curve, 0.1f, 8);
    if (key == "frost") return num(S.frost, 0, 8);
    if (key == "disp") return num(S.disp, 0, 1);
    if (key == "sat") return num(S.sat, 0, 3);
    if (key == "bright") return num(S.bright, -1, 1);
    if (key == "tint") return num(S.tint, 0, 1);
    if (key == "spec") return num(S.spec, 0, 2);
    if (key == "angle") return num(S.angle, -360, 720);
    if (key == "tintrgb") return rgb(v, S.tintRGB), "";
    if (key == "light") return pick(S.light, {"off", "focused", "all"});
    if (key == "anim") return pick(S.anim, {"none", "breathe", "flow", "shimmer"});
    if (key == "color") return rgb(v, S.color), "";
    if (key == "urgentrgb") return rgb(v, S.urgentRGB), "";
    if (key == "pal") return rgb(v, S.pal, 6), "";
    if (key == "rainbow") return S.rainbow = v == "1" || v == "true", "";
    if (key == "urgent") return S.urgent = v == "1" || v == "true", "";
    if (key == "strength") return num(S.strength, 0, 3);
    if (key == "unfocused") return num(S.unfocused, 0, 1);
    if (key == "outside") return num(S.outside, 0, 400);
    if (key == "inside") return num(S.inside, 0, 800);
    if (key == "edge") return num(S.edge, 0, 20);
    if (key == "softness") return num(S.softness, 0.1f, 5);
    if (key == "speed") return num(S.speed, 0, 10);
    if (key == "sound") return num(S.sound, 0, 4);
    if (key == "fade") return num(S.fade, 0, 5000);
    return key + ": unknown setting";
}

// ---------------------------------------------------------------- per-monitor state

// What lies behind the window being drawn, copied out of the frame (with its
// mipmaps for the frost). One, reused by every window in turn.
struct SScratch {
    GLuint tex = 0, fbo = 0;
    int    w = 0, h = 0, levels = 1;
};
static SScratch g_scratch;

struct SLit {
    PHLWINDOWREF window;
    float        alpha = 0;     // eased toward its target as focus moves
    CBox         lastBox;       // last damaged light area, global logical px
    float        lastWeight = -1;
};
static std::vector<SLit> g_lit;
static Time::steady_tp   g_lastStep = Time::steadyNow();
static Time::steady_tp   g_start    = Time::steadyNow();
static SP<CEventLoopTimer> g_tick;

// sound level written by the engine while the light reacts to sound
static float* g_level = nullptr;

static float soundGain() {
    if (S.sound <= 0) return 0;
    if (!g_level) {
        const char* rt = getenv("XDG_RUNTIME_DIR");
        if (!rt) return 0;
        int fd = open(std::format("{}/kinetic/light-level", rt).c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return 0;
        void* p = mmap(nullptr, 8, PROT_READ, MAP_SHARED, fd, 0);
        close(fd);
        if (p == MAP_FAILED) return 0;
        g_level = (float*)p;
    }
    float e = g_level[0], k = g_level[1];
    if (!std::isfinite(e) || !std::isfinite(k)) return 0;
    return S.sound * std::clamp(e + k * 0.5f, 0.f, 4.f);
}

// ---------------------------------------------------------------- which windows

static bool matchesApp(PHLWINDOW w) {
    for (auto const& a : S.apps) {
        if (a.ends_with('*')) {
            auto p = a.substr(0, a.size() - 1);
            if (w->m_class.starts_with(p) || w->m_initialClass.starts_with(p)) return true;
        } else if (w->m_class == a || w->m_initialClass == a)
            return true;
        if (w->m_ruleApplicator && w->m_ruleApplicator->m_tagKeeper.isTagged(a)) return true;
    }
    return false;
}

static bool coversWindow(PHLWINDOW w) {
    if (!glassOn() || !validMapped(w) || w->isHidden()) return false;
    if (S.glass == "all") return true;
    if (S.glass == "floating") return w->m_isFloating;
    return w->alphaValue(Desktop::View::WINDOW_ALPHA_ACTIVE) <= 0.951f || matchesApp(w);
}

// Where Hyprland draws the window this frame: monitor-local logical px.
static CBox windowBoxLocal(PHLWINDOW w, PHLMONITOR m) {
    auto       pos = w->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    auto       sz  = w->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const auto ws  = w->m_workspace;
    if (ws && !w->m_pinned) pos = pos + ws->m_renderOffset->value();
    pos = pos + w->m_floatingOffset;
    return CBox{pos.x - m->m_position.x, pos.y - m->m_position.y, sz.x, sz.y};
}

// The same box in the frame's pixels (y down, the monitor's transform applied),
// as Hyprland's scissor() places boxes.
static CBox toFB(CBox b, PHLMONITOR m) {
    b.scale(m->m_scale);
    const auto TR = Math::wlTransformToHyprutils(Math::invertTransform(m->m_transform));
    b.transform(TR, m->m_transformedSize.x, m->m_transformedSize.y);
    return b;
}

// ---------------------------------------------------------------- GL

static SP<CShader> g_glassShader, g_lightShader;
static GLint       gu[16], lu[16];
static bool        g_glFailed = false;

static const char* VERT = R"(#version 300 es
precision highp float;
uniform vec4 uQuad;   // x, y, w, h in frame px
uniform vec2 uFB;     // frame size
in vec2 pos;
void main() {
  vec2 p = uQuad.xy + pos * uQuad.zw;
  gl_Position = vec4(p / uFB * 2.0 - 1.0, 0.0, 1.0);
}
)";

static const char* GLASS_FRAG = R"(#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec2 uTexSize;
uniform vec2 uOrigin;  // where the copy starts in the frame
uniform vec4 uRect;    // center x, y and half size, frame px
uniform float uRadius;
uniform float uPower;  // Hyprland's rounding_power: the corners' curve
uniform float uAlpha;
uniform vec4 uOptics;  // displacement px, frost lod, dispersion, bevel px (0 = half the thinner side)
uniform vec4 uLook;    // saturation, brightness, tint amount, edge profile exponent
uniform vec4 uLight;   // light direction xy (frame px, y down), rim light strength
uniform vec3 uTint;
out vec4 frag;
// A rounded box whose corners follow |x|^k + |y|^k = r^k, as Hyprland rounds windows.
float sdp(vec2 p, vec2 b, float r, float k) {
  vec2 q = abs(p) - b + r, m = max(q, 0.0);
  float n = m.x > 0.0 || m.y > 0.0 ? pow(pow(m.x, k) + pow(m.y, k), 1.0 / k) : 0.0;
  return n + min(max(q.x, q.y), 0.0) - r; }
// Frost: mip levels read through a cubic B-spline (four bilinear taps), smooth
// and close to a Gaussian; a fractional frost blends two levels.
vec3 bspline(vec2 uv, int lod) {
  vec2 size = vec2(textureSize(uTex, lod)), p = uv * size - 0.5, i = floor(p), f = p - i;
  vec2 w0 = (1.0 - f) * (1.0 - f) * (1.0 - f) / 6.0, w1 = (3.0 * f * f * f - 6.0 * f * f + 4.0) / 6.0;
  vec2 w3 = f * f * f / 6.0, w2 = 1.0 - w0 - w1 - w3, g0 = w0 + w1, g1 = w2 + w3;
  vec2 h0 = (i - 0.5 + w1 / g0) / size, h1 = (i + 1.5 + w3 / g1) / size;
  float l = float(lod);
  return g0.y * (g0.x * textureLod(uTex, h0, l).rgb + g1.x * textureLod(uTex, vec2(h1.x, h0.y), l).rgb)
       + g1.y * (g0.x * textureLod(uTex, vec2(h0.x, h1.y), l).rgb + g1.x * textureLod(uTex, h1, l).rgb); }
vec3 fetch(vec2 q) {
  vec2 uv = clamp((q - uOrigin) / uTexSize, vec2(0.0), vec2(1.0));
  if (uOptics.y <= 0.0) return textureLod(uTex, uv, 0.0).rgb;
  int l0 = int(floor(uOptics.y)); float t = uOptics.y - float(l0);
  vec3 a = bspline(uv, l0);
  return t > 0.01 ? mix(a, bspline(uv, l0 + 1), t) : a; }
void main() {
  vec2 p = gl_FragCoord.xy, c = uRect.xy, b = uRect.zw;
  // coverage: Hyprland's own corner test and edge (rounding.glsl), so the glass
  // ends exactly where the window does; straight edges are the quad's own
  vec2 pc = abs(p - c) - (b - uRadius);
  float cover = uAlpha;
  if (uRadius > 0.0 && pc.x + pc.y > uRadius) {
    const float S = 3.1415926535897932 / 5.34665792551;
    float dist = pow(pow(max(pc.x, 0.0), uPower) + pow(max(pc.y, 0.0), uPower), 1.0 / uPower);
    cover *= 1.0 - smoothstep(0.0, 1.0, (dist - uRadius + S) / (S * 2.0));
  }
  if (cover <= 0.0) discard;
  // the bend: a field with corners at least as round as the bevel is deep, so
  // its slope never creases where the glass bends (no lines from the corners)
  float band = max(1.0, uOptics.w > 0.0 ? min(uOptics.w, min(b.x, b.y)) : min(b.x, b.y));
  float R = min(max(uRadius, band), min(b.x, b.y));
  float d = sdp(p - c, b, R, 2.0);
  vec2 e = vec2(0.75, 0.0);
  vec2 g = vec2(sdp(p + e.xy - c, b, R, 2.0) - sdp(p - e.xy - c, b, R, 2.0), sdp(p + e.yx - c, b, R, 2.0) - sdp(p - e.yx - c, b, R, 2.0));
  vec2 n = dot(g, g) > 1e-8 ? normalize(g) : vec2(0.0);
  float t = clamp(-d / band, 0.0, 1.0), u = 1.0 - t, k = uLook.w;
  float bend = 1.0 - pow(max(0.0, 1.0 - pow(u, k)), 1.0 / k);
  vec2 off = -n * uOptics.x * bend;
  vec3 col;
  if (uOptics.z > 0.0 && dot(off, off) > 0.25) {
    float s = uOptics.z * 0.5;
    col = vec3(fetch(p + off * (1.0 - s)).r, fetch(p + off).g, fetch(p + off * (1.0 + s)).b);
  } else col = fetch(p + off);
  col = mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, uLook.x) + uLook.y;
  col = mix(col, uTint, uLook.z);
  float rim = pow(u, 3.0) * max(0.0, dot(n, uLight.xy));
  col += uLight.z * rim * 0.6;
  frag = vec4(clamp(col, 0.0, 1.0) * cover, cover);
}
)";

static const char* LIGHT_FRAG = R"(#version 300 es
precision highp float;
uniform vec4 uRect;    // center and half size, frame px
uniform float uRadius;
uniform float uPower;
uniform float uWeight; // strength x fade
uniform float uUrgent;
uniform float uIndex;
uniform vec4 uShape;   // outside px, inside px, edge px, softness
uniform vec4 uAnim;    // animation (0 none, 1 breathe, 2 flow, 3 shimmer), time, speed, rainbow
uniform vec3 uColor;
uniform vec3 uPal[6];
uniform vec4 uFx;      // sound gain, urgent color
out vec4 frag;
float sdp(vec2 p, vec2 b, float r, float k) {
  vec2 q = abs(p) - b + r, m = max(q, 0.0);
  float n = m.x > 0.0 || m.y > 0.0 ? pow(pow(m.x, k) + pow(m.y, k), 1.0 / k) : 0.0;
  return n + min(max(q.x, q.y), 0.0) - r; }
vec3 pal(float t) { t = fract(t) * 6.0; int i = int(t); return mix(uPal[i], uPal[(i + 1) % 6], smoothstep(0.0, 1.0, fract(t))); }
void main() {
  vec2 p = gl_FragCoord.xy, c = uRect.xy;
  float t = uAnim.y * uAnim.z;
  float d = sdp(p - c, uRect.zw, uRadius, uPower);
  float k = d > 0.0 ? (uShape.x > 0.0 ? exp(-pow(d / uShape.x, uShape.w) * 3.0) : 0.0)
                    : (uShape.y > 0.0 ? exp(-pow(-d / uShape.y, uShape.w) * 3.0) : 0.0);
  if (uShape.z > 0.0) k += 1.5 * smoothstep(uShape.z, 0.0, abs(d));
  if (k < 0.002) discard;
  float a = atan(c.y - p.y, p.x - c.x) / 6.2831853;
  float w = uWeight * (1.0 + uFx.x);
  if (uAnim.x == 1.0) w *= 0.7 + 0.3 * sin(t * 1.6 + uIndex);
  if (uAnim.x == 3.0) w *= 0.45 + 1.4 * pow(max(0.0, cos((a - t * 0.25) * 6.2831853)), 12.0);
  vec3 col = uAnim.w > 0.5 ? pal(a + (uAnim.x == 2.0 ? t * 0.1 : 0.0)) : uColor;
  if (uAnim.x == 2.0 && uAnim.w < 0.5) col *= 0.55 + 0.45 * cos((a * 2.0 - t * 0.2) * 6.2831853);
  if (uUrgent > 0.5) { col = uFx.yzw; w = max(w, 0.5) * (0.6 + 0.4 * sin(uAnim.y * 6.0)); }
  vec3 l = 1.0 - exp(-col * k * w);
  frag = vec4(l, max(l.r, max(l.g, l.b)));
}
)";

static bool ensureShaders() {
    if (g_glFailed) return false;
    if (g_glassShader && g_lightShader) return true;
    g_glassShader = makeShared<CShader>();
    g_lightShader = makeShared<CShader>();
    if (!g_glassShader->createProgram(VERT, GLASS_FRAG, true, false) || !g_lightShader->createProgram(VERT, LIGHT_FRAG, true, false)) {
        g_glFailed = true;
        HyprlandAPI::addNotification(PHANDLE, "[kinetic-glass] its shaders did not compile; glass and light are off (see Hyprland's log)", CHyprColor{1.0, 0.3, 0.3, 1.0}, 8000);
        return false;
    }
    const char* gn[] = {"uQuad", "uFB", "uTex", "uTexSize", "uRect", "uRadius", "uAlpha", "uOptics", "uLook", "uLight", "uTint", "uOrigin", "uPower"};
    for (int i = 0; i < 13; i++)
        gu[i] = glGetUniformLocation(g_glassShader->program(), gn[i]);
    const char* ln[] = {"uQuad", "uFB", "uRect", "uRadius", "uWeight", "uUrgent", "uIndex", "uShape", "uAnim", "uColor", "uPal", "uFx", "uPower"};
    for (int i = 0; i < 13; i++)
        lu[i] = glGetUniformLocation(g_lightShader->program(), ln[i]);
    return true;
}

static void drawQuad(SP<CShader> sh) {
    glBindVertexArray(sh->getUniformLocation(SHADER_SHADER_VAO));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

static void freeScratch() {
    if (g_scratch.fbo) glDeleteFramebuffers(1, &g_scratch.fbo);
    if (g_scratch.tex) glDeleteTextures(1, &g_scratch.tex);
    g_scratch = SScratch{};
}

// Sized exactly to what is copied, so the mip chain holds nothing else: a
// window that only moves keeps it; one that is resizing remakes it per frame.
static bool ensureScratch(int w, int h) {
    if (g_scratch.tex && g_scratch.w == w && g_scratch.h == h) return true;
    freeScratch();
    g_scratch.w      = w;
    g_scratch.h      = h;
    g_scratch.levels = std::min(8, 1 + (int)std::floor(std::log2((double)std::max(w, h))));
    glGenTextures(1, &g_scratch.tex);
    glBindTexture(GL_TEXTURE_2D, g_scratch.tex);
    glTexStorage2D(GL_TEXTURE_2D, g_scratch.levels, GL_RGBA8, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &g_scratch.fbo);
    GLint prev = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_scratch.fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_scratch.tex, 0);
    bool ok = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev);
    if (!ok) freeScratch();
    return ok;
}

// How far outside a window its glass reads (the frost's reach), frame px.
static int glassMarginPx() {
    return (int)std::ceil(std::exp2(std::min(S.frost, 7.f) + 1.f)) + 2;
}

// The glass reads what is behind the window on this frame, so all of that must
// be drawn fresh: when anything damages a covered window, damage all of it (and
// the frost's margin). Hyprland's live blur widens its damage the same way.
static void widenDamage(PHLMONITOR m) {
    if (!glassOn()) return;
    CRegion pending = m->m_damage.getBufferDamage(1);
    if (pending.empty()) return;
    for (auto const& w : Desktop::windowState()->windows()) {
        if (!coversWindow(w) || !g_pHyprRenderer->shouldRenderWindow(w, m)) continue;
        CBox px = windowBoxLocal(w, m).scale(m->m_scale).expand(glassMarginPx()).round();
        if (!pending.copy().intersect(px).empty()) m->addDamage(px);
    }
}

// ---------------------------------------------------------------- the light

static float lightTarget(PHLWINDOW w, PHLWINDOW focused) {
    if (S.urgent && w->m_isUrgent) return 1;
    if (w == focused) return 1;
    return S.light == "all" ? S.unfocused : 0;
}

static bool lightAnimated() {
    return lightOn() && (S.anim != "none" || S.sound > 0);
}

static float lightWeight(const SLit& l, PHLWINDOW w) {
    float a = l.alpha * S.strength;
    if (w->m_workspace && !w->m_pinned) a *= w->m_workspace->m_alpha->value();
    a *= w->alphaValue(Desktop::View::WINDOW_ALPHA_FADE);
    return a;
}

// The light's whole area, global logical px.
static CBox lightBox(PHLWINDOW w) {
    auto pos = w->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    auto sz  = w->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    if (w->m_workspace && !w->m_pinned) pos = pos + w->m_workspace->m_renderOffset->value();
    pos = pos + w->m_floatingOffset;
    return CBox{pos.x, pos.y, sz.x, sz.y}.expand(S.outside + S.edge + 2);
}

// Advances the fades and keeps the list in step with the windows; damages what changed.
static bool stepLights() {
    auto  now = Time::steadyNow();
    float dt  = std::chrono::duration<float>(now - g_lastStep).count();
    g_lastStep = now;
    dt         = std::clamp(dt, 0.f, 0.25f);

    std::erase_if(g_lit, [](const SLit& l) {
        if (l.window.expired() || !validMapped(l.window.lock())) {
            if (l.lastBox.w > 0) g_pHyprRenderer->damageBox(l.lastBox);
            return true;
        }
        return false;
    });
    if (!lightOn()) {
        for (auto& l : g_lit)
            if (l.lastBox.w > 0) g_pHyprRenderer->damageBox(l.lastBox);
        g_lit.clear();
        return false;
    }

    auto focused = Desktop::focusState()->window();
    for (auto const& w : Desktop::windowState()->windows()) {
        if (!validMapped(w) || w->isHidden()) continue;
        if (std::ranges::none_of(g_lit, [&](const SLit& l) { return l.window.lock() == w; })) g_lit.push_back(SLit{.window = w});
    }

    float rate   = S.fade > 0 ? 3000.f / S.fade : 1e6f;
    bool  moving = false;
    for (auto& l : g_lit) {
        auto  w = l.window.lock();
        float t = lightTarget(w, focused);
        l.alpha += (t - l.alpha) * (1 - std::exp(-dt * rate));
        if (std::fabs(t - l.alpha) < 0.004f)
            l.alpha = t;
        else
            moving = true;
        float weight = lightWeight(l, w);
        CBox  box    = weight > 0.003f || (S.urgent && w->m_isUrgent) ? lightBox(w) : CBox{};
        if (box != l.lastBox || std::fabs(weight - l.lastWeight) > 0.001f) {
            if (l.lastBox.w > 0) g_pHyprRenderer->damageBox(l.lastBox);
            if (box.w > 0) g_pHyprRenderer->damageBox(box);
            l.lastBox    = box;
            l.lastWeight = weight;
        }
        moving |= S.urgent && w->m_isUrgent;
    }
    return moving;
}

static void scheduleTick();

// An animated or fading light needs frames of its own: 30 a second is smooth for a glow.
static void onTick(SP<CEventLoopTimer> self, void*) {
    bool more = stepLights();
    if (lightAnimated())
        for (auto& l : g_lit)
            if (l.lastBox.w > 0) g_pHyprRenderer->damageBox(l.lastBox);
    if (more || lightAnimated()) self->updateTimeout(std::chrono::milliseconds(33));
}

static void scheduleTick() {
    if (!g_tick) {
        g_tick = makeShared<CEventLoopTimer>(std::chrono::milliseconds(33), onTick, nullptr);
        g_pEventLoopManager->addTimer(g_tick);
    } else if (!g_tick->armed())
        g_tick->updateTimeout(std::chrono::milliseconds(33));
}

static void drawLights(PHLMONITOR m, int fbw, int fbh) {
    if (!lightOn() || g_lit.empty()) return;
    float gain = soundGain();
    float tsec = std::chrono::duration<float>(Time::steadyNow() - g_start).count();
    tsec       = std::fmod(tsec, 3600.f);
    float anim = S.anim == "breathe" ? 1 : S.anim == "flow" ? 2 : S.anim == "shimmer" ? 3 : 0;
    float sc   = m->m_scale;

    auto sh = g_lightShader;
    g_pHyprOpenGL->useShader(sh);
    glUniform2f(lu[1], fbw, fbh);
    glUniform4f(lu[7], S.outside * sc, S.inside * sc, S.edge * sc, S.softness);
    glUniform4f(lu[8], anim, tsec, S.speed, S.rainbow ? 1.f : 0.f);
    glUniform3fv(lu[9], 1, S.color);
    glUniform3fv(lu[10], 6, S.pal);
    glUniform4f(lu[11], gain, S.urgentRGB[0], S.urgentRGB[1], S.urgentRGB[2]);
    g_pHyprOpenGL->setCapStatus(GL_BLEND, true);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR); // screen: light adds up but never clips

    const auto& damage = g_pHyprRenderer->m_renderData.damage;
    int         index  = 0;
    for (auto& l : g_lit) {
        auto w = l.window.lock();
        if (!w || !g_pHyprRenderer->shouldRenderWindow(w, m)) continue;
        float weight = lightWeight(l, w);
        bool  urg    = S.urgent && w->m_isUrgent;
        if (weight < 0.003f && !urg) continue;
        CBox win = toFB(windowBoxLocal(w, m), m);
        if (win.w < 1 || win.h < 1) continue;
        float pad  = (S.outside + S.edge + 2) * sc;
        CBox  quad = win.copy().expand(pad);
        float rad  = std::min((float)w->rounding() * sc, (float)std::min(win.w, win.h) * 0.5f);
        glUniform4f(lu[0], quad.x, quad.y, quad.w, quad.h);
        glUniform4f(lu[2], win.x + win.w / 2, win.y + win.h / 2, win.w / 2, win.h / 2);
        glUniform1f(lu[3], rad);
        glUniform1f(lu[12], std::max(1.f, w->roundingPower()));
        glUniform1f(lu[4], weight);
        glUniform1f(lu[5], urg ? 1.f : 0.f);
        glUniform1f(lu[6], (float)index++);
        // damage is in untransformed monitor px; scissor() transforms each rect
        for (auto const& r : damage.copy().getRects()) {
            g_pHyprOpenGL->scissor(&r);
            drawQuad(sh);
        }
    }
    g_pHyprOpenGL->blend(true); // back to premultiplied
}

// ---------------------------------------------------------------- pass elements

// After the background layer, under every window: the lights, so the glass
// bends and frosts them with the wallpaper, as it would a light behind glass.
class CLightPass : public IPassElement {
  public:
    std::vector<UP<IPassElement>> draw() override {
        auto m  = g_pHyprRenderer->m_renderData.pMonitor.lock();
        auto fb = g_pHyprRenderer->m_renderData.currentFB;
        if (!m || !fb || !ensureShaders()) return {};
        const int fbw = (int)fb->m_size.x, fbh = (int)fb->m_size.y;
        if (fbw < 1 || fbh < 1) return {};
        g_pHyprOpenGL->setViewport(0, 0, fbw, fbh);
        drawLights(m, fbw, fbh);
        return {};
    }
    bool needsLiveBlur() override {
        return false;
    }
    bool needsPrecomputeBlur() override {
        return false;
    }
    const char* passName() override {
        return "CKineticLightPass";
    }
    ePassElementType type() override {
        return EK_CUSTOM;
    }
};

class CKineticGlass;

class CGlassPass : public IPassElement {
  public:
    CGlassPass(PHLWINDOWREF w, float a) : m_window(w), m_a(a) {}
    std::vector<UP<IPassElement>> draw() override;
    bool                          needsLiveBlur() override {
        return false;
    }
    bool needsPrecomputeBlur() override {
        return false;
    }
    const char* passName() override {
        return "CKineticGlassPass";
    }
    ePassElementType type() override {
        return EK_CUSTOM;
    }
    std::optional<CBox> boundingBox() override {
        auto w = m_window.lock();
        auto m = g_pHyprRenderer->m_renderData.pMonitor.lock();
        if (!w || !m) return std::nullopt;
        return windowBoxLocal(w, m);
    }

  private:
    PHLWINDOWREF m_window;
    float        m_a = 1;
};

std::vector<UP<IPassElement>> CGlassPass::draw() {
    auto w  = m_window.lock();
    auto m  = g_pHyprRenderer->m_renderData.pMonitor.lock();
    auto fb = g_pHyprRenderer->m_renderData.currentFB;
    if (!w || !m || !fb || !ensureShaders()) return {};
    const int fbw = (int)fb->m_size.x, fbh = (int)fb->m_size.y;

    CBox win = toFB(windowBoxLocal(w, m), m);
    if (win.w < 2 || win.h < 2) return {};

    // copy what is behind the window right now (everything under it is drawn)
    const int margin = glassMarginPx();
    const int x0 = std::clamp((int)std::floor(win.x) - margin, 0, fbw), y0 = std::clamp((int)std::floor(win.y) - margin, 0, fbh);
    const int x1 = std::clamp((int)std::ceil(win.x + win.w) + margin, 0, fbw), y1 = std::clamp((int)std::ceil(win.y + win.h) + margin, 0, fbh);
    if (x1 - x0 < 2 || y1 - y0 < 2 || !ensureScratch(x1 - x0, y1 - y0)) return {};
    auto& b   = g_scratch;
    GLint cur = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &cur);
    g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, cur);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, b.fbo);
    glBlitFramebuffer(x0, y0, x1, y1, 0, 0, b.w, b.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, cur);

    float frost = std::min(S.frost, (float)(b.levels - 2));
    if (frost > 0) {
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, b.tex);
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    // the window's own opacity is not the glass's: only its fades are
    float opacity = std::max(0.01f, w->alphaValue(Desktop::View::WINDOW_ALPHA_ACTIVE));
    float alpha   = std::clamp(m_a / opacity, 0.f, 1.f);
    float sc      = m->m_scale;
    float rad     = std::min((float)w->rounding() * sc, (float)std::min(win.w, win.h) * 0.5f);
    float ang     = S.angle * (float)M_PI / 180.f;

    auto sh = g_glassShader;
    g_pHyprOpenGL->useShader(sh);
    g_pHyprOpenGL->setViewport(0, 0, fbw, fbh);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, b.tex);
    glUniform1i(gu[2], 5);
    glUniform4f(gu[0], win.x, win.y, win.w, win.h);
    glUniform2f(gu[1], fbw, fbh);
    glUniform2f(gu[3], b.w, b.h);
    glUniform2f(gu[11], x0, y0);
    glUniform4f(gu[4], win.x + win.w / 2, win.y + win.h / 2, win.w / 2, win.h / 2);
    glUniform1f(gu[5], rad);
    glUniform1f(gu[12], std::max(1.f, w->roundingPower()));
    glUniform1f(gu[6], alpha);
    glUniform4f(gu[7], S.refract * sc, frost, S.disp, S.bevel * sc);
    glUniform4f(gu[8], S.sat, S.bright, S.tint, 2 * S.curve); // curve 1: exponent 2, a circular edge (as the engine)
    glUniform4f(gu[9], std::cos(ang), -std::sin(ang), S.spec, 0);
    glUniform3fv(gu[10], 1, S.tintRGB);
    g_pHyprOpenGL->blend(true);
    for (auto const& r : g_pHyprRenderer->m_renderData.damage.copy().getRects()) {
        g_pHyprOpenGL->scissor(&r);
        drawQuad(sh);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    return {};
}

// ---------------------------------------------------------------- the decoration

class CKineticGlass : public IHyprWindowDecoration {
  public:
    CKineticGlass(PHLWINDOW w) : IHyprWindowDecoration(w), m_window(w) {}
    ~CKineticGlass() override = default;

    SDecorationPositioningInfo getPositioningInfo() override {
        SDecorationPositioningInfo info;
        info.policy = DECORATION_POSITION_ABSOLUTE;
        info.edges  = DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP;
        return info;
    }
    void onPositioningReply(const SDecorationPositioningReply&) override {}
    void draw(PHLMONITOR m, float const& a) override {
        auto w = m_window.lock();
        if (!w || !coversWindow(w)) return;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CGlassPass>(m_window, a));
    }
    eDecorationType getDecorationType() override {
        return DECORATION_CUSTOM;
    }
    void updateWindow(PHLWINDOW) override {}
    void damageEntire() override {
        auto w = m_window.lock();
        if (validMapped(w)) g_pHyprRenderer->damageBox(w->getFullWindowBoundingBox());
    }
    eDecorationLayer getDecorationLayer() override {
        return DECORATION_LAYER_BOTTOM;
    }
    uint64_t getDecorationFlags() override {
        return DECORATION_NON_SOLID;
    }
    std::string getDisplayName() override {
        return NAME;
    }

  private:
    PHLWINDOWREF m_window;
};

static void addDeco(PHLWINDOW w) {
    if (!w || std::ranges::any_of(w->m_windowDecorations, [](const auto& d) { return d->getDisplayName() == NAME; })) return;
    HyprlandAPI::addWindowDecoration(PHANDLE, w, makeUnique<CKineticGlass>(w));
}

static void damageAll() {
    for (auto const& m : State::monitorState()->monitors())
        g_pHyprRenderer->damageMonitor(m);
}

// ---------------------------------------------------------------- hyprctl

static std::string status() {
    int covered = 0;
    for (auto const& w : Desktop::windowState()->windows())
        covered += coversWindow(w) ? 1 : 0;
    return std::format("{{\"plugin\":\"kinetic-glass\",\"version\":\"D0.1\",\"glass\":\"{}\",\"light\":\"{}\",\"covered\":{},\"lit\":{},\"shaders\":{}}}", S.glass, S.light, covered,
                       g_lit.size(), g_glFailed ? "false" : "true");
}

static std::string onCommand(eHyprCtlOutputFormat, std::string request) {
    std::stringstream ss(request);
    std::string       word, sub;
    ss >> word >> sub; // "kinetic-glass", then the subcommand
    if (sub == "status" || sub.empty()) return status();
    if (sub != "set") return "kinetic-glass: unknown command (set key=value ... | status)";
    std::string errors, kv;
    while (ss >> kv) {
        auto eq = kv.find('=');
        if (eq == std::string::npos) {
            errors += kv + ": expected key=value\n";
            continue;
        }
        auto e = applySetting(kv.substr(0, eq), kv.substr(eq + 1));
        if (!e.empty()) errors += e + "\n";
    }
    for (auto const& w : Desktop::windowState()->windows())
        if (validMapped(w)) addDeco(w);
    stepLights();
    scheduleTick();
    damageAll();
    return errors.empty() ? "ok" : errors;
}

// ---------------------------------------------------------------- plugin

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static SP<SHyprCtlCommand> g_cmd;

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[kinetic-glass] built for another Hyprland: run `kinetic glass rebuild`", CHyprColor{1.0, 0.3, 0.3, 1.0}, 8000);
        throw std::runtime_error("[kinetic-glass] version mismatch");
    }

    static auto P1 = Event::bus()->m_events.window.open.listen([](PHLWINDOW w) { addDeco(w); });
    static auto P2 = Event::bus()->m_events.window.active.listen([](PHLWINDOW, Desktop::eFocusReason) {
        if (lightOn()) {
            stepLights();
            scheduleTick();
        }
    });
    static auto P3 = Event::bus()->m_events.window.urgent.listen([](PHLWINDOW) {
        if (lightOn()) scheduleTick();
    });
    static auto P4 = Event::bus()->m_events.render.preChecks.listen([](PHLMONITOR m) {
        if (lightOn() || !g_lit.empty()) stepLights();
        widenDamage(m);
    });
    static auto P5 = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) {
        if (stage != RENDER_POST_WALLPAPER || !lightOn()) return;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CLightPass>());
    });

    g_cmd = HyprlandAPI::registerHyprCtlCommand(PHANDLE, SHyprCtlCommand{.name = "kinetic-glass", .exact = false, .fn = onCommand});

    for (auto const& w : Desktop::windowState()->windows())
        if (validMapped(w)) addDeco(w);

    // Kinetic's engine answers this with every setting
    g_pEventManager->postEvent(SHyprIPCEvent{.event = "kinetic-glass", .data = "ready"});

    return {"kinetic-glass", "Kinetic's window glass and selection light, drawn in Hyprland's frame", "Kinetic", "D0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_pHyprRenderer->m_renderPass.removeAllOfType("CKineticLightPass");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CKineticGlassPass");
    if (g_tick) {
        g_tick->cancel();
        g_pEventLoopManager->removeTimer(g_tick);
        g_tick.reset();
    }
    g_pHyprOpenGL->makeEGLCurrent();
    freeScratch();
    if (g_glassShader) g_glassShader->destroy();
    if (g_lightShader) g_lightShader->destroy();
    g_glassShader.reset();
    g_lightShader.reset();
    if (g_level) munmap(g_level, 8);
    g_level = nullptr;
    damageAll();
}
