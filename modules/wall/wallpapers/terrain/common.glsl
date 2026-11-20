const int GX = 200, GZ = 80;
const float CELL = 0.5, FAR = float(GZ) * CELL;

// sun elevation (-1..1) and warmth (0 = white, 1 = golden) from the clock or the chosen preset
vec2 sun_state() {
  float h = sun < 0.5 ? uDayTime : sun < 1.5 ? 8.0 : sun < 2.5 ? 13.0 : sun < 3.5 ? 19.2 : 23.0;
  float elev = sin((h - 6.5) / 13.0 * 3.14159);
  return vec2(elev, 1.0 - smoothstep(0.05, 0.5, elev));
}

vec3 sky_color(float y) {
  vec2 s = sun_state();
  vec3 day = mix(uBackground * 1.3 + 0.08, uPalette[4] * 0.6 + 0.25, 0.5 * (1.0 - uDark * 0.5));
  vec3 dusk = mix(uBackground, vec3(1.0, 0.45, 0.25), 0.55);
  vec3 night = uBackground * 0.7;
  vec3 horizon = s.x < 0.0 ? mix(dusk, night, smoothstep(0.0, -0.3, s.x)) : mix(day, dusk, s.y);
  vec3 top = mix(horizon, uBackground * (s.x > 0.0 ? 0.9 : 0.5), 0.7);
  return mix(horizon, top, smoothstep(0.0, 0.6, y));
}
