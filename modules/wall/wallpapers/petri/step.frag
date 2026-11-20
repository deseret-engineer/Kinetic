// One Gray-Scott step. r = chemical U, g = chemical V.
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 px = 1.0 / iResolution.xy, uv = fragCoord * px;
  if (iFrame < 1) {
    float seed = step(0.975, lw_hash(floor(fragCoord / 5.0) + 3.0 + uEpoch * 11.3));
    fragColor = vec4(1.0 - 0.5 * seed, 0.25 * seed, 0.0, 1.0);
    return;
  }
  vec2 s = texture(iChannel0, uv).rg;
  vec2 lap = -s;
  lap += 0.2 * (texture(iChannel0, uv + vec2(px.x, 0.0)).rg + texture(iChannel0, uv - vec2(px.x, 0.0)).rg +
                texture(iChannel0, uv + vec2(0.0, px.y)).rg + texture(iChannel0, uv - vec2(0.0, px.y)).rg);
  lap += 0.05 * (texture(iChannel0, uv + px).rg + texture(iChannel0, uv - px).rg +
                 texture(iChannel0, uv + vec2(px.x, -px.y)).rg + texture(iChannel0, uv + vec2(-px.x, px.y)).rg);
  // feed and kill drift across the surface so different patterns coexist
  float F = feed + 0.008 * (uv.y - 0.5) + 0.003 * sin(iTime * 0.05 + uv.x * 6.0);
  float K = kill + 0.004 * (uv.x - 0.5) + 0.02 * uReverse; // reverse: dissolve back
  float u = s.r, v = s.g, uvv = u * v * v;
  u += lap.r - uvv + F * (1.0 - u);
  v += 0.5 * lap.g + uvv - (F + K) * v;

  float asp = iResolution.x / iResolution.y;
  // the pointer seeds new growth
  v += 0.4 * uPointer.z * smoothstep(0.02, 0.0, length((uv - uPointer.xy) * vec2(asp, 1.0)));
  // sound onsets seed a colony somewhere new
  vec2 sq = lw_hash2(vec2(uSound.w, 3.7));
  v += step(uSound.y, 0.15) * uSound.z * 0.5 * smoothstep(0.03, 0.0, length((uv - sq) * vec2(asp, 1.0)));
  // every few seconds a patch is wiped, so the culture never settles for good
  float k = floor(iTime / 5.0);
  vec2 c = lw_hash2(vec2(k, 7.0));
  float wipe = smoothstep(0.1, 0.07, length((uv - c) * vec2(asp, 1.0))) * step(fract(iTime / 5.0), 0.04);
  v *= 1.0 - wipe;
  u = mix(u, 1.0, wipe);
  v += wipe * 0.3 * smoothstep(0.75, 0.95, lw_noise(fragCoord / 3.0 + k));
  fragColor = vec4(clamp(u, 0.0, 1.0), clamp(v, 0.0, 1.0), 0.0, 1.0);
}
