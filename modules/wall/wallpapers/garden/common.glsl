// Garden: shared by every pass. Positions are in screen-height units, y up.
#define MAXP 48
#define PER_LAYER 16
#define NB 127            // branches per plant: a complete binary tree, depth 0..6
#define PV (NB * 36)      // vertices per plant: 18 stem + 12 leaf + 6 flower per branch
#define GRASS 1100          // meadow blades per layer, one triangle each
#define BLOCK (6 + GRASS * 3 + PER_LAYER * PV)

float layer_scale(int L) { return L == 0 ? 0.55 : L == 1 ? 0.8 : 1.12; }
float layer_base(int L) { return L == 0 ? 0.34 : L == 1 ? 0.23 : 0.11; }
float ground_y(int L, float x) {
  float s = float(L) * 7.1;
  return layer_base(L) + 0.022 * sin(x * 4.0 + s) + 0.012 * sin(x * 11.0 + s * 2.3);
}

int depth_of(int i) { int d = 0; for (int k = i + 1; k > 1; k >>= 1) d++; return d; }

struct Shape { int maxd, kind, leafd; float L0, ratio, spread, curl, width, leaf, flower, grow, life, flex; };

// genes A: kind, size, hue, spread; genes B: curl, lean, seed, lifespan
Shape shape_of(vec4 ga, vec4 gb) {
  Shape s;
  s.kind = int(ga.x + 0.5);
  if (s.kind == 0) {        // wildflower: a branching stalk with a bloom on each tip
    s.maxd = 3; s.L0 = 0.085; s.ratio = 0.7; s.spread = 0.4; s.curl = 0.5; s.width = 0.005;
    s.leaf = 0.05; s.leafd = 0; s.flower = 0.019; s.grow = 2.0; s.life = 6.0; s.flex = 1.0;
  } else if (s.kind == 1) { // shrub: low and dense, small flowers then berries
    s.maxd = 5; s.L0 = 0.04; s.ratio = 0.84; s.spread = 0.75; s.curl = 0.6; s.width = 0.008;
    s.leaf = 0.03; s.leafd = 1; s.flower = 0.01; s.grow = 4.5; s.life = 14.0; s.flex = 0.5;
  } else if (s.kind == 2) { // tree: a trunk, a crown of foliage, blossom in its prime
    s.maxd = 6; s.L0 = 0.13; s.ratio = 0.76; s.spread = 0.5; s.curl = 0.35; s.width = 0.024;
    s.leaf = 0.042; s.leafd = 2; s.flower = 0.012; s.grow = 9.0; s.life = 32.0; s.flex = 0.25;
  } else {                  // grass: a tuft of blades with seed heads
    s.maxd = 4; s.L0 = 0.003; s.ratio = 1.0; s.spread = 0.22; s.curl = 1.4; s.width = 0.006;
    s.leaf = 0.0; s.leafd = 99; s.flower = 0.012; s.grow = 1.5; s.life = 7.0; s.flex = 1.4;
  }
  s.spread *= ga.w;
  s.curl *= gb.x;
  s.life *= gb.w;
  return s;
}

struct Life { float g, bloom, wilt; };
Life life_of(float age, Shape sh) {
  Life l;
  float span = sh.life - sh.grow;
  l.g = clamp(age / sh.grow, 0.0, 1.0);
  l.bloom = smoothstep(sh.grow * 0.7, sh.grow, age) * (1.0 - smoothstep(sh.grow + span * 0.45, sh.grow + span * 0.62, age));
  l.wilt = smoothstep(sh.grow + span * 0.6, sh.life, age);
  return l;
}

float sun_hour() {
  if (sky < 0.5) return uDayTime;
  return sky < 1.5 ? 8.0 : sky < 2.5 ? 13.0 : sky < 3.5 ? 18.6 : 0.5;
}
float sun_elev() { return sin((sun_hour() - 6.0) / 12.0 * 3.14159); }
float daylight() { return smoothstep(-0.12, 0.25, sun_elev()); }

float bloom_hue(float g) { return fract(0.47 + g * 0.78); } // never leaf-green

vec3 hsv(float h, float s, float v) { return v * mix(vec3(1.0), clamp(abs(fract(h + vec3(0.0, 2.0, 1.0) / 3.0) * 6.0 - 3.0) - 1.0, 0.0, 1.0), s); }

// The sky at height y (0 bottom, 1 top).
vec3 sky_color(float y) {
  float e = sun_elev(), day = daylight(), dusk = exp(-e * e * 30.0);
  vec3 top, hor;
  if (colors < 0.5) {
    top = mix(vec3(0.02, 0.03, 0.08), vec3(0.22, 0.45, 0.85), day);
    hor = mix(vec3(0.05, 0.07, 0.15), vec3(0.65, 0.8, 0.95), day);
    hor = mix(hor, vec3(1.0, 0.55, 0.3), dusk * 0.8);
    top = mix(top, vec3(0.3, 0.25, 0.5), dusk * 0.4);
  } else {
    top = mix(uBackground * 0.6, mix(uBackground, uAccent, 0.35), day);
    hor = mix(uBackground, mix(uBackground, uForeground, 0.5), day);
    hor = mix(hor, uPalette[3], dusk * 0.4);
  }
  return mix(hor, top, smoothstep(0.1, 1.0, y));
}

// Light falling on the plants: warm at dusk, blue moonlight at night.
vec3 light_color() {
  float e = sun_elev(), day = daylight(), dusk = exp(-e * e * 30.0);
  vec3 c = mix(vec3(0.16, 0.2, 0.32), vec3(1.0), day);
  return mix(c, vec3(1.0, 0.72, 0.5), dusk * 0.6);
}

// Wind at horizontal position x (uv) for a plant of this flexibility.
float wind_at(float x) {
  float t = iTime;
  float w = wind * (0.35 + 0.25 * sin(t * 0.21 + x * 2.0)) + wind * 0.12 * sin(t * 1.3 + x * 9.0) + 0.05 * wind * sin(t * 3.1 + x * 23.0);
  w += 0.6 * clamp(uWorkspace.z - uWorkspace.x, -1.0, 1.0) * exp(-uWorkspace.y * 1.5);          // workspace gust
  w += 0.5 * min(uSound.z, 1.5) * exp(-uSound.y * 2.5) * sin(t * 8.0 + x * 5.0);                 // sound
  if (uPointer.z > 0.05) {                                                                        // the pointer brushes past
    float d = x - uPointer.x;
    w -= sign(d) * 0.9 * exp(-d * d * 300.0) * smoothstep(0.6, 0.0, uPointer.y) * uPointer.z;
  }
  return w;
}

// One branch of plant s, walked from its root. Returns base (local), start angle,
// grown length; also curvature, widths at both ends and the branch's growth.
struct Branch { vec2 p; float a, len, c, w0, w1, gd; int d; bool ok; };

Branch walk(int s, int i, vec4 st, vec4 ga, vec4 gb) {
  Branch b;
  Shape sh = shape_of(ga, gb);
  Life lf = life_of(st.y, sh);
  int L = s / PER_LAYER, seed = int(gb.z);
  float sc = layer_scale(L) * ga.y;
  int d = depth_of(i);
  b.ok = d <= sh.maxd;
  b.d = d;
  float sway = wind_at(st.x) * sh.flex;
  vec2 p = vec2(0.0);
  float a = 1.5708 + gb.y - sway * 0.08;
  int n = 0;
  for (int lev = 0; lev <= 6; lev++) {
    if (lev > d) break;
    float main = 1.0, off = 0.0;
    if (lev > 0) {
      int bit = (i + 1) >> (d - lev) & 1, parent = n;
      n = 2 * n + 1 + bit;
      float r = lw_rand(seed * 131 + n, 3);
      if (sh.kind == 1 || sh.kind == 2) {        // one child carries on, the other branches off
        bool leader = (lw_rand(seed * 131 + parent, 5) < 0.5) == (bit == 0);
        main = leader ? 1.0 : 0.8;
        off = (bit == 0 ? 1.0 : -1.0) * sh.spread * (leader ? 0.25 + 0.3 * r : 0.8 + 0.5 * r);
        if (!leader && lw_rand(seed * 131 + n, 9) < (sh.kind == 2 && lev < 2 ? 0.0 : 0.18)) b.ok = false;
      } else {
        off = (bit == 0 ? 1.0 : -1.0) * sh.spread * (0.5 + 0.9 * r);
      }
      if (sh.kind == 0 && lev == sh.maxd && lw_rand(seed * 131 + n, 11) < 0.3) b.ok = false;
    }
    a += off;
    float full = sh.L0 * sc * pow(sh.ratio, float(lev)) * main * (0.8 + 0.4 * lw_rand(seed * 131 + n, 7));
    if (sh.kind == 3 && lev == sh.maxd) full = 0.11 * sc * (0.6 + 0.7 * lw_rand(seed * 131 + n, 7));
    if (sh.kind == 0 && lev == 0) full *= 1.8;
    float gd = clamp(lf.g * float(sh.maxd + 1) - float(lev), 0.0, 1.0);
    float len = full * (gd * (2.0 - gd));
    float bend = sh.curl * (lw_rand(seed * 131 + n, 13) - 0.5);
    if (sh.kind == 3) bend = sign(off) * sh.curl * (0.4 + 0.5 * lw_rand(seed * 131 + n, 13)) * float(lev == sh.maxd);
    bend -= sway * sh.flex * 0.25 * float(lev + 1) / float(sh.maxd + 1);
    bend -= lf.wilt * (0.9 + 0.6 * sh.flex) * cos(a) * float(lev + 1) / float(sh.maxd + 1);
    float c = bend / max(full, 1e-4);
    float w = sh.width * sc * pow(sh.kind == 2 ? 0.66 : 0.72, float(lev)) * (0.35 + 0.65 * lf.g);
    if (lev == d) {
      b.p = p; b.a = a; b.len = b.ok ? len : 0.0; b.c = c; b.gd = gd;
      b.w0 = w;
      b.w1 = lev == sh.maxd ? (sh.kind == 3 ? 0.0 : w * 0.45) : w * (sh.kind == 2 ? 0.66 : 0.72);
      return b;
    }
    // to the end of this arc
    float e = a + c * len;
    p += abs(c) > 1e-3 ? vec2(sin(e) - sin(a), cos(a) - cos(e)) / c : vec2(cos(a), sin(a)) * len;
    a = e;
  }
  b.ok = false;
  b.len = 0.0;
  return b;
}

vec2 arc_at(vec2 p, float a, float c, float s) {
  float e = a + c * s;
  return p + (abs(c) > 1e-3 ? vec2(sin(e) - sin(a), cos(a) - cos(e)) / c : vec2(cos(a), sin(a)) * s);
}
