// Smoke color (rgb, premultiplied density) and heat (a), carried by the flow.
void mainImage(out vec4 o, in vec2 fc) {
  vec2 uv = fc / iResolution.xy;
  if (iFrame < 2) { o = vec4(0.0); return; }
  float dt = smoke_dt();
  vec2 cell = 1.0 / iChannelResolution[1].xy;               // velocity is in sim cells
  vec2 v = texture(iChannel1, uv).xy;
  vec2 back = uv - v * cell * dt;
  // second-order-ish: average the velocity along the step (midpoint rule)
  back = uv - texture(iChannel1, mix(uv, back, 0.5)).xy * cell * dt;
  vec4 d = texture(iChannel0, back);
  float keep = mix(0.96, 0.9985, fade);
  d.rgb *= pow(keep, dt);
  d.a *= pow(0.985, dt);                                      // heat leaves faster than smoke

  float asp = iResolution.x / iResolution.y, pump = music_pump();
  for (int i = 0; i < 6; i++) {
    if (float(i) >= sources) break;
    vec2 q = (uv - source_pos(i)) * vec2(asp, 1.0);
    float w = exp(-dot(q, q) * 1400.0) * dt * (pump + puff(i));
    d.rgb += source_color(i) * w * 0.25;
    d.a += w * 0.6;
  }
  if (uPointer.z > 0.05) {
    vec2 q = (uv - uPointer.xy) * vec2(asp, 1.0);
    float speed = length(iMouse.xy / iResolution.xy - uPointer.xy) * 30.0;
    float w = exp(-dot(q, q) * 2500.0) * min(speed, 1.0) * uPointer.z * dt;
    if (uPointer.w < 0.3) w += exp(-dot(q, q) * 600.0) * (1.0 - uPointer.w / 0.3) * dt;
    d.rgb += uAccent * w * 0.3;
    d.a += w * 0.3;
  }
  o = clamp(d, 0.0, 4.0);
}
