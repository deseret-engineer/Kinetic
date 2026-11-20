// Snow: a winter night (or day, on light themes). Layered hills with snow-lit
// tops and pine silhouettes, soft depth haze, and flakes in several depth layers;
// the nearest are large and out of focus.
float pines(float x, float base, float seed) {
  float cell = floor(x * 14.0), f = fract(x * 14.0);
  float h = lw_hash(vec2(cell, seed));
  if (h < 0.55) return base;
  float th = 0.035 + 0.05 * lw_hash(vec2(cell, seed + 1.0));
  float w = abs(f - 0.5) * 2.0 / 0.7;                    // a narrow cone
  float y = 1.0 - w;                                     // 0 at the edge, 1 on the axis
  float tiers = 0.85 + 0.15 * fract(y * 3.0);            // a few soft tiers of branches
  return base + max(th * y * tiers, 0.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float asp = iResolution.x / iResolution.y;
  vec3 top = uBackground, bot = mix(uBackground, uPalette[4], 0.2);
  vec3 col = mix(bot, top, smoothstep(0.0, 1.0, uv.y));
  col = lw_blend(col, uPalette[4], 0.12 * exp(-length((uv - vec2(0.7, 0.75)) * vec2(asp, 1.0)) * 3.0));  // moon haze
  if (ground > 0.5) {
    for (int i = 0; i < 3; i++) {
      float fi = float(i);
      float x = uv.x * asp + lw_pan(0.15 + 0.25 * fi).x;  // workspace parallax: the hazy far hills move least
      float h = 0.3 - 0.09 * fi + 0.07 * lw_fbm(vec2(x * (0.8 + 0.5 * fi) + fi * 5.0, 1.0));
      float hp = pines(x * (1.0 + fi * 0.4) + fi * 3.0, h, fi);
      float haze = 1.0 - fi / 3.0;
      vec3 snowc = mix(uForeground, uBackground, 0.25 + 0.45 * haze);
      vec3 tree = mix(uBackground * 0.6, col, 0.6 * haze);
      if (uv.y < hp && uv.y > h) col = tree;
      if (uv.y <= h) {
        float lit = smoothstep(h - 0.05, h, uv.y);
        col = mix(snowc * 0.8, snowc, lit) * (0.94 + 0.08 * lw_noise(vec2(x * 40.0, uv.y * 90.0)));
        col = mix(col, mix(uBackground, uPalette[4], 0.2), haze * 0.35);
      }
    }
  }
  for (int l = 0; l < 8; l++) {
    if (float(l) >= layers) break;
    float fl = float(l);
    float depth = 1.0 + fl * 0.7;
    float near = 1.0 - fl / max(layers - 1.0, 1.0);        // 1 for the nearest layer
    float s = 10.0 * depth / size;
    vec2 p = vec2(uv.x * asp, uv.y) * s;
    p.y += iTime * 1.6 / sqrt(depth) * s * 0.06;
    p.x -= iTime * wind * s * 0.05 / depth;
    p += lw_pan(0.9 / depth) * s;  // workspace parallax: near flakes pan furthest
    vec2 id = floor(p);
    p.x += sin(p.y * 0.7 + lw_hash(id) * 6.28 + iTime) * 0.25;
    vec2 f = fract(p) - 0.5;
    float h = lw_hash(id + fl * 11.0);
    vec2 c = (lw_hash2(id + 4.0) - 0.5) * 0.5;
    float r = min((0.03 + 0.06 * h) / sqrt(depth) * 1.8, 0.22);
    float blur = 0.15 + 0.7 * near * near;                  // the closest flakes are out of focus
    float flake = smoothstep(r, r * blur, length(f - c)) * step(1.0 - amount * mix(0.6, 0.25, near), h);
    col = mix(col, mix(uForeground, vec3(1.0), 0.3), flake * mix(0.3, 0.75, 1.0 - near * 0.6));
  }
  col *= 1.0 - 0.25 * dot(uv - 0.5, uv - 0.5);
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
