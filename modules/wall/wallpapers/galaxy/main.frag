void mainImage(out vec4 o, in vec2 fc) {
  vec2 uv = fc / iResolution.xy;
  vec3 s = texture(iChannel0, uv).rgb;
  vec3 g = textureLod(iChannel0, uv, 2.0).rgb * 0.35 + textureLod(iChannel0, uv, 4.0).rgb * 0.35 +
           textureLod(iChannel0, uv, 5.0).rgb * 0.25;
  vec3 c = s + g * glow;
  c = c / (1.0 + lw_luma(c) * 1.5) * 1.6;            // soft shoulder keeps the cores from blowing out
  // the sky: the theme's background, black, or the custom color. Stars glow on a dark
  // sky and are inked on a light one, decided by the sky itself, not only the theme.
  vec3 base = sky < 0.5 ? uBackground : sky < 1.5 ? vec3(0.0) : skyColor;
  float dark = sky < 0.5 ? uDark : 1.0 - smoothstep(0.35, 0.6, lw_luma(base));
  vec3 starInk = sky < 0.5 ? uForeground : vec3(0.9);
  // faint, fixed background stars
  vec2 cell = floor(fc / 3.0);
  float st = step(1.0 - 0.0015 * bgstars, lw_hash(cell)) * lw_hash(cell + 7.0) * 0.35;
  vec3 star = c + st * starInk * dark;
  float a = max(max(c.r, c.g), c.b);
  vec3 col = mix(mix(base, star, clamp(a, 0.0, 1.0)), base + star * a, dark); // lw_blend, with this sky's darkness
  o = vec4(lw_dither(col, fc), 1.0);
}
