// Subtract the pressure gradient: the velocity becomes divergence-free.
void mainImage(out vec4 o, in vec2 fc) {
  ivec2 p = ivec2(fc), n = ivec2(iResolution.xy);
  float l = texelFetch(iChannel1, fl_wrap(p - ivec2(1, 0), n), 0).x;
  float r = texelFetch(iChannel1, fl_wrap(p + ivec2(1, 0), n), 0).x;
  float b = texelFetch(iChannel1, fl_wrap(p - ivec2(0, 1), n), 0).x;
  float t = texelFetch(iChannel1, fl_wrap(p + ivec2(0, 1), n), 0).x;
  vec4 v = texelFetch(iChannel0, p, 0);
  o = vec4(v.xy - 0.5 * vec2(r - l, t - b), v.z, 1.0);
}
