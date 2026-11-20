void mainImage(out vec4 o, in vec2 fc) {
  ivec2 p = ivec2(fc), n = ivec2(iResolution.xy);
  float l = texelFetch(iChannel0, fl_wrap(p - ivec2(1, 0), n), 0).x;
  float r = texelFetch(iChannel0, fl_wrap(p + ivec2(1, 0), n), 0).x;
  float b = texelFetch(iChannel0, fl_wrap(p - ivec2(0, 1), n), 0).y;
  float t = texelFetch(iChannel0, fl_wrap(p + ivec2(0, 1), n), 0).y;
  o = vec4(0.5 * (r - l + t - b), 0.0, 0.0, 1.0);
}
