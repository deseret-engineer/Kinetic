void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 p = fragCoord / iResolution.y * tiles;  // workspace parallax: the engine pans the whole plane (parallax = 1)
  vec2 id = floor(p), f = fract(p);
  float epoch = floor(iTime * flip * 0.25 + lw_hash(id) * 4.0);
  float r = lw_hash(id + epoch * 0.731);
  if (r > 0.5) f.x = 1.0 - f.x;
  float d1 = abs(length(f) - 0.5), d2 = abs(length(f - 1.0) - 0.5);
  float d = min(d1, d2);
  vec2 cf = d1 < d2 ? f : 1.0 - f;
  float ang = atan(cf.y, cf.x) / 1.5708;              // 0..1 along the arc
  float dir = mod(id.x + id.y, 2.0) * 2.0 - 1.0;      // alternate so flows connect
  if (r > 0.5) dir = -dir;
  float flow = fract(ang * dir * 1.0 + iTime * speed * 0.35);
  float px = tiles / iResolution.y;

  vec3 col = uBackground;
  float line = smoothstep(width + px, width - px, d);
  vec3 c = mix(uPalette[4], uAccent, lw_noise(id * 0.3 + iTime * 0.05));
  if (style < 0.5) {
    col = lw_blend(col, mix(uBackground, c, 0.35), line);
    col = lw_blend(col, c, line * smoothstep(0.5, 0.0, abs(flow - 0.5)) * 0.9);
  } else if (style < 1.5) {
    float side = d1 < d2 ? 1.0 : 0.0;
    float inside = step(length(d1 < d2 ? f : f - 1.0), 0.5);
    col = mix(col, mix(uBackground, c, 0.25), mod(inside + side + mod(id.x + id.y, 2.0), 2.0) * 0.6);
    col = lw_blend(col, uForeground, line * 0.3);
  } else {
    float glow = exp(-d * 18.0) * 0.5 + line;
    col = lw_blend(uBackground * 0.6, c, glow * (0.3 + 0.7 * smoothstep(0.6, 0.0, abs(flow - 0.5))));
  }
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
