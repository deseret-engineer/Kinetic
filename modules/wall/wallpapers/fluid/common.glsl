// Shared by every Fluid pass. Velocity is in sim cells per 1/60 s.
float fl_dt() { return clamp(iTimeDelta * 60.0, 0.0, 3.0); }
ivec2 fl_wrap(ivec2 p, ivec2 n) { return (p + n) % n; }

// Torus distance in aspect-corrected units, so splats near an edge wrap round.
vec2 fl_delta(vec2 uv, vec2 c) {
  vec2 d = uv - c;
  d -= floor(d + 0.5);
  return d * vec2(iResolution.x / iResolution.y, 1.0);
}

vec3 fl_ink(float h) {
  if (inks < 0.5) return uPalette[1 + int(h * 6.0) % 6];
  if (inks < 1.5) return mix(uAccent, uForeground, fract(h * 3.0) * 0.4);
  if (inks < 2.5) return 0.55 + 0.45 * cos(6.2832 * (h + vec3(0.0, 0.33, 0.67)));
  return mix(uForeground, vec3(lw_luma(uForeground)), 0.6) * (0.6 + 0.4 * fract(h * 5.0));
}

// Bursts: jets of ink that fire one after another. Each has a place, a heading,
// a color and a short life; the current and the previous one can overlap.
struct Burst { vec2 pos; vec2 dir; vec3 col; float w; };

Burst fl_burst(float k, float age, float life) {
  Burst b;
  float h1 = lw_hash(vec2(k, 1.7)), h2 = lw_hash(vec2(k, 9.3)), h3 = lw_hash(vec2(k, 4.1));
  b.pos = vec2(lw_hash(vec2(k, 3.3)), lw_hash(vec2(k, 7.9)));
  float a = h1 * 6.2832 + age * (h2 - 0.5) * 3.0;            // jets curve a little as they fire
  b.dir = vec2(cos(a), sin(a));
  b.pos += b.dir * age * 0.05;
  b.col = fl_ink(h3);
  b.w = age >= 0.0 && age < life ? sin(3.1416 * age / life) : 0.0;
  return b;
}

// Up to three bursts alive at this time (two scheduled, one from a sound onset).
int fl_bursts(out Burst bs[3]) {
  float period = 1.8 / max(bursts, 0.05), life = 0.45;
  float k = floor(iTime / period);
  bs[0] = fl_burst(k, iTime - k * period, bursts > 0.01 ? life : 0.0);
  bs[1] = fl_burst(k - 1.0, iTime - (k - 1.0) * period, bursts > 0.01 ? life : 0.0);
  bs[2] = fl_burst(500.0 + uSound.w, uSound.y, 0.35);
  bs[2].w *= min(uSound.z * 1.5, 2.0);
  return 3;
}
