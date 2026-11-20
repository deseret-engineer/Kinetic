void mainImage(out vec4 c, in vec2 fc) {
  vec3 b = lw_Data.xyz;
  float edge = min(min(b.x, b.y), b.z);
  float line = 1.0 - smoothstep(0.0, fwidth(edge) * 1.2, edge);
  vec3 wire = mix(uAccent, uPalette[5], 0.3) * (1.0 - lw_Data.w * 0.8);
  vec3 col = lw_Color.rgb;
  if (style > 0.5 && style < 1.5) col = mix(uBackground * 0.6, wire * 1.4, line);
  else if (style > 1.5) col = mix(col, wire * 1.2, line * 0.7);
  c = vec4(col, 1.0);
}
