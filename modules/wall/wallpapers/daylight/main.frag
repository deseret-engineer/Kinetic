// Daylight: uDayTime (local hours) places the sun. Four ranges of mountains fade
// into the haze, clouds catch the light at dawn and dusk, and a lake mirrors it all.
// Almost nothing changes from one second to the next, so the manifest asks for a
// frame every two seconds.
vec3 sky_at(vec2 uv, vec2 p, float asp, float ang, float elev, float day, float dusk, out vec3 bot) {
  vec3 top = mix(uPalette[0], mix(uPalette[4], uBackground, 0.25 + 0.3 * uDark), day);
  bot = mix(uBackground, mix(uPalette[6], uPalette[15], 0.45), day);
  bot = mix(bot, mix(uPalette[1], uPalette[3], 0.55), dusk * 0.8);
  vec3 col = mix(bot, top, pow(clamp(uv.y, 0.0, 1.0), 0.75));
  vec2 g = uv * iResolution.xy / (iResolution.y / 60.0);
  float h = lw_hash(floor(g));
  col += uPalette[15] * step(0.86, h) * smoothstep(0.14, 0.0, length(fract(g) - 0.5 - (lw_hash2(floor(g) + 3.0) - 0.5) * 0.7)) *
         (1.0 - day) * smoothstep(0.3, 0.7, uv.y) * 0.8;
  vec2 sun = vec2(asp * (0.5 - 0.42 * cos(ang)), 0.2 + 0.6 * elev);
  float sd = length(p - sun);
  vec3 sunCol = mix(uPalette[1], uPalette[3], smoothstep(0.0, 0.5, elev));
  col += sunCol * (0.45 * exp(-sd * 5.0) + 0.25 * exp(-sd * 1.6)) * smoothstep(-0.3, 0.1, elev);
  col = mix(col, mix(sunCol, vec3(1.0), 0.55), smoothstep(0.045, 0.04, sd));
  vec2 moon = vec2(asp * (0.5 + 0.42 * cos(ang)), 0.2 - 0.6 * elev);
  float md = length(p - moon);
  float crescent = smoothstep(0.038, 0.034, md) * smoothstep(0.03, 0.036, length(p - moon - vec2(0.016, 0.007)));
  col += uPalette[15] * (crescent * 0.9 + 0.12 * exp(-md * 9.0)) * (1.0 - day);
  // clouds: lit from the sun's side, glowing at dawn and dusk
  float c = lw_fbm(vec2(p.x * 1.4, p.y * 4.0) + vec2(iTime * 0.004, 0.0));
  float cm = smoothstep(0.52, 0.78, c) * smoothstep(0.35, 0.6, uv.y) * (1.0 - smoothstep(0.85, 1.0, uv.y));
  vec3 cloud = mix(mix(uBackground, uPalette[0], 0.6), mix(uForeground, vec3(1.0), 0.3), day * 0.85);
  cloud = mix(cloud, mix(sunCol, uPalette[1], 0.4), dusk * 0.7 * smoothstep(1.2, 0.0, abs(p.x - sun.x)));
  return mix(col, cloud, cm * 0.75);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float asp = iResolution.x / iResolution.y;
  float hh = hour >= 0.0 ? hour : uDayTime;
  float ang = (hh - 6.0) / 12.0 * 3.14159265;
  float elev = sin(ang);
  float day = smoothstep(-0.12, 0.3, elev);
  float dusk = exp(-elev * elev * 14.0);
  float lake = 0.16;
  bool water = uv.y < lake;
  vec2 q = water ? vec2(uv.x + 0.002 * sin(uv.y * 400.0), 2.0 * lake - uv.y) : uv;   // mirror below the shore
  vec2 p = vec2(q.x * asp, q.y);
  vec3 bot;
  vec3 col = sky_at(q, p, asp, ang, elev, day, dusk, bot);
  for (int i = 0; i < 4; i++) {
    float fi = float(i);
    float base = 0.46 - 0.08 * fi;
    float n = lw_fbm(vec2(p.x * (0.9 + 0.5 * fi) + fi * 9.0, fi));
    float peaks = 1.0 - abs(lw_noise(vec2(p.x * (2.2 + fi), fi * 7.0)) * 2.0 - 1.0);   // ridged: sharp crests
    float ridge = base - 0.05 + (0.34 - 0.06 * fi) * (n - 0.35) + (0.08 - 0.015 * fi) * peaks * peaks;
    float haze = 1.0 - fi / 3.5;
    vec3 land = mix(mix(uBackground, uPalette[0], 0.5) * (0.6 + 0.15 * fi), bot, haze * 0.75) * (0.75 + 0.25 * day);
    land *= 0.9 + 0.12 * smoothstep(ridge - 0.12, ridge, q.y);                         // lighter toward the crest
    col = mix(col, land, smoothstep(ridge + 0.002, ridge - 0.002, q.y));
  }
  if (water) {
    col = mix(col, uBackground, 0.35) * 0.85;
    col += uForeground * 0.05 * smoothstep(0.97, 1.0, sin(uv.y * 900.0 / (lake - uv.y + 0.02) * 0.02 + lw_noise(uv * vec2(30.0, 200.0)) * 3.0));
    col = mix(col, uBackground * 0.8, smoothstep(lake, 0.0, uv.y) * 0.5);
  }
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
