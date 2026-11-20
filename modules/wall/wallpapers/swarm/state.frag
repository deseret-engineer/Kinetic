void mainImage(out vec4 o, in vec2 fc) {
  ivec2 p = ivec2(fc);
  int id = p.x + p.y * int(iResolution.x);
  vec4 s = texelFetch(iChannel0, p, 0);
  vec2 res = iChannelResolution[1].xy;
  float asp = res.x / res.y, dt = min(iTimeDelta, 0.05);
  if (iFrame < 2 || lw_rand(id, iFrame) < 0.0015) {   // seed, and slowly recycle so nothing gets stuck
    o = vec4(lw_rand(id, 11 + iFrame), lw_rand(id, 23 + iFrame), 0.0, 0.0);
    return;
  }
  vec2 flow = texture(iChannel1, s.xy).xy * speed;
  flow *= 1.0 + 2.5 * uSound.x;                      // sound: louder flows faster
  vec2 v = mix(s.zw, flow, 1.0 - exp(-dt * mix(30.0, 1.5, inertia)));
  if (uPointer.z > 0.05 && pointer < 2.5) {
    vec2 d = (uPointer.xy - s.xy) * vec2(asp, 1.0);
    float r2 = dot(d, d) + 0.002;
    vec2 pull = d / r2 * 0.012;
    if (pointer > 1.5) pull = vec2(-d.y, d.x) / r2 * 0.02 + d * 0.3;
    else if (pointer > 0.5) pull = -pull * 1.5;
    v += pull * uPointer.z * dt * 8.0;
  }
  if (uPointer.w < 0.4) {                             // a click blasts everything nearby outwards
    vec2 d = (s.xy - abs(iMouse.zw) / res) * vec2(asp, 1.0);
    v += normalize(d + 1e-6) * exp(-dot(d, d) * 40.0) * (0.4 - uPointer.w) * 4.0;
  }
  if (uSound.y < 0.25) {                              // an onset: a shock wave from a random point
    vec2 c = vec2(lw_rand(int(uSound.w), 1), lw_rand(int(uSound.w), 2));
    vec2 d = (s.xy - c) * vec2(asp, 1.0);
    v += normalize(d + 1e-6) * exp(-dot(d, d) * 6.0) * uSound.z * (0.25 - uSound.y) * 6.0;
  }
  v = clamp(v, -2.0, 2.0);
  s.xy = fract(s.xy + v * dt * vec2(1.0 / asp, 1.0));
  o = vec4(s.xy, v);
}
