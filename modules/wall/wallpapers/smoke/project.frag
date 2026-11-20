// Subtract the pressure gradient: the velocity becomes divergence-free.
void mainImage(out vec4 o, in vec2 fc) {
  ivec2 p = ivec2(fc), m = ivec2(iResolution.xy) - 1;
  float l = texelFetch(iChannel1, clamp(p - ivec2(1, 0), ivec2(0), m), 0).x;
  float r = texelFetch(iChannel1, clamp(p + ivec2(1, 0), ivec2(0), m), 0).x;
  float b = texelFetch(iChannel1, clamp(p - ivec2(0, 1), ivec2(0), m), 0).x;
  float t = texelFetch(iChannel1, clamp(p + ivec2(0, 1), ivec2(0), m), 0).x;
  vec2 v = texelFetch(iChannel0, p, 0).xy - 0.5 * vec2(r - l, t - b);
  if (p.x == 0 || p.x == m.x) v.x = 0.0;
  if (p.y == 0 || p.y == m.y) v.y = 0.0;
  o = vec4(v, 0.0, 1.0);
}
