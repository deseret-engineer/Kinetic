void mainVertex(int id, vec4 state, inout vec4 pos, inout float size, inout vec4 color) {
  if (id < 3) { size = 0.0; return; }
  float e = texelFetch(lw_Source, ivec2(2, 0), 0).x;
  float asp = iResolution.x / iResolution.y;
  vec2 w = state.xy;
  w.y *= cos(tilt);                              // seen at an angle
  pos.xy = vec2(w.x / asp, w.y) + 0.5;
  bool inB = home(id, e) > 0.5;
  vec3 c = inB ? mix(uPalette[5], uAccent, 0.4) : mix(uPalette[4], uPalette[6], 0.3);
  float young = lw_rand(id, 9);
  c = mix(c, uForeground, young * young * 0.6);   // a scatter of bright white stars
  float b = brightness * 0.12 * (0.4 + young * 1.2) * sqrt(131072.0 / stars);
  color = vec4(c * b * (1.0 + 1.2 * uSound.x + uSound.z * exp(-uSound.y * 6.0)), 1.0);
  size = max(1.0, iResolution.y / 1200.0) * (young > 0.97 ? 2.0 : 1.0);
}
