// Aurora: three drifting curtains colored from the Omarchy palette.
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float asp = iResolution.x / iResolution.y;
  float t = iTime * speed;

  vec3 night = mix(uBackground * 1.15 + uPalette[4] * 0.05, uPalette[0], smoothstep(0.0, 0.9, uv.y));
  vec3 col = mix(uBackground, night, uDark);

  // stars
  // workspace parallax in layers: stars far off, curtains between, hills nearest
  vec2 g = (fragCoord + lw_pan(0.08) * iResolution.y) / (iResolution.y / 70.0);
  vec2 id = floor(g);
  float h = lw_hash(id);
  float d = length(fract(g) - 0.5 - (lw_hash2(id + 3.0) - 0.5) * 0.7);
  float star = smoothstep(0.11, 0.0, d) * step(0.8, h) * (0.55 + 0.45 * sin(t * 1.7 + h * 40.0));
  col = lw_blend(col, uForeground, star * smoothstep(0.1, 0.6, uv.y) * 0.8);

  for (int i = 0; i < 3; i++) {
    float fi = float(i);
    float x = (uv.x * asp + lw_pan(0.55 - 0.15 * fi).x) * (0.9 + 0.25 * fi) + fi * 4.1 + (uPointer.x - 0.5) * 0.12 * (fi + 1.0);
    float n = lw_fbm(vec2(x * 0.8 - t * 0.05 * (1.0 + fi * 0.4), t * 0.035 + fi * 7.3));
    float y = uv.y - (0.2 + 0.13 * fi + (n - 0.5) * 0.45);
    float edge = smoothstep(-0.02, 0.015, y);
    float fall = exp(-max(y, 0.0) * (4.2 - fi * 0.7));
    float rays = 0.4 + 0.6 * lw_noise(vec2(x * 15.0 + n * 9.0, t * 0.25 + fi * 3.0));
    rays *= 0.75 + 0.25 * lw_noise(vec2(x * 60.0, t * 0.4 + fi));
    vec3 lo = i == 0 ? uPalette[2] : i == 1 ? uPalette[6] : uAccent;
    vec3 hi = i == 0 ? uPalette[6] : uPalette[5];
    vec3 c = mix(lo, hi, smoothstep(0.0, 0.45, y));
    col = lw_blend(col, c, edge * fall * rays * intensity * (0.7 - 0.13 * fi));
  }

  // distant hills
  float hill = 0.07 + 0.06 * lw_fbm(vec2((uv.x * asp + lw_pan(0.8).x) * 1.8 + 2.0, 3.0));
  col = mix(col, mix(uPalette[0], uBackground, 0.25), smoothstep(hill + 0.004, hill - 0.001, uv.y));
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
