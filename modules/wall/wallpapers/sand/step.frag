// One Margolus step of the sand world. Each fragment works out its 2x2 block,
// updates the whole block the same way its three neighbors do, and keeps its cell.
ivec2 N;

vec4 get(ivec2 p) {
  if (p.x < 0 || p.x >= N.x || p.y < 0) return cell(WALL, 0.0, 0.0);
  if (p.y >= N.y) return cell(EMPTY, 0.0, 0.0);
  return texelFetch(iChannel0, p, 0);
}

float dens(int t) { return t == SAND ? 3.0 : t == WATER ? 2.0 : t == SEED ? 1.5 : t == EMPTY || t == FIRE ? 0.0 : 99.0; }
bool movable(int t) { return t == SAND || t == WATER || t == SEED; }
bool plant(int t) { return t == STEM || t == TIP || t == LEAF || t == FLOWER; }

// can a fall from cell x into cell y happen?
bool falls(vec4 x, vec4 y) { int a = ctype(x), b = ctype(y); return movable(a) && dens(b) < dens(a); }

void swap(inout vec4 a, inout vec4 b) { vec4 t = a; a = b; b = t; }

float h01(ivec2 b, int step, int salt) { return lw_rand((b.x * 7919 + b.y * 104729) ^ (step * 2654435), salt + int(uEpoch) * 13); }

// what a spout drops at column x (uv) at this time; returns 0 for nothing
vec4 spout(float x, int step, ivec2 b) {
  float t = iTime;
  for (int k = 0; k < 4; k++) {
    float fk = float(k);
    float sx = 0.5 + 0.45 * sin(t * (0.05 + 0.017 * fk) + fk * 1.9);
    float on = smoothstep(0.2, 0.6, sin(t * (0.09 + 0.02 * fk) + fk * 2.7));
    if (abs(x - sx) < 0.006 && h01(b, step, 30 + k) < sand * on * 0.5)
      return cell(SAND, (float(k + int(t / 40.0)) - floor(float(k + int(t / 40.0)) / 8.0) * 8.0) * 32.0 / 255.0 + h01(b, step, 40) * 31.0 / 255.0, 0.0);
  }
  // rain falls from passing clouds
  float cx = fract(t * 0.013 + 0.3), cw = 0.18;
  float rain_on = smoothstep(0.0, 0.5, sin(t * 0.07 + 1.0)) * rain;
  if (abs(x - cx) < cw && h01(b, step, 50) < rain_on * 0.012) return cell(WATER, h01(b, step, 51), 0.0);
  if (h01(b, step, 60) < seeds * 0.00025) return cell(SEED, h01(b, step, 61), 0.0);
  return vec4(-1.0);
}

void mainImage(out vec4 o, in vec2 fc) {
  N = ivec2(iResolution.xy);
  ivec2 p = ivec2(fc);
  int step = iFrame * 8 + iPass;
  if (iFrame < 2 && iPass == 0) {                  // a new world: a sandy floor and a few stone ledges
    float x = fc.x / iResolution.x, y = fc.y / iResolution.y;
    float floor_ = 0.06 + 0.04 * lw_noise(vec2(x * 6.0, uEpoch));
    o = cell(EMPTY, 0.0, 0.0);
    if (y < floor_) o = cell(SAND, (floor(lw_noise(vec2(x * 3.0, y * 9.0)) * 4.0) * 32.0 + lw_hash(fc) * 31.0) / 255.0, 0.0);
    for (int i = 0; i < 5; i++) {
      vec2 c = vec2(0.12 + 0.19 * float(i) + 0.05 * lw_hash(vec2(float(i), uEpoch)), 0.3 + 0.35 * lw_hash(vec2(uEpoch, float(i))));
      vec2 d = (vec2(x, y) - c) * vec2(iResolution.x / iResolution.y, 1.0);
      float tilt = (lw_hash(vec2(float(i) * 3.0, uEpoch)) - 0.5) * 0.3;
      if (abs(d.y - d.x * tilt) < 0.006 && abs(d.x) < 0.06 + 0.04 * lw_hash(vec2(float(i), 7.0))) o = cell(STONE, lw_hash(fc), 0.0);
    }
    return;
  }
  ivec2 off = ivec2(step & 1);
  ivec2 b = ((p - off + 2) / 2) * 2 - 2 + off;    // block origin (floor division)
  ivec2 lp = p - b;
  vec4 c0 = get(b), c1 = get(b + ivec2(1, 0)), c2 = get(b + ivec2(0, 1)), c3 = get(b + ivec2(1, 1));
  // c0 bottom-left, c1 bottom-right, c2 top-left, c3 top-right

  // sources at the top edge
  if (b.y + 1 >= N.y - 1 && uReverse < 0.01) {
    float x = (float(b.x) + 0.5) / float(N.x);
    vec4 s = spout(x, step, b);
    if (s.x >= 0.0 && ctype(c2) == EMPTY) c2 = s;
    if (uPointer.z > 0.05 && uPointer.w < 1.0) {}
  }
  // the pointer: a click pours sand for a second where it is
  if (uPointer.z > 0.05 && uPointer.w < 1.2) {
    vec2 d = (vec2(b) / vec2(N) - uPointer.xy) * vec2(float(N.x) / float(N.y), 1.0);
    if (dot(d, d) < 0.0004 && ctype(c2) == EMPTY && h01(b, step, 70) < 0.5) c2 = cell(SAND, (7.0 * 32.0 + h01(b, step, 71) * 31.0) / 255.0, 0.0);
  }
  // lightning: a bolt column from the sky that ignites what it hits
  float bolt_k = floor(iTime / 6.0), bolt_age = iTime - bolt_k * 6.0;
  bool strike = lw_rand(int(bolt_k), 80 + int(uEpoch)) < lightning * 0.25 && bolt_age < 0.15;
  float bolt_x = lw_rand(int(bolt_k), 81);
  if (uSound.y < 0.12) { strike = true; bolt_x = lw_rand(int(uSound.w), 82); }
  if (strike && abs((float(b.x) + 0.5) / float(N.x) - bolt_x) < 0.004) {
    if (plant(ctype(c0))) c0 = cell(FIRE, 0.0, 1.0);
    if (plant(ctype(c1))) c1 = cell(FIRE, 0.0, 1.0);
  }

  float r = h01(b, step, 1), r2 = h01(b, step, 2);
  // gravity
  if (falls(c2, c0) && r2 < (ctype(c2) == SEED ? 0.5 : 0.95)) swap(c2, c0);
  if (falls(c3, c1) && h01(b, step, 3) < (ctype(c3) == SEED ? 0.5 : 0.95)) swap(c3, c1);
  // sliding down the slope
  if (r < 0.5) {
    if (falls(c2, c1) && !falls(c2, c0) && ctype(c0) != EMPTY) swap(c2, c1);
    else if (falls(c3, c0) && !falls(c3, c1) && ctype(c1) != EMPTY) swap(c3, c0);
  } else {
    if (falls(c3, c0) && !falls(c3, c1) && ctype(c1) != EMPTY) swap(c3, c0);
    else if (falls(c2, c1) && !falls(c2, c0) && ctype(c0) != EMPTY) swap(c2, c1);
  }
  // water levels out sideways
  int t0 = ctype(c0), t1 = ctype(c1), t2 = ctype(c2), t3 = ctype(c3);
  if (((t0 == WATER && t1 == EMPTY) || (t1 == WATER && t0 == EMPTY)) && h01(b, step, 4) < 0.9) swap(c0, c1);
  if (((t2 == WATER && t3 == EMPTY) || (t3 == WATER && t2 == EMPTY)) && h01(b, step, 5) < 0.6) swap(c2, c3);

  // moisture: sand touching water gets wet and soaks a little of it up; wetness spreads through sand and dries
  t0 = ctype(c0); t1 = ctype(c1); t2 = ctype(c2); t3 = ctype(c3);
  bool water = t0 == WATER || t1 == WATER || t2 == WATER || t3 == WATER;
  float wet = 0.0;
  if (t0 == SAND) wet = max(wet, c0.b);
  if (t1 == SAND) wet = max(wet, c1.b);
  if (t2 == SAND) wet = max(wet, c2.b);
  if (t3 == SAND) wet = max(wet, c3.b);
  wet = water ? 1.0 : wet * 0.985;
  if (t0 == SAND) c0.b = max(c0.b * 0.9995, wet * 0.97);
  if (t1 == SAND) c1.b = max(c1.b * 0.9995, wet * 0.97);
  if (t2 == SAND) c2.b = max(c2.b * 0.9995, wet * 0.97);
  if (t3 == SAND) c3.b = max(c3.b * 0.9995, wet * 0.97);
  if (water && (t0 == SAND || t1 == SAND) && h01(b, step, 6) < 0.0015) {   // absorbed
    if (t2 == WATER) c2 = cell(EMPTY, 0.0, 0.0); else if (t3 == WATER) c3 = cell(EMPTY, 0.0, 0.0);
  }

  // plants: a seed on wet sand sprouts; a growing tip climbs, branches and blooms
  t0 = ctype(c0); t1 = ctype(c1); t2 = ctype(c2); t3 = ctype(c3);
  if (t2 == SEED && t0 == SAND && c0.b > 0.3 && h01(b, step, 7) < 0.02) c2 = cell(TIP, c2.g, 0.35 + 0.6 * h01(b, step, 8));
  if (t3 == SEED && t1 == SAND && c1.b > 0.3 && h01(b, step, 9) < 0.02) c3 = cell(TIP, c3.g, 0.35 + 0.6 * h01(b, step, 8));
  for (int side = 0; side < 2; side++) {
    vec4 lo = side == 0 ? c0 : c1, hi = side == 0 ? c2 : c3, nb = side == 0 ? c3 : c2;
    if (ctype(lo) == TIP && h01(b, step, 10 + side) < 0.03) {
      float left = lo.b - 0.07;
      if (left <= 0.0) {                                  // bloom: a little cluster of petals
        lo = cell(FLOWER, lo.g, 0.0);
        if (ctype(hi) == EMPTY) hi = cell(FLOWER, lo.g, 0.0);
        if (ctype(nb) == EMPTY && h01(b, step, 16 + side) < 0.7) nb = cell(FLOWER, lo.g, 0.0);
      } else if (ctype(hi) == EMPTY) {
        float r3 = h01(b, step, 12 + side);
        bool lean = r3 < 0.2 && ctype(nb) == EMPTY;
        if (lean) nb = cell(TIP, lo.g, left); else hi = cell(TIP, lo.g, left);
        if (!lean && ctype(nb) == EMPTY) {
          float r4 = h01(b, step, 14 + side);
          if (r4 < 0.12) nb = cell(TIP, lo.g, left * 0.6);   // a side branch
          else if (r4 < 0.45) nb = cell(LEAF, lo.g, 0.0);
        }
        lo = cell(STEM, lo.g, left);
      } else if (ctype(hi) != TIP && ctype(hi) != STEM) lo = cell(FLOWER, lo.g, 0.0);
    }
    if (side == 0) { c0 = lo; c2 = hi; c3 = nb; } else { c1 = lo; c3 = hi; c2 = nb; }
  }
  // old flowers wilt; wilting spreads down the plant and it crumbles, dropping seeds
  vec4 cs[4] = vec4[4](c0, c1, c2, c3);
  bool wilting = false, burning = false;
  for (int i = 0; i < 4; i++) { wilting = wilting || ctype(cs[i]) == WILT; burning = burning || ctype(cs[i]) == FIRE; }
  for (int i = 0; i < 4; i++) {
    int t = ctype(cs[i]);
    float rr = h01(b, step, 20 + i);
    if (t == FLOWER && rr < 0.00012) cs[i] = cell(WILT, cs[i].g, 1.0);
    else if (plant(t) && wilting && rr < 0.03) cs[i] = cell(WILT, cs[i].g, 1.0);
    else if (t == WILT) {
      cs[i].b -= 0.004;
      if (cs[i].b <= 0.0) cs[i] = rr < 0.08 ? cell(SEED, cs[i].g, 0.0) : cell(EMPTY, 0.0, 0.0);
    } else if (plant(t) && burning && rr < 0.12) cs[i] = cell(FIRE, 0.0, 1.0);
    else if (t == FIRE) {
      cs[i].b -= 0.006 + 0.01 * rr;
      cs[i].g = rr;
      if (cs[i].b <= 0.0) cs[i] = cell(EMPTY, 0.0, 0.0);
      if (water) cs[i] = cell(EMPTY, 0.0, 0.0);
    }
  }
  // draining: when the world is full the floor opens and everything runs out
  if (uReverse > 0.0 && b.y == 0) {
    if (movable(ctype(cs[0])) && h01(b, step, 30) < uReverse) cs[0] = cell(EMPTY, 0.0, 0.0);
    if (movable(ctype(cs[1])) && h01(b, step, 31) < uReverse) cs[1] = cell(EMPTY, 0.0, 0.0);
  }
  o = lp.y == 0 ? (lp.x == 0 ? cs[0] : cs[1]) : (lp.x == 0 ? cs[2] : cs[3]);
}
