// Synthwave: palette-driven outrun scene. uAudio.x (bass) brightens the grid.
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
  float t = iTime * speed;
  float horizon = -0.06;
  float y = p.y - horizon;
  vec3 hot = uPalette[5], warm = uPalette[3], red = uPalette[1];

  vec3 col = mix(mix(uBackground, hot, 0.38), mix(uBackground, uPalette[0], 0.6 * uDark), smoothstep(0.0, 0.55, y));
  // stars
  vec2 g = p * 60.0;
  float h = lw_hash(floor(g));
  col = lw_blend(col, uForeground, step(0.9, h) * smoothstep(0.12, 0.0, length(fract(g) - 0.5)) * smoothstep(0.15, 0.4, y) * 0.7);

  // sun with slipping bands
  vec2 sp = p - vec2(0.0, horizon + 0.2);
  float sr = 0.21 + 0.012 * uAudio.x;
  float sd = length(sp);
  float bands = step(0.0, sp.y) + step(sp.y, 0.0) * step(0.42, fract(sp.y * 22.0 - t * 0.35) + sp.y * 2.2 + 0.42);
  vec3 sun = mix(red, warm, smoothstep(-sr, sr, sp.y));
  col = lw_blend(col, hot, 0.5 * exp(-max(sd - sr, 0.0) * 9.0) * step(0.0, y));
  col = mix(col, sun, smoothstep(sr + 0.002, sr - 0.002, sd) * bands * step(0.0, y));

  // ridge line
  float ridge = 0.015 + 0.075 * lw_fbm(vec2(p.x * 3.0 + 4.0, 1.0)) * smoothstep(0.1, 0.6, abs(p.x));
  float m = smoothstep(ridge + 0.002, ridge - 0.002, y) * step(0.0, y);
  col = mix(col, mix(uBackground, uPalette[0], 0.7), m);
  col = lw_blend(col, uAccent, smoothstep(0.004, 0.0, abs(y - ridge)) * 0.6);

  if (y < 0.0) {
    float z = 0.22 / -y;
    vec2 gp = vec2(p.x * z * 1.6, z + t * 0.9);
    vec2 fw = fwidth(gp) * 1.2;
    vec2 d = abs(fract(gp) - 0.5);
    float line = max(smoothstep(0.5 - fw.x, 0.5, d.x), smoothstep(0.5 - fw.y, 0.5, d.y));
    float fade = exp(-z * 0.11);
    vec3 ground = mix(uBackground, uPalette[0], 0.6);
    ground = lw_blend(ground, hot, 0.25 * exp(y * 14.0)); // horizon glow
    col = lw_blend(ground, mix(uAccent, hot, 0.4), line * fade * (0.75 + 1.2 * uAudio.x));
  }
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
