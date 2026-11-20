void mainImage(out vec4 o, in vec2 fc) {
  vec2 r = iResolution.xy;
  float b = texture(iChannel0, fc / r).y;
  float bx = texture(iChannel0, (fc + vec2(1.0, 0.0)) / r).y - texture(iChannel0, (fc - vec2(1.0, 0.0)) / r).y;
  float by = texture(iChannel0, (fc + vec2(0.0, 1.0)) / r).y - texture(iChannel0, (fc - vec2(0.0, 1.0)) / r).y;
  o = vec4(b, bx, by, 1.0);
}
