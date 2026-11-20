// Draw the cells: lit sand with grain, glassy water, plants, glowing fire.
vec4 at(ivec2 p) { return texelFetch(iChannel0, clamp(p, ivec2(0), ivec2(iChannelResolution[0].xy) - 1), 0); }

void mainImage(out vec4 o, in vec2 fc) {
  vec2 uv = fc / iResolution.xy;
  vec2 g = uv * iChannelResolution[0].xy;
  ivec2 p = ivec2(g);
  vec4 c = at(p);
  int t = ctype(c);
  vec3 bg = mix(uBackground * 0.85, mix(uBackground, uForeground, 0.12), uv.y);
  vec3 col = bg;
  float above_empty = ctype(at(p + ivec2(0, 1))) == EMPTY ? 1.0 : 0.0;
  if (t == SAND) {
    col = sand_color(c.g) * (1.0 - 0.38 * c.b);
    col *= 1.0 + 0.18 * above_empty;
    // depth: darker the deeper it is buried
    float depth = 0.0;
    for (int k = 1; k <= 4; k++) depth += ctype(at(p + ivec2(0, k * 3))) == SAND ? 0.25 : 0.0;
    col *= 1.0 - 0.15 * depth;
  } else if (t == WATER) {
    vec3 w = colors > 0.5 && colors < 1.5 ? mix(uPalette[4], uAccent, 0.3) : vec3(0.25, 0.55, 0.9);
    float surf = above_empty;
    float shim = 0.5 + 0.5 * sin(g.x * 0.7 + iTime * 2.0 + g.y * 0.3);
    col = mix(bg, w, 0.6) + surf * 0.25 * shim;
  } else if (t == STONE) {
    col = mix(vec3(0.38, 0.38, 0.42), mix(uBackground, uForeground, 0.4), 0.4) * (0.8 + 0.3 * c.g) * (1.0 + 0.2 * above_empty);
  } else if (t == STEM || t == TIP) {
    col = vec3(0.25, 0.55, 0.22) * (0.85 + 0.3 * fract(c.g * 7.0));
  } else if (t == LEAF) {
    col = vec3(0.35, 0.7, 0.25) * (0.85 + 0.3 * fract(c.g * 5.0));
  } else if (t == FLOWER) {
    col = colors > 0.5 && colors < 1.5 ? uPalette[1 + int(c.g * 6.0) % 6] : 0.6 + 0.4 * cos(6.2832 * (c.g + vec3(0.0, 0.33, 0.67)));
  } else if (t == SEED) {
    col = vec3(0.55, 0.4, 0.22);
  } else if (t == WILT) {
    col = mix(vec3(0.55, 0.45, 0.25), vec3(0.3, 0.5, 0.2), c.b * 0.5);
  } else if (t == FIRE) {
    col = mix(vec3(1.0, 0.25, 0.05), vec3(1.0, 0.9, 0.4), c.b * (0.6 + 0.4 * c.g)) * 1.3;
  }
  // a soft glow from nearby fire
  float glow = 0.0;
  for (int k = 0; k < 6; k++) {
    vec2 q = vec2(cos(float(k) * 1.047), sin(float(k) * 1.047)) * 4.0;
    glow += ctype(at(p + ivec2(q))) == FIRE ? 1.0 : 0.0;
  }
  col += vec3(1.0, 0.45, 0.1) * glow * 0.06;
  // cells, gently separated
  vec2 f = fract(g);
  float edge = (t == SAND || t == STONE) ? 0.92 + 0.08 * smoothstep(0.0, 0.12, min(min(f.x, f.y), min(1.0 - f.x, 1.0 - f.y))) : 1.0;
  o = vec4(lw_dither(col * edge, fc), 1.0);
}
