// Plexus: one point per grid cell (from the points buffer), linked to the
// points of the 3x3 neighborhood when they are close enough.
vec2 pointAt(vec2 cell, vec2 mp) {
  vec2 n = iChannelResolution[0].xy;
  vec2 q = cell + texelFetch(iChannel0, ivec2(mod(cell, n)), 0).xy;
  vec2 toM = mp - q;
  return q + toM * 0.25 * uPointer.z * exp(-dot(toM, toM) * 1.5);
}

float segment(vec2 p, vec2 a, vec2 b) {
  vec2 pa = p - a, ba = b - a;
  return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float asp = iResolution.x / iResolution.y;
  vec2 shift = lw_pan(0.5) * density;  // workspace parallax
  vec2 p = vec2(uv.x * asp, uv.y) * density + shift;
  vec2 mp = vec2(uPointer.x * asp, uPointer.y) * density + shift;
  float px = density / iResolution.y;
  vec2 base = floor(p);

  vec2 pts[9];
  for (int i = 0; i < 9; i++) pts[i] = pointAt(base + vec2(float(i % 3) - 1.0, float(i / 3) - 1.0), mp);

  float ink = 0.0, glow = 0.0;
  for (int i = 0; i < 9; i++) {
    float d = length(p - pts[i]);
    float big = lw_hash(floor(pts[i]) + 0.37);
    ink = max(ink, smoothstep((2.5 + 2.5 * big) * px, (0.8 + 1.5 * big) * px, d));
    glow += (0.012 + 0.03 * big * big) / (d * d * 60.0 + 0.4);
    for (int j = i + 1; j < 9; j++) {
      vec2 ab = pts[j] - pts[i];
      float len2 = dot(ab, ab);
      if (len2 > reach * reach) continue;
      float len = sqrt(len2);
      ink = max(ink, (1.0 - len / reach) * lines * smoothstep(1.4 * px, 0.3 * px, segment(p, pts[i], pts[j])));
    }
  }
  vec3 col = mix(uBackground, uPalette[0], 0.3 * uDark);
  col = lw_blend(col, mix(uPalette[4], uPalette[5], uv.x), 0.06 * (1.0 - uv.y));
  col *= 1.0 - 0.25 * dot(uv - 0.5, uv - 0.5);
  vec3 c = color < 0.5 ? uAccent : color < 1.5 ? mix(uPalette[4], uPalette[5], lw_noise(p * 0.3 + iTime * 0.05)) : uForeground;
  col = lw_blend(col, c, ink + glow * 0.3);
  col = lw_blend(col, c, 0.08 * uPointer.z * exp(-length(p - mp) * 1.5));
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
