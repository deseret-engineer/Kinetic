void mainImage(out vec4 o, in vec2 fc) {
  vec3 d = texture(iChannel0, fc / iResolution.xy).rgb;
  vec3 glow = 1.0 - exp(-d * 1.2);
  vec3 col = lw_blend(uBackground, glow * (uDark > 0.5 ? 1.0 : 1.6), max(max(glow.r, glow.g), glow.b));
  o = vec4(lw_dither(col, fc), 1.0);
}
