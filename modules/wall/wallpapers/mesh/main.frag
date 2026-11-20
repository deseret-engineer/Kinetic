// Mesh: six color blobs wander through a warped space; the result is blended over
// the theme background, lit along its folds, and finished with film grain.
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float asp = iResolution.x / iResolution.y;
  vec2 p = vec2(uv.x * asp, uv.y);
  float t = iTime * speed * 0.06 + (uParallax.x - uParallax.y) * 1.6;  // a workspace switch stirs the blobs, as far as the parallax goes
  // warp the space with slow noise so the blobs flow into ribbons
  vec2 q = p + 0.35 * vec2(lw_fbm(p * 1.2 + t * 0.7), lw_fbm(p * 1.2 - t * 0.6 + 5.2)) - 0.17;
  vec3 acc = vec3(0.0);
  float wsum = 0.0;
  for (int i = 0; i < 6; i++) {
    float fi = float(i);
    vec2 c = vec2(asp * (0.5 + 0.5 * sin(t * (0.7 + 0.13 * fi) + fi * 2.4)), 0.5 + 0.5 * cos(t * (0.6 + 0.17 * fi) + fi * 1.7));
    float d = length(q - c);
    float w = exp(-d * d * 10.0);
    vec3 col = i == 0 ? uAccent : i == 1 ? uPalette[5] : i == 2 ? uPalette[4] : i == 3 ? uPalette[6] : i == 4 ? uPalette[1] : uPalette[3];
    acc += col * w;
    wsum += w;
  }
  vec3 mixc = acc / max(wsum, 1e-3);
  float cover = smoothstep(0.0, 1.0, wsum / (wsum + 0.45));
  vec3 col = mix(uBackground, mixc, cover * strength);
  // silk: light on the slopes of the warp field
  float h = lw_fbm(q * 2.0 + t * 0.3);
  float fold = smoothstep(0.45, 0.75, h) - smoothstep(0.75, 0.95, h);
  col = lw_blend(col, mix(mixc, vec3(1.0), 0.4), fold * folds * 0.18 * cover);
  col *= 1.0 - 0.18 * dot(uv - 0.5, uv - 0.5);
  float g = lw_hash(fragCoord + fract(iTime * 7.0) * 91.0) - 0.5;
  col += g * grain * 0.045;
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
