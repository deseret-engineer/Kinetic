float potential(vec2 p) {
  float t = iTime * 0.05;
  return lw_noise(p + vec2(t, -t * 0.7)) + 0.5 * lw_noise(p * 2.1 + vec2(-t * 1.3, t)) + 0.25 * lw_noise(p * 4.3 + t * 2.0);
}

void mainImage(out vec4 o, in vec2 fc) {
  float asp = iResolution.x / iResolution.y;
  vec2 p = fc / iResolution.y * turbulence;
  float e = 0.02;
  float dx = potential(p + vec2(e, 0.0)) - potential(p - vec2(e, 0.0));
  float dy = potential(p + vec2(0.0, e)) - potential(p - vec2(0.0, e));
  vec2 v = vec2(dy, -dx) / (2.0 * e);                 // curl: divergence-free
  v += vec2(0.15, 0.05);                              // a gentle overall drift
  o = vec4(v * 0.12, 0.0, 1.0);                       // screen heights per second
}
