// Ink, carried by the flow. Sampled with a 9-tap Catmull-Rom filter so the folds
// stay sharp instead of blurring away as they are advected every frame.
vec4 sample_cr(vec2 uv) {
  vec2 res = iResolution.xy, pos = uv * res - 0.5, f = fract(pos), c = floor(pos) + 0.5;
  vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f)), w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
  vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f)), w3 = f * f * (-0.5 + 0.5 * f);
  vec2 w12 = w1 + w2, o12 = w2 / w12;
  vec2 t0 = (c - 1.0) / res, t3 = (c + 2.0) / res, t12 = (c + o12) / res;
  vec4 s = texture(iChannel0, vec2(t0.x, t0.y)) * w0.x * w0.y + texture(iChannel0, vec2(t12.x, t0.y)) * w12.x * w0.y +
           texture(iChannel0, vec2(t3.x, t0.y)) * w3.x * w0.y + texture(iChannel0, vec2(t0.x, t12.y)) * w0.x * w12.y +
           texture(iChannel0, vec2(t12.x, t12.y)) * w12.x * w12.y + texture(iChannel0, vec2(t3.x, t12.y)) * w3.x * w12.y +
           texture(iChannel0, vec2(t0.x, t3.y)) * w0.x * w3.y + texture(iChannel0, vec2(t12.x, t3.y)) * w12.x * w3.y +
           texture(iChannel0, vec2(t3.x, t3.y)) * w3.x * w3.y;
  return max(s, 0.0);
}

void mainImage(out vec4 o, in vec2 fc) {
  vec2 uv = fc / iResolution.xy;
  if (iFrame < 2) { o = vec4(0.0); return; }
  float dt = fl_dt();
  vec2 cell = 1.0 / iChannelResolution[1].xy;
  vec2 back = uv - texture(iChannel1, uv).xy * cell * dt;
  back = uv - texture(iChannel1, mix(uv, back, 0.5)).xy * cell * dt;
  vec4 d = sample_cr(back);
  d *= pow(mix(0.985, 0.9993, linger), dt);

  Burst bs[3];
  fl_bursts(bs);
  for (int i = 0; i < 3; i++) {
    if (bs[i].w <= 0.0) continue;
    vec2 q = fl_delta(uv, bs[i].pos);
    d.rgb += bs[i].col * ink * 0.6 * bs[i].w * exp(-dot(q, q) * 3500.0) * dt;
  }
  if (uPointer.z > 0.05) {
    vec2 q = fl_delta(uv, uPointer.xy);
    float speed = length(iMouse.xy / iResolution.xy - uPointer.xy) * 30.0;
    float w = exp(-dot(q, q) * 2500.0) * min(speed, 1.0) * uPointer.z * dt;
    if (uPointer.w < 0.3) w += exp(-dot(q, q) * 700.0) * (1.0 - uPointer.w / 0.3) * dt;
    d.rgb += fl_ink(fract(iTime * 0.05)) * ink * w * 0.4;
  }
  o = vec4(min(d.rgb, vec3(3.0)), 1.0);
}
