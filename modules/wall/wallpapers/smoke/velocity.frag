// Advect the projected velocity through itself, then add forces:
// buoyancy from the smoke's heat, vorticity confinement, sources, pointer, gusts.
void mainImage(out vec4 o, in vec2 fc) {
  vec2 px = 1.0 / iResolution.xy, uv = fc * px;
  if (iFrame < 2) { o = vec4(0.0); return; }
  float dt = smoke_dt();
  vec2 u = texture(iChannel0, uv).xy;
  vec2 v = texture(iChannel0, uv - u * px * dt).xy;            // semi-Lagrangian advection

  // vorticity confinement, using last frame's curl stored in vel.z
  float cl = abs(texture(iChannel2, uv - vec2(px.x, 0.0)).z), cr = abs(texture(iChannel2, uv + vec2(px.x, 0.0)).z);
  float cb = abs(texture(iChannel2, uv - vec2(0.0, px.y)).z), ct = abs(texture(iChannel2, uv + vec2(0.0, px.y)).z);
  vec2 l = texture(iChannel0, uv - vec2(px.x, 0.0) * 1.5).xy, r = texture(iChannel0, uv + vec2(px.x, 0.0) * 1.5).xy;
  vec2 b = texture(iChannel0, uv - vec2(0.0, px.y) * 1.5).xy, t = texture(iChannel0, uv + vec2(0.0, px.y) * 1.5).xy;
  float curl = ((r.y - l.y) - (t.x - b.x)) / 3.0;
  vec2 g = vec2(cr - cl, ct - cb);
  if (dot(g, g) > 1e-9) v += normalize(g).yx * vec2(1.0, -1.0) * curl * swirl * 0.18 * dt;

  // hot smoke rises; heat lives in dye.a (dye is at a different resolution: sample by uv)
  float heatv = texture(iChannel1, uv).a;
  v.y += heatv * heat * 0.035 * dt;

  // sources: jets of rising fluid
  float asp = iResolution.x / iResolution.y, pump = music_pump();
  for (int i = 0; i < 6; i++) {
    if (float(i) >= sources) break;
    vec2 d = (uv - source_pos(i)) * vec2(asp, 1.0);
    float w = exp(-dot(d, d) * 900.0);
    v += vec2(sin(iTime * 1.3 + float(i) * 4.0) * 0.6, 1.0) * (jet * 1.4 * pump + puff(i) * 2.0) * w * dt;
  }
  // the pointer pushes the fluid along its motion; a click blows a ring outwards
  if (uPointer.z > 0.05) {
    vec2 d = (uv - uPointer.xy) * vec2(asp, 1.0);
    vec2 motion = (iMouse.xy * px - uPointer.xy) * iResolution.xy * 0.25;
    v += clamp(motion, -8.0, 8.0) * exp(-dot(d, d) * 600.0) * uPointer.z;
    if (uPointer.w < 0.25) v += normalize(d + 1e-5) * 3.0 * exp(-dot(d, d) * 300.0) * (1.0 - uPointer.w * 4.0);
  }
  v.x += wind * 0.01 * dt;
  if (gusts > 0.5) {
    float s = uWorkspace.z - uWorkspace.x;           // target minus smoothed: direction of the switch
    v.x += clamp(s, -1.0, 1.0) * 0.25 * exp(-uWorkspace.y * 2.0) * dt;
  }
  v *= 0.9995;
  v = clamp(v, -12.0, 12.0);
  // closed box: no flow through the walls
  if (fc.x < 1.0 || fc.x > iResolution.x - 1.0) v.x = 0.0;
  if (fc.y < 1.0 || fc.y > iResolution.y - 1.0) v.y = 0.0;
  o = vec4(v, curl, 1.0);
}
