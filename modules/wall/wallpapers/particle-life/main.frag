// The dots buffer is mipmapped, so a soft bloom is just two coarse lookups.
void mainImage(out vec4 o, in vec2 fc) {
  vec2 uv = fc / iResolution.xy;
  vec3 d = texture(iChannel0, uv).rgb;
  vec3 bloom = textureLod(iChannel0, uv, 2.0).rgb * 0.5 + textureLod(iChannel0, uv, 4.0).rgb * 0.5;
  vec3 c = 1.0 - exp(-(d + bloom) * 1.4);
  vec3 col = lw_blend(uBackground, c * (uDark > 0.5 ? 1.0 : 1.5), max(max(c.r, c.g), c.b));
  o = vec4(lw_dither(col, fc), 1.0);
}
