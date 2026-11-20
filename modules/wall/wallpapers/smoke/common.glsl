// Shared by every Smoke pass.
float smoke_dt() { return clamp(iTimeDelta * 60.0, 0.0, 3.0); }

// Where source i sits right now (uv), and the color it emits.
vec2 source_pos(int i) {
  float fi = float(i), n = max(sources, 1.0);
  float x = (fi + 0.5) / n + 0.12 * sin(iTime * (0.07 + 0.013 * fi) + fi * 2.4) / n * 2.0;
  return vec2(x, 0.06);
}

vec3 source_color(int i) {
  if (colors < 0.5) return uPalette[1 + i % 6];
  if (colors < 1.5) return mix(uAccent, uForeground, float(i % 3) * 0.25);
  if (colors < 2.5) return mix(vec3(1.0, 0.35, 0.08), vec3(1.0, 0.75, 0.3), float(i % 2));
  return mix(uForeground, vec3(lw_luma(uForeground)), 0.5);
}

// sound: louder pumps harder; each onset gives one source a puff
float music_pump() { return 1.0 + 2.5 * uSound.x; }
float puff(int i) { return int(uSound.w) % 6 == i ? uSound.z * exp(-uSound.y * 5.0) * 4.0 : 0.0; }
