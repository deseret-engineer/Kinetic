// Advect the projected velocity through itself, then add forces: vorticity
// confinement, ink bursts, a slow background current, the pointer.
void mainImage(out vec4 o, in vec2 fc) {
  vec2 px = 1.0 / iResolution.xy, uv = fc * px;
  if (iFrame < 2) { o = vec4(0.0); return; }
  float dt = fl_dt();
  vec2 u = texture(iChannel0, uv).xy;
  vec2 v = texture(iChannel0, uv - texture(iChannel0, uv - u * px * dt * 0.5).xy * px * dt).xy; // midpoint backtrace

  // vorticity confinement from last frame's curl (vel.z)
  float cl = abs(texture(iChannel1, uv - vec2(px.x, 0.0)).z), cr = abs(texture(iChannel1, uv + vec2(px.x, 0.0)).z);
  float cb = abs(texture(iChannel1, uv - vec2(0.0, px.y)).z), ct = abs(texture(iChannel1, uv + vec2(0.0, px.y)).z);
  vec2 l = texture(iChannel0, uv - vec2(px.x, 0.0)).xy, r = texture(iChannel0, uv + vec2(px.x, 0.0)).xy;
  vec2 b = texture(iChannel0, uv - vec2(0.0, px.y)).xy, t = texture(iChannel0, uv + vec2(0.0, px.y)).xy;
  float curl = 0.5 * ((r.y - l.y) - (t.x - b.x));
  vec2 g = vec2(cr - cl, ct - cb);
  if (dot(g, g) > 1e-10) v += normalize(g).yx * vec2(1.0, -1.0) * curl * swirl * 0.25 * dt;

  Burst bs[3];
  fl_bursts(bs);
  for (int i = 0; i < 3; i++) {
    if (bs[i].w <= 0.0) continue;
    vec2 d = fl_delta(uv, bs[i].pos);
    v += bs[i].dir * force * 3.2 * bs[i].w * exp(-dot(d, d) * 1400.0) * dt;
  }

  // background current: a slowly turning large-scale flow
  float tt = iTime * 0.05;
  vec2 cur = vec2(sin(6.2832 * uv.y + tt * 3.0 + sin(tt)), cos(6.2832 * uv.x - tt * 2.0));
  v += cur * stir * 0.004 * dt;

  if (uPointer.z > 0.05) {
    vec2 d = fl_delta(uv, uPointer.xy);
    vec2 motion = (iMouse.xy * px - uPointer.xy) * iResolution.xy * 0.3;
    v += clamp(motion, -10.0, 10.0) * exp(-dot(d, d) * 500.0) * uPointer.z;
    if (uPointer.w < 0.3) v += normalize(d + 1e-5) * 4.0 * exp(-dot(d, d) * 250.0) * (1.0 - uPointer.w / 0.3);
  }
  v *= pow(0.9985, dt);
  v = clamp(v, -14.0, 14.0);
  o = vec4(v, curl, 1.0);
}
