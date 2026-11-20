// Scent diffuses a little and evaporates; food is eaten bite by bite and new
// clumps appear now and then.
void mainImage(out vec4 o, in vec2 fc) {
  vec2 px = 1.0 / iResolution.xy, uv = fc * px;
  if (iFrame < 2) { o = vec4(0.0); return; }
  float dt = min(iTimeDelta, 0.1);
  vec4 f = texture(iChannel0, uv);
  vec4 avg = (texture(iChannel0, uv + vec2(px.x, 0.0)) + texture(iChannel0, uv - vec2(px.x, 0.0)) +
              texture(iChannel0, uv + vec2(0.0, px.y)) + texture(iChannel0, uv - vec2(0.0, px.y))) * 0.25;
  vec4 dep = texture(iChannel1, uv);
  vec4 sc = mix(f, avg, 0.1) * pow(mix(0.6, 0.97, evaporate), dt) + dep;
  float bits = max(f.b - dep.b * 0.25, 0.0);
  // a new clump every so often, somewhere away from the nests
  float period = 22.0 / max(food, 0.05);
  float k = floor(iTime / period), age = iTime - k * period;
  vec2 c = vec2(0.08 + 0.84 * lw_rand(int(k), 11 + int(uEpoch)), 0.1 + 0.8 * lw_rand(int(k), 12));
  vec2 d = (uv - c) * vec2(iResolution.x / iResolution.y, 1.0);
  float speck = step(0.35, lw_hash(fc + k));
  if (age < 1.5 && food > 0.01) bits += speck * smoothstep(0.045, 0.02, length(d)) * dt * 2.0;
  if (uPointer.z > 0.05 && uPointer.w < 0.4) {
    vec2 q = (uv - uPointer.xy) * vec2(iResolution.x / iResolution.y, 1.0);
    bits += speck * smoothstep(0.04, 0.02, length(q)) * dt * 4.0;
  }
  o = vec4(min(sc.r, 4.0), min(sc.g, 4.0), min(bits, 1.0), min(sc.a, 4.0));
}
