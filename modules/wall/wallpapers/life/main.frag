void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  vec2 g = floor(iChannelResolution[0].xy * 2.0 / cell);
  vec2 gp = uv * g;
  ivec2 c = ivec2(floor(gp));
  vec2 f = fract(gp);
  vec4 s = texelFetch(iChannel0, c, 0);
  float age = floor(s.g), nbrs = fract(s.g) * 10.0;

  vec3 col = uBackground;
  vec3 cellCol;
  if (colors < 0.5) cellCol = uAccent;
  else if (colors < 1.5) cellCol = mix(uPalette[15], mix(uAccent, uPalette[5], smoothstep(8.0, 60.0, age)), smoothstep(0.0, 6.0, age));
  else if (colors < 2.5) cellCol = nbrs < 2.5 ? uPalette[4] : nbrs < 3.5 ? uAccent : uPalette[1];
  else cellCol = tint;

  // cell shape mask, antialiased in screen pixels
  float px = g.x / iResolution.x; // one screen pixel in cell units
  vec2 q = abs(f - 0.5);
  float d;
  if (shape < 0.5) d = max(q.x, q.y) - 0.42;
  else if (shape < 1.5) d = length(max(q - 0.28, 0.0)) - 0.14;
  else d = length(f - 0.5) - 0.32;
  float mask = cell < 4.0 ? 1.0 : smoothstep(px, -px, d);

  float alive = s.r;
  // a soft neon glow from living neighbors
  float halo = 0.0;
  if (glow > 0.0)
    for (int k = 0; k < 9; k++) {
      ivec2 o = ivec2(k % 3 - 1, k / 3 - 1);
      vec2 dd = gp - (vec2(c + o) + 0.5);
      halo += texelFetch(iChannel0, c + o, 0).r * exp(-dot(dd, dd) * 1.6);
    }
  col = lw_blend(col, cellCol, s.b * 0.35 * (1.0 - alive) * mask);
  col = lw_blend(col, cellCol, min(halo, 2.0) * 0.12 * glow);
  vec3 lit = lw_blend(uBackground, cellCol, 1.0) * (1.08 - 0.25 * length(f - 0.5));   // a little shading on each cell
  col = mix(col, lit, alive * mask);
  if (grid > 0.5 && cell >= 5.0) {
    vec2 e = min(f, 1.0 - f) / (g / iResolution.xy) ;
    col = mix(col, mix(uBackground, uForeground, 0.08), (1.0 - smoothstep(0.0, 1.0, min(e.x, e.y))) * 0.8);
  }
  // pointer halo shows where drawing lands
  float asp = iResolution.x / iResolution.y;
  col = lw_blend(col, uForeground, 0.05 * uPointer.z * smoothstep(0.03, 0.0, length((uv - uPointer.xy) * vec2(asp, 1.0))));
  col *= 1.0 - 0.18 * dot(uv - 0.5, uv - 0.5);
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
