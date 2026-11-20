// Shade the simulation as an embossed surface in theme colors.
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  vec2 e = 1.2 / iChannelResolution[0].xy;
  float v = texture(iChannel0, uv).g;
  float dx = texture(iChannel0, uv + vec2(e.x, 0.0)).g - texture(iChannel0, uv - vec2(e.x, 0.0)).g;
  float dy = texture(iChannel0, uv + vec2(0.0, e.y)).g - texture(iChannel0, uv - vec2(0.0, e.y)).g;
  vec3 n = normalize(vec3(-dx, -dy, 0.12));
  float light = dot(n, normalize(vec3(-0.5, 0.6, 0.6)));
  float body = smoothstep(0.08, 0.3, v);
  vec3 cell = mix(uPalette[4], uAccent, smoothstep(0.15, 0.4, v));
  vec3 col = mix(uBackground, mix(uBackground, cell, 0.85), body);
  col *= 0.8 + 0.4 * light * body;
  col = lw_blend(col, uPalette[15], pow(max(light, 0.0), 12.0) * body * 0.35);
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
