// Sky (following the clock), sun or moon, clouds, stars, then the garden layer,
// lit by the time of day; fireflies come out after dark.
void mainImage(out vec4 o, in vec2 fc) {
  vec2 uv = fc / iResolution.xy;
  float asp = iResolution.x / iResolution.y;
  vec3 col = sky_color(uv.y);
  float e = sun_elev(), day = daylight();

  // sun crosses left to right over the day; the moon at night
  float h = sun_hour();
  vec2 sp = vec2(fract((h - 6.0) / 24.0) * 2.0 - 0.0, 0.0);
  vec2 sun = vec2(0.1 + 0.8 * clamp((h - 6.0) / 12.0, 0.0, 1.0), 0.35 + 0.5 * max(e, 0.0));
  vec2 moon = vec2(0.1 + 0.8 * fract((h - 18.0 + 24.0) / 24.0 * 2.0), 0.35 + 0.45 * max(-e, 0.0));
  vec2 ds = (uv - sun) * vec2(asp, 1.0), dm = (uv - moon) * vec2(asp, 1.0);
  vec3 sunc = colors < 0.5 ? vec3(1.0, 0.92, 0.75) : mix(uForeground, uAccent, 0.3);
  col += sunc * (smoothstep(0.03, 0.026, length(ds)) + 0.35 * exp(-length(ds) * 7.0)) * smoothstep(-0.05, 0.05, e);
  float mo = smoothstep(0.03, 0.026, length(dm)) * (1.0 - smoothstep(0.03, 0.026, length(dm - vec2(0.012, 0.006))));
  col += vec3(0.85, 0.88, 1.0) * (mo * 0.9 + 0.08 * exp(-length(dm) * 12.0)) * (1.0 - day);

  // stars
  vec2 g = fc / 3.0;
  float st = step(0.997, lw_hash(floor(g))) * (0.5 + 0.5 * sin(iTime * 2.0 + lw_hash(floor(g) + 3.0) * 40.0));
  col += vec3(st) * (1.0 - day) * smoothstep(0.3, 0.7, uv.y) * 0.8;

  // soft drifting clouds
  float cl = lw_fbm(vec2(uv.x * asp * 1.6 + iTime * 0.006, uv.y * 4.0));
  float cm = smoothstep(0.55, 0.8, cl) * smoothstep(0.35, 0.75, uv.y);
  vec3 cloud = mix(vec3(0.15, 0.17, 0.25), mix(vec3(1.0), vec3(1.0, 0.7, 0.55), exp(-e * e * 30.0)), day);
  if (colors > 0.5) cloud = mix(uBackground, uForeground, 0.25 + 0.4 * day);
  col = mix(col, cloud, cm * 0.6);

  // the garden, lit
  vec4 gd = texture(iChannel0, uv);
  col = col * (1.0 - gd.a) + gd.rgb * light_color();

  // fireflies
  if (fireflies > 0.5 && day < 0.9) {
    float glow = 0.0;
    for (int i = 0; i < 24; i++) {
      float fi = float(i);
      vec2 p = vec2(fract(lw_hash(vec2(fi, 1.0)) + 0.03 * sin(iTime * 0.3 + fi)), 0.05 + 0.3 * lw_hash(vec2(fi, 2.0)));
      p += vec2(0.02 * sin(iTime * 0.7 + fi * 2.0), 0.02 * sin(iTime * 0.9 + fi * 3.1));
      float blink = smoothstep(0.3, 1.0, sin(iTime * (0.8 + 0.5 * lw_hash(vec2(fi, 3.0))) + fi * 5.0));
      vec2 d = (uv - p) * vec2(asp, 1.0);
      glow += blink * (exp(-dot(d, d) * 40000.0) + 0.15 * exp(-dot(d, d) * 2000.0));
    }
    col += vec3(0.85, 1.0, 0.45) * glow * (1.0 - day);
  }
  o = vec4(lw_dither(col, fc), 1.0);
}
