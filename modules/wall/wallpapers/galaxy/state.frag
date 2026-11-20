vec2 rot(vec2 v, float a) { float c = cos(a), s = sin(a); return vec2(c * v.x - s * v.y, s * v.x + c * v.y); }

vec4 seed_state(int id, float e) {
  int k = esalt(e);
  float mA = 1.0, mB = mass_b(e), M = mA + mB;
  float ang = (lw_rand(1, k) - 0.5) * 0.8;
  vec2 pa = rot(vec2(-0.4, 0.12), ang), pb = rot(vec2(0.4, -0.12), ang);
  vec2 com = (pa * mA + pb * mB) / M;                                     // center of mass at the origin
  pa -= com, pb -= com;
  float d = length(pb - pa);
  vec2 dir = normalize(pa - pb);
  float impact = 0.35 + 0.4 * lw_rand(2, k);                              // how far off a head-on hit
  vec2 vrel = rot(dir, impact * (lw_rand(4, k) < 0.5 ? 1.0 : -1.0)) * sqrt(2.0 * G * M / d) * 0.86;
  vec2 va = -vrel * mB / M, vb = vrel * mA / M;
  if (id == 0) return vec4(pa, va);
  if (id == 1) return vec4(pb, vb);
  if (id == 2) return vec4(e, 0.0, 0.0, 0.0);
  bool inB = home(id, e) > 0.5;
  vec2 c = inB ? pb : pa, cv = inB ? vb : va;
  float m = inB ? mB : mA;
  float spin = lw_rand(inB ? 5 : 6, k) < 0.5 ? 1.0 : -1.0;
  float u = lw_rand(id, k + 1);
  float r = 0.006 + 0.17 * sqrt(m) * min(-log(1.0 - u * 0.98) * 0.32, 1.0); // exponential disk
  float th = lw_rand(id, k + 2) * 6.2831853;
  // a hint of spiral arms in the initial disk
  th += 0.35 * sin(2.0 * th + r * 30.0) * step(0.5, lw_rand(id, k + 3));
  vec2 off = vec2(cos(th), sin(th)) * r;
  float eps = 0.012, vc = sqrt(G * m * r * r / pow(r * r + eps * eps, 1.5));
  return vec4(c + off, cv + spin * vc * vec2(-sin(th), cos(th)));
}

void mainImage(out vec4 o, in vec2 fc) {
  ivec2 p = ivec2(fc);
  int id = p.x + p.y * int(iResolution.x);
  float e = encounter();
  vec4 meta = texelFetch(iChannel0, ivec2(2, 0), 0);
  if (iFrame < 2 || abs(meta.x - e) > 0.5) { o = seed_state(id, e); return; }
  if (id == 2) { o = meta; return; }
  if (id >= int(stars) + 3) { o = texelFetch(iChannel0, p, 0); return; }

  vec4 s = texelFetch(iChannel0, p, 0);
  vec4 A = texelFetch(iChannel0, ivec2(0, 0), 0), B = texelFetch(iChannel0, ivec2(1, 0), 0);
  float mB = mass_b(e), h = min(iTimeDelta, 1.0 / 30.0) * speed * 0.5;
  vec2 acc;
  if (id < 2) {                                  // the cores pull on each other
    vec4 other = id == 0 ? B : A;
    float m = id == 0 ? mB : 1.0;
    vec2 d = other.xy - s.xy;
    float r2 = dot(d, d) + 0.03 * 0.03;
    acc = G * m * d / (r2 * sqrt(r2));
    // dynamical friction, weighted by the other core's share of the mass so
    // momentum is conserved: the pair merges where it is instead of drifting
    acc -= (s.zw - other.zw) * 0.5 * (m / (1.0 + mB)) * (exp(-dot(d, d) / 0.004) + 0.12 * exp(-dot(d, d) / 0.06));
  } else {                                       // stars feel both cores
    float eps2 = 0.012 * 0.012;
    vec2 da = A.xy - s.xy, db = B.xy - s.xy;
    float ra = dot(da, da) + eps2, rb = dot(db, db) + eps2;
    acc = G * da / (ra * sqrt(ra)) + G * mB * db / (rb * sqrt(rb));
  }
  s.zw += acc * h;                               // semi-implicit Euler: kick, then drift
  s.xy += s.zw * h;
  o = s;
}
