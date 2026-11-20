// Every particle sums the force from every other one (O(n^2), all on the GPU).
// Distances wrap around the edges: the world is a torus in screen proportions.
void mainImage(out vec4 o, in vec2 fc) {
  ivec2 p = ivec2(fc);
  int W = int(iResolution.x), id = p.x + p.y * W, n = int(particles);
  vec4 s = texelFetch(iChannel0, p, 0);
  float sig = seed + species * 10000.0;
  if (iFrame < 2) { o = vec4(lw_rand(id, 1), lw_rand(id, 2), 0.0, 0.0); return; }
  if (id >= n) { o = s; return; }
  float asp = 16.0 / 10.0;                    // world proportions (the image stretches to the screen)
  int me = species_of(id);
  float rmax = radius, beta = 0.3;
  vec2 f = vec2(0.0);
  // Loop species by species: the attraction is then a loop constant, with no
  // dynamically indexed array (which GPUs spill to slow scratch memory).
  int S = int(species);
  for (int sp = 0; sp < S; sp++) {
    float att = attraction(me, sp);
    for (int j = sp; j < n; j += S) {
      vec2 d = texelFetch(iChannel0, ivec2(j % W, j / W), 0).xy - s.xy;
      d -= round(d);                          // shortest way around the torus
      d.x *= asp;
      float r2 = dot(d, d);
      if (r2 >= rmax * rmax || r2 < 1e-10) continue;
      float r = sqrt(r2), x = r / rmax;
      float a = x < beta ? x / beta - 1.0 : att * (1.0 - abs(2.0 * x - 1.0 - beta) / (1.0 - beta));
      f += d * (a / r);
    }
  }
  float dt = min(iTimeDelta, 1.0 / 30.0);
  vec2 v = s.zw * pow(mix(0.9, 0.0005, friction), dt) + f * rmax * force * (1.0 + 2.0 * uSound.x) * 8.0 * dt;
  if (uSound.y < 0.2) v += (vec2(lw_rand(id, int(uSound.w)), lw_rand(id, int(uSound.w) + 1)) - 0.5) * uSound.z * 0.3;
  if (uPointer.z > 0.05 && pointer < 1.5) {
    vec2 d = (uPointer.xy - s.xy);
    d -= round(d);
    d.x *= asp;
    float k = exp(-dot(d, d) * 60.0) * uPointer.z * (pointer < 0.5 ? -1.0 : 0.6);
    v += normalize(d + 1e-6) * k * 2.0 * dt;
  }
  s.xy = fract(s.xy + v * dt * vec2(1.0 / asp, 1.0));
  o = vec4(s.xy, v);
}
