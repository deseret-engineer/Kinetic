void mainVertex(int id, vec4 state, inout vec4 pos, inout float size, inout vec4 color) {
  if (id < STAR0) { size = 0.0; return; }
  float e = texelFetch(lw_Source, ivec2(META, 0), 0).x;
  float asp = iResolution.x / iResolution.y;
  float a = turn * 0.01745329, ca = cos(a), sa = sin(a);
  vec2 w = vec2(ca * state.x - sa * state.y, sa * state.x + ca * state.y);   // the camera turns...
  w.y *= cos(tilt);                                                          // ...looks at an angle...
  w = w * zoom + vec2(panx, pany);                                           // ...and zooms
  pos.xy = vec2(w.x / asp, w.y) + 0.5;
  if (any(greaterThan(abs(pos.xy - 0.5), vec2(0.52)))) { size = 0.0; return; }  // off screen: skip it
  int g = home(id, e);
  vec3 c = g == 0 ? mix(uPalette[4], uPalette[6], 0.3)
         : g == 1 ? mix(uPalette[5], uAccent, 0.4)
         : g == 2 ? mix(uPalette[2], uPalette[6], 0.4)
         : g == 3 ? mix(uPalette[3], uPalette[1], 0.3)
                  : mix(uPalette[1], uPalette[5], 0.5);
  float young = lw_rand(id, 9);
  c = mix(c, uForeground, young * young * 0.6);   // a scatter of bright white stars
  float b = brightness * 0.12 * (0.4 + young * 1.2) * sqrt(131072.0 / stars) * sqrt(zoom);
  color = vec4(c * b * (1.0 + 1.2 * uSound.x + uSound.z * exp(-uSound.y * 6.0)), 1.0);
  size = max(1.0, iResolution.y / 1200.0) * (young > 0.97 ? 2.0 : 1.0);
}
