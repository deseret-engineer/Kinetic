// Soil, scent roads glowing in each colony's color, food, nests, then the ants.
void mainImage(out vec4 o, in vec2 fc) {
  vec2 uv = fc / iResolution.xy;
  float ar = iResolution.x / iResolution.y;
  vec4 f = texture(iChannel0, uv);
  float grain = lw_fbm(uv * vec2(ar, 1.0) * 18.0);
  vec3 col = uBackground * (0.85 + 0.3 * grain) + 0.015 * (lw_hash(fc) - 0.5);
  int n = int(colonies + 0.5);
  for (int c = 0; c < 3; c++) {
    if (c >= n) break;
    float s = scent(f, c);
    col = lw_blend(col, colony_color(c), (1.0 - exp(-s * 2.0)) * 0.5 * trails);
    vec2 d = (uv - nest_pos(c)) * vec2(ar, 1.0);
    float r = length(d);
    col = mix(col, uBackground * 0.5, smoothstep(0.035, 0.02, r));               // the mound
    col = lw_blend(col, colony_color(c), smoothstep(0.004, 0.0, abs(r - 0.03)) * 0.6 + smoothstep(0.012, 0.0, r) * 0.3);
  }
  // food: speckled crumbs
  vec2 cell = floor(fc / max(2.0, iResolution.y / 500.0));
  float crumb = step(1.0 - f.b * 0.9, lw_hash(cell)) * step(0.02, f.b);
  vec3 fcol = mix(vec3(0.55, 0.78, 0.3), vec3(1.0, 0.86, 0.45), lw_hash(cell + 7.0));
  col = mix(col, fcol, crumb);
  col = lw_blend(col, fcol, f.b * 0.12);
  vec4 b = texture(iChannel1, uv);
  col = col * (1.0 - b.a) + b.rgb;
  o = vec4(lw_dither(col, fc), 1.0);
}
