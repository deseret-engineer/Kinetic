// `point` is the point size; `size` is the Particle size setting
void mainVertex(int id, vec4 state, inout vec4 pos, inout float point, inout vec4 color) {
  float sp = length(state.zw);
  vec3 c;
  if (colors < 0.5) c = lw_palette(lw_rand(id, 7) * 0.35 + state.x * 0.4 + state.y * 0.25 + iTime * 0.01);
  else if (colors < 1.5) c = mix(uPalette[4], uAccent, smoothstep(0.02, 0.25, sp * speed));
  else if (colors < 2.5) c = uAccent;
  else c = uForeground;
  float b = 0.35 * brightness * (0.5 + lw_rand(id, 3));
  color = vec4(c * b, 1.0);
  point = size * iResolution.y / 1200.0 * (0.8 + 0.4 * lw_rand(id, 5));
  point *= 1.0 + uSound.x;
  color.rgb *= 1.0 + uSound.x * 1.5;
}
