vec2 rot(vec2 v, float a) { float c = cos(a), s = sin(a); return vec2(c * v.x - s * v.y, s * v.x + c * v.y); }

// Where core i starts and how fast it moves, before the center of mass is removed
vec4 core_start(int i, float e, int n, float M) {
  int k = esalt(e);
  float m = gmass(i, e);
  float R = 0.42 * separation * (1.0 + 0.2 * float(max(n - 2, 0)));   // a crowd starts wider
  float a = lw_rand(1, k) * 6.2831853 + 6.2831853 * float(i) / float(n);
  if (n > 2) a += (lw_rand(10 + i, k) - 0.5) * 1.2 / float(n);
  float r = n == 1 ? 0.0 : R * (n > 2 ? 0.75 + 0.5 * lw_rand(20 + i, k) : (M - m) / M * 2.0);
  vec2 p = vec2(cos(a), sin(a)) * r;
  if (n < 2) return vec4(p, 0.0, 0.0);
  // fall in toward the others, off-center by the collision angle: a two-body
  // orbit's speed for this core, 86% of escape so a pair stays bound. A crowd
  // goes slower, or close three-body passes fling galaxies off the screen
  float mo = M - m, meff = mo * mo * mo / (M * M);
  float imp = angle * 0.01745329 * (1.0 + vary * (lw_rand(2, k) - 0.5)) * oside(e);
  float spd = (n == 2 ? 0.86 : 0.7) * approach * (1.0 + vary * 0.2 * (lw_rand(30 + i, k) - 0.5));
  vec2 v = rot(-normalize(p), imp) * sqrt(2.0 * G * meff / max(r, 0.05)) * spd;
  return vec4(p, v);
}

vec4 seed_state(int id, float e) {
  int k = esalt(e), n = ngal();
  if (id == META) return vec4(e, 0.0, 0.0, 0.0);
  float M = 0.0;
  vec4 C[MAXG];
  vec4 com = vec4(0.0);
  for (int i = 0; i < MAXG; i++) M += i < n ? gmass(i, e) : 0.0;
  for (int i = 0; i < MAXG; i++) {
    C[i] = i < n ? core_start(i, e, n, M) : vec4(0.0);
    com += C[i] * (i < n ? gmass(i, e) : 0.0);
  }
  com /= M;                                                     // the center of mass sits still at the origin
  for (int i = 0; i < MAXG; i++) C[i] -= com;
  if (id < MAXG) return id < n ? C[id] : vec4(0.0);
  if (id < STAR0) return vec4(0.0);

  int g = home(id, e);
  vec4 c = C[0];
  for (int i = 1; i < MAXG; i++) if (i == g) c = C[i];
  float m = gmass(g, e);
  int sh = gshape(g, e);                                        // 0 spiral 1 irregular 2 ring 3 lenticular 4 elliptical
  float sp = gspin(g, e);
  float Rd = 0.17 * galsize * sqrt(m);                          // disk edge
  float u = lw_rand(id, k + 1), th = lw_rand(id, k + 2) * 6.2831853, r;
  float pick = lw_rand(id, k + 4);
  float bulge = sh == 0 ? 0.12 : sh == 1 ? 0.04 : sh == 3 ? 0.4 : sh == 4 ? 1.0 : 0.15;
  bool hot = pick < bulge;                                      // bulge and ellipticals: stars on every kind of orbit
  vec2 off;
  if (hot) {
    // Plummer sphere seen flat, cut off so no star starts unbound
    float a0 = Rd * (sh == 4 ? 0.3 : 0.07);
    r = a0 / sqrt(pow(0.02 + u * 0.93, -2.0 / 3.0) - 1.0);
    off = vec2(cos(th), sin(th)) * r;
  } else if (sh == 1 && pick < bulge + 0.45) {
    // irregular: knots of stars scattered through the disk, sheared into arcs as it turns
    int knot = int(lw_rand(id, k + 5) * 7.0);
    float kr = Rd * (0.2 + 0.7 * lw_rand(60 + knot, k + g * 13)), kt = lw_rand(70 + knot, k + g * 13) * 6.2831853;
    float gr = Rd * 0.07 * sqrt(-2.0 * log(max(u, 1e-4)));
    off = vec2(cos(kt), sin(kt)) * kr + vec2(cos(th), sin(th)) * gr;
    r = length(off);
    th = atan(off.y, off.x);
  } else {
    if (sh == 2) {
      // a ring: most of the disk in a band near the edge
      float gs = sqrt(-2.0 * log(max(u, 1e-4))) * cos(lw_rand(id, k + 5) * 6.2831853);
      r = Rd * clamp(0.75 + 0.1 * gs, 0.3, 1.0);
    } else {
      // exponential disk with scale length 0.25, truncated at the edge (sampled
      // from its own CDF, so nothing piles up on the rim)
      r = Rd * -0.25 * log(1.0 - u * (1.0 - exp(-1.0 / 0.25)));
    }
    if (sh == 0) th += arms * 0.5 * sin(armcount * th + r / Rd * 6.0 * winding) * step(0.4, lw_rand(id, k + 3));
    off = vec2(cos(th), sin(th)) * r;
  }
  float eps = 0.012, vc = sqrt(G * m * r * r / pow(r * r + eps * eps, 1.5));
  vec2 v;
  if (hot) {
    float va = lw_rand(id, k + 7) * 6.2831853;                  // a random direction, slower than circular
    v = 0.75 * vc * vec2(cos(va), sin(va));
    v += sp * vc * 0.25 * vec2(-sin(th), cos(th));              // with a little net turn
  } else {
    v = sp * vc * vec2(-sin(th), cos(th));
    if (sh == 1) {                                              // irregular: a ragged disk, not thin rings
      float va = lw_rand(id, k + 7) * 6.2831853;
      v += 0.08 * vc * sqrt(lw_rand(id, k + 8)) * vec2(cos(va), sin(va));
    }
  }
  return vec4(c.xy + off, c.zw + v);
}

void mainImage(out vec4 o, in vec2 fc) {
  ivec2 p = ivec2(fc);
  int id = p.x + p.y * int(iResolution.x);
  float e = encounter();
  vec4 meta = texelFetch(iChannel0, ivec2(META, 0), 0);
  if (iFrame < 2 || abs(meta.x - e) > 0.5) { o = seed_state(id, e); return; }
  if (id >= MAXG && id < STAR0 || id >= int(stars)) { o = texelFetch(iChannel0, p, 0); return; }

  int n = ngal();
  vec4 s = texelFetch(iChannel0, p, 0);
  float h = min(iTimeDelta, 1.0 / 30.0) * speed * 0.5;
  vec2 acc = vec2(0.0);
  if (id < MAXG) {                               // the cores pull on each other
    if (id >= n) { o = s; return; }
    float mi = gmass(id, e);
    for (int j = 0; j < MAXG; j++) {
      if (j >= n) break;
      if (j == id) continue;
      vec4 other = texelFetch(iChannel0, ivec2(j, 0), 0);
      float mj = gmass(j, e);
      vec2 d = other.xy - s.xy;
      float d2 = dot(d, d), r2 = d2 + 0.04 * 0.04;
      acc += G * mj * d / (r2 * sqrt(r2));
      // dynamical friction, weighted by the other core's share of the pair's
      // mass so momentum is conserved: they merge where they are, not drifting
      acc -= (s.zw - other.zw) * 0.5 * (mj / (mi + mj)) * (exp(-d2 / 0.004) + 0.12 * exp(-d2 / 0.06));
    }
  } else {                                       // stars feel every core
    const float eps2 = 0.012 * 0.012;
    for (int j = 0; j < MAXG; j++) {
      if (j >= n) break;
      vec2 d = texelFetch(iChannel0, ivec2(j, 0), 0).xy - s.xy;
      float r2 = dot(d, d) + eps2;
      acc += G * gmass(j, e) * d * inversesqrt(r2 * r2 * r2);
    }
  }
  s.zw += acc * h;                               // semi-implicit Euler: kick, then drift
  s.xy += s.zw * h;
  o = s;
}
