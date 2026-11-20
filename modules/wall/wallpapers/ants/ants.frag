// One ant per texel. Searching ants follow their colony's food scent (and smell
// food directly); ants carrying food head for home by dead reckoning, keeping to
// the scent road where there is one, and lay scent as they go.
void mainImage(out vec4 o, in vec2 fc) {
  ivec2 p = ivec2(fc);
  int i = p.y * 128 + p.x, c = colony_of(i);
  vec4 s = texelFetch(iChannel0, p, 0);
  float ar = iChannelResolution[1].x / iChannelResolution[1].y;   // the screen's aspect
  if (iFrame < 2) {
    float a = lw_rand(i, 1) * 6.2832, r = 0.01 * sqrt(lw_rand(i, 2));
    o = vec4(nest_pos(c) + vec2(cos(a) / ar, sin(a)) * r, a, lw_rand(i, 3) * 30.0);
    return;
  }
  float dt = min(iTimeDelta, 1.0 / 20.0) * speed;
  float mode = floor(s.w / 1000.0), timer = s.w - mode * 1000.0;
  vec2 pos = s.xy;
  float a = s.z;
  if (mode > 2.5) mode = 0.0;          // just dropped food: search again
  else if (mode > 1.5) mode = 1.0;     // just picked some up: carry it

  float sd = 0.022, sa = 0.55;
  vec2 L = vec2(cos(a + sa) / ar, sin(a + sa)) * sd, C = vec2(cos(a) / ar, sin(a)) * sd, R = vec2(cos(a - sa) / ar, sin(a - sa)) * sd;
  vec4 fl = texture(iChannel1, pos + L), fcn = texture(iChannel1, pos + C), fr = texture(iChannel1, pos + R);
  float wander = (lw_rand(i, iFrame) - 0.5) * 2.0;
  float turn;
  if (mode < 0.5) {
    float sl = scent(fl, c) + fl.b * 4.0, sc = scent(fcn, c) + fcn.b * 4.0, sr = scent(fr, c) + fr.b * 4.0;
    turn = sc >= sl && sc >= sr ? 0.0 : sl > sr ? 1.0 : -1.0;
    turn = turn * 4.0 * smoothstep(0.0, 0.02, max(max(sl, sr), sc)) + wander * 3.0;
  } else {
    vec2 h = (nest_pos(c) - pos) * vec2(ar, 1.0);
    float want = atan(h.y, h.x), diff = mod(want - a + 3.14159, 6.28318) - 3.14159;
    float sl = scent(fl, c), sc = scent(fcn, c), sr = scent(fr, c);
    float road = sc >= sl && sc >= sr ? 0.0 : sl > sr ? 1.0 : -1.0;
    turn = diff * 2.0 + road * 1.5 * smoothstep(0.0, 0.05, max(sl, sr)) + wander * 1.5;
  }
  a += turn * dt;
  pos += vec2(cos(a) / ar, sin(a)) * 0.055 * dt;
  // the pointer scatters them
  if (uPointer.z > 0.05) {
    vec2 d = (pos - uPointer.xy) * vec2(ar, 1.0);
    float k = exp(-dot(d, d) * 600.0) * uPointer.z;
    if (k > 0.05) a = mix(a, atan(d.y, d.x), min(k * dt * 20.0, 1.0));
  }
  // sound: a shiver sends them off in new directions
  a += (lw_rand(i, iFrame + 7) - 0.5) * 6.0 * min(uSound.z, 1.0) * exp(-uSound.y * 6.0);
  if (pos.x < 0.0 || pos.x > 1.0) { a = 3.14159 - a; pos.x = clamp(pos.x, 0.0, 1.0); }
  if (pos.y < 0.0 || pos.y > 1.0) { a = -a; pos.y = clamp(pos.y, 0.0, 1.0); }
  timer = min(timer + dt, 999.0);

  vec4 here = texture(iChannel1, pos);
  vec2 hn = (pos - nest_pos(c)) * vec2(ar, 1.0);
  if (mode < 0.5 && here.b > 0.05) { mode = 2.0; timer = 0.0; a += 3.14159; }
  else if (mode > 0.5 && dot(hn, hn) < 0.0002) { mode = 3.0; timer = 0.0; a += 3.14159; }
  else if (mode < 0.5 && timer > 90.0) { vec2 h = -hn; a = atan(h.y, h.x); timer = 0.0; } // lost: wander home
  o = vec4(pos, mod(a, 6.28318), mode * 1000.0 + timer);
}
