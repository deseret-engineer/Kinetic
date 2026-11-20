void mainImage(out vec4 o, in vec2 fc) {
  vec2 uv = fc / iResolution.xy;
  vec3 s = texture(iChannel0, uv).rgb;
  vec3 g = textureLod(iChannel0, uv, 2.0).rgb * 0.35 + textureLod(iChannel0, uv, 4.0).rgb * 0.35 +
           textureLod(iChannel0, uv, 5.0).rgb * 0.25;
  vec3 c = s + g * glow;
  c = c / (1.0 + lw_luma(c) * 1.5) * 1.6;            // soft shoulder keeps the cores from blowing out
  // faint, fixed background stars
  vec2 cell = floor(fc / 3.0);
  float st = step(0.9985, lw_hash(cell)) * lw_hash(cell + 7.0) * 0.35;
  vec3 col = lw_blend(uBackground, c + st * uForeground * uDark, max(max(c.r, c.g), c.b));
  o = vec4(lw_dither(col, fc), 1.0);
}
