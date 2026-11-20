vec2 at(vec2 fc, vec2 off) { return texture(iChannel0, (fc + off) / iResolution.xy).xy; } // nearest + repeat

void mainImage(out vec4 o, in vec2 fc) {
  vec2 c = at(fc, vec2(0.0));
  if (iFrame < 2) {                 // the seed parameter has restart = true: changing it lands here

    float b = 0.0;
    for (int i = 0; i < 24; i++) {
      int s = int(seed) * 3 + int(uEpoch) * 101;
      vec2 q = vec2(lw_rand(i, s + 1), lw_rand(i, s + 2)) * iResolution.xy;
      b = max(b, step(length(fc - q), iResolution.y * (0.01 + 0.02 * lw_rand(i, s + 9))));
    }
    o = vec4(1.0, b, 0.0, 1.0);
    return;
  }
  // 3x3 Laplacian: 0.2 on the sides, 0.05 on the corners
  vec2 lap = -c
    + 0.2 * (at(fc, vec2(1.0, 0.0)) + at(fc, vec2(-1.0, 0.0)) + at(fc, vec2(0.0, 1.0)) + at(fc, vec2(0.0, -1.0)))
    + 0.05 * (at(fc, vec2(1.0, 1.0)) + at(fc, vec2(-1.0, -1.0)) + at(fc, vec2(1.0, -1.0)) + at(fc, vec2(-1.0, 1.0)));
  vec2 uv = fc / iResolution.xy;
  float n = variation > 0.0 ? lw_noise(uv * 2.5 + iTime * 0.004) - 0.5 : 0.0;
  float f = feed + n * variation * 0.02, k = kill + n * variation * 0.004;
  k += 0.02 * uReverse;           // running back: a higher kill rate dissolves the pattern from its edges
  float A = c.x, B = c.y, abb = A * B * B;
  A += 1.0 * lap.x - abb + f * (1.0 - A);
  B += 0.5 * lap.y + abb - (k + f) * B;
  if (iPass == 0) {   // the pointer and occasional random drops add B
    if (iMouse.z > 0.0 && length(fc - iMouse.xy) < iResolution.y * 0.015) B = 1.0;
    if (drops > 0.5 && uReverse < 0.5) {
      int slot = int(iTime / 3.0);
      vec2 q = vec2(lw_rand(slot, 5), lw_rand(slot, 6)) * iResolution.xy;
      if (fract(iTime / 3.0) < 0.05 && length(fc - q) < iResolution.y * 0.012) B = 1.0;
    }
    if (uSound.y < 0.15) {        // each sound onset drops a seed somewhere new
      int k = int(uSound.w);
      vec2 q = vec2(lw_rand(k, 15), lw_rand(k, 16)) * iResolution.xy;
      if (length(fc - q) < iResolution.y * (0.008 + 0.02 * uSound.z)) B = 1.0;
    }
  }
  o = vec4(clamp(A, 0.0, 1.0), clamp(B, 0.0, 1.0), 0.0, 1.0);
}
