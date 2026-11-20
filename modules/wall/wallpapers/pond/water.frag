// Two-step wave equation: r = height now, g = height a step ago.
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 px = 1.0 / iResolution.xy, uv = fragCoord * px;
  if (iFrame < 2) { fragColor = vec4(0.0); return; }
  vec2 s = texture(iChannel0, uv).rg;
  float n = texture(iChannel0, uv + vec2(px.x, 0.0)).r + texture(iChannel0, uv - vec2(px.x, 0.0)).r +
            texture(iChannel0, uv + vec2(0.0, px.y)).r + texture(iChannel0, uv - vec2(0.0, px.y)).r;
  float h = (n * 0.5 - s.g) * damping;
  float asp = iResolution.x / iResolution.y;

  // raindrops
  float k = floor(iTime * 30.0);
  for (int i = 0; i < 2; i++) {
    vec2 r = lw_hash2(vec2(k, float(i) * 7.0));
    if (lw_hash(vec2(k, 3.0 + float(i))) < rain * 0.35) {
      float d = length((uv - r) * vec2(asp, 1.0));
      h -= 0.9 * smoothstep(0.006, 0.0, d);
    }
  }
  // sound: each onset drops a stone, its size following how sharp the sound was
  if (uSound.y < 0.05) {
    vec2 r = lw_hash2(vec2(uSound.w, 41.0));
    h -= (0.8 + 1.5 * uSound.z) * smoothstep(0.004 + 0.01 * uSound.z, 0.0, length((uv - r) * vec2(asp, 1.0)));
  }
  h -= 0.02 * uSound.x * lw_hash(fragCoord + iTime) * step(0.997, lw_hash(fragCoord * 0.37 + floor(iTime * 30.0)));
  // pointer
  float pd = length((uv - uPointer.xy) * vec2(asp, 1.0));
  if (iMouse.z > 0.0) h -= 1.5 * smoothstep(0.012, 0.0, pd) * step(fract(iTime * 4.0), 0.25);
  else if (trail > 0.5) h -= 0.06 * uPointer.z * smoothstep(0.008, 0.0, pd);
  fragColor = vec4(clamp(h, -2.0, 2.0), s.r, 0.0, 1.0);
}
