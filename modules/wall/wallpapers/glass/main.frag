// Rain on Glass: "@background" is whatever still Omarchy is set to. Outside the
// drops it is sampled from a blurry mip level; inside them it is sharp and refracted.
vec3 scene(vec2 uv, float lod) {
  return textureLod(iChannel0, lw_cover(uv, iChannelResolution[0].xy, iResolution.xy), lod).rgb;
}

// xy = refraction offset, z = drop coverage, w = cleared trail
vec4 sliding(vec2 uv, float t, float scale) {
  float cellAsp = iResolution.x / (2.0 * iResolution.y);
  vec2 g = uv * scale * vec2(2.0, 1.0);
  float n = lw_hash(vec2(floor(g.x), scale));
  g.y += t * (0.18 + 0.3 * n) + n * 10.0;
  vec2 id = floor(g), f = fract(g) - 0.5;
  float h = lw_hash(id + scale * 3.1);
  if (h < 0.35) return vec4(0.0);
  float tt = t * 0.9 + h * 6.2831;
  float x = (h - 0.675) * 0.9 + 0.05 * sin(g.y * 5.0 + h * 9.0);
  float y = -sin(tt + sin(tt + sin(tt) * 0.5)) * 0.38; // sticks, then slips
  vec2 dp = (f - vec2(x, y)) * vec2(cellAsp, 1.0);
  dp.y *= dp.y > 0.0 ? 0.75 : 1.0; // teardrop: stretched tail
  float r = 0.07 + 0.05 * fract(h * 17.0);
  float drop = smoothstep(r, r * 0.55, length(dp));
  float trail = smoothstep(r * 0.7, r * 0.3, abs(f.x - x) * cellAsp) * smoothstep(y, y + 0.05, f.y) * smoothstep(0.5, y, f.y);
  return vec4(dp / r * drop, drop, trail);
}

vec3 beads(vec2 uv, float t) {
  vec2 g = uv * vec2(iResolution.x / iResolution.y, 1.0) * 26.0;
  vec2 id = floor(g);
  float h = lw_hash(id);
  vec2 c = lw_hash2(id + 2.0) * 0.6 + 0.2;
  float life = fract(t * 0.04 + h * 5.0);
  float r = (0.1 + 0.16 * fract(h * 31.0)) * smoothstep(0.0, 0.1, life) * smoothstep(1.0, 0.7, life);
  vec2 dp = fract(g) - c;
  float drop = smoothstep(r, r * 0.5, length(dp)) * step(0.45, h);
  return vec3(dp / max(r, 1e-3) * drop, drop);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float t = iTime * speed;
  vec4 a = sliding(uv, t, 5.0), b = sliding(uv * 1.37 + 3.1, t * 1.15, 8.0);
  vec3 s = beads(uv, t);
  vec2 off = a.xy + b.xy * 0.7 + s.xy * 0.5;
  float drops = clamp(a.z + b.z + s.z, 0.0, 1.0);
  float clear = clamp(drops + 0.6 * max(a.w, b.w), 0.0, 1.0);
  vec3 col = scene(uv - off * 0.045, mix(fog, 0.0, clear));
  col = mix(col, uBackground, 0.12) * (0.82 + 0.1 * clear);
  col += drops * 0.1 * smoothstep(0.2, 0.9, off.y - off.x); // catch-light on each drop
  col *= 1.0 - 0.25 * dot(uv - 0.5, uv - 0.5) * 2.0;
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
