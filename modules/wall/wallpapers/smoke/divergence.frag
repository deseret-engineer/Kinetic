void mainImage(out vec4 o, in vec2 fc) {
  ivec2 p = ivec2(fc), m = ivec2(iResolution.xy) - 1;
  float l = texelFetch(iChannel0, clamp(p - ivec2(1, 0), ivec2(0), m), 0).x;
  float r = texelFetch(iChannel0, clamp(p + ivec2(1, 0), ivec2(0), m), 0).x;
  float b = texelFetch(iChannel0, clamp(p - ivec2(0, 1), ivec2(0), m), 0).y;
  float t = texelFetch(iChannel0, clamp(p + ivec2(0, 1), ivec2(0), m), 0).y;
  if (p.x == 0) l = 0.0;
  if (p.x == m.x) r = 0.0;
  if (p.y == 0) b = 0.0;
  if (p.y == m.y) t = 0.0;
  o = vec4(0.5 * (r - l + t - b), 0.0, 0.0, 1.0);
}
