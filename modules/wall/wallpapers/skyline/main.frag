// Skyline: three parallax rows of procedural buildings with windows that light
// in the evening and go dark late at night; a slow trickle of traffic below.
vec4 building(vec2 p, float row, float hr) {
  float w = 0.08 / density;
  float id = floor(p.x / w);
  float h = lw_hash(vec2(id, row));
  float height = 0.12 + 0.35 * h * h + 0.08 * row;
  float lx = fract(p.x / w);
  float style = lw_hash(vec2(id, row + 5.0));
  bool inB = p.y < height && lx > 0.06 && lx < 0.94;
  // rooftops: setbacks, a spire, or an antenna with a blinking light
  if (!inB && style > 0.5 && p.y < height + 0.04 * h && lx > 0.25 && lx < 0.75) inB = true;
  if (!inB && style > 0.8 && abs(lx - 0.5) < 0.03 && p.y < height + 0.04 * h + 0.05) inB = true;
  float beacon = style > 0.8 ? step(length(vec2((lx - 0.5) * w, p.y - (height + 0.04 * h + 0.05))), 0.004) : 0.0;
  if (beacon > 0.0) return vec4(2.0, step(0.5, fract(iTime * 0.5 + h * 3.0)), h, 1.0);
  if (!inB) return vec4(0.0);
  vec2 win = vec2((lx - 0.06) / 0.88 * 5.0, p.y / 0.018);
  vec2 wid = floor(win), wf = fract(win);
  float lit = 0.0;
  if (wf.x > 0.25 && wf.x < 0.75 && wf.y > 0.3 && wf.y < 0.75 && p.y < height - 0.01) {
    float r = lw_hash(wid + vec2(id * 7.0, row * 13.0));
    float on = smoothstep(17.0, 19.5, hr) * (1.0 - smoothstep(23.0, 26.0, hr)) + (1.0 - smoothstep(5.5, 7.5, hr)) * 0.25;
    float flick = step(0.5, lw_hash(wid + floor(iTime * 0.05 + r * 9.0)));
    lit = step(r, lights * on) * mix(1.0, flick, 0.2);
  }
  return vec4(1.0, lit, h, 1.0);
}

vec3 scene(vec2 fragCoord);
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float shore = 0.1;
  vec3 col;
  if (uv.y >= shore) col = scene(fragCoord);
  else {                                     // the river: a rippled, darkened mirror
    float d = shore - uv.y;
    vec2 r = vec2(fragCoord.x + sin(uv.y * 300.0 + iTime * 2.0) * 3.0 * (1.0 + d * 20.0), (shore + d) * iResolution.y);
    float hr0 = hour >= 0.0 ? hour : uDayTime;
    float day0 = smoothstep(6.0, 8.0, hr0) * (1.0 - smoothstep(18.0, 20.0, hr0));
    col = scene(r) * (mix(0.5, 0.75, day0) - d * 1.5);
    col = mix(col, uBackground * 0.7, 0.3);
    col += 0.04 * uForeground * smoothstep(0.995, 1.0, sin(fragCoord.x * 0.05 + lw_noise(vec2(uv.y * 80.0, iTime)) * 4.0));
  }
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}

vec3 scene(vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float asp = iResolution.x / iResolution.y;
  float hr = hour >= 0.0 ? hour : uDayTime;
  float day = smoothstep(6.0, 8.0, hr) * (1.0 - smoothstep(18.0, 20.0, hr));
  vec3 skyTop = mix(uPalette[0], mix(uPalette[4], uBackground, 0.5), day);
  vec3 skyLow = mix(mix(uBackground, uPalette[5], 0.25), mix(uPalette[6], uPalette[15], 0.3), day);
  vec3 col = mix(skyLow, skyTop, uv.y);
  if (stars > 0.5) {
    vec2 g = (fragCoord + lw_pan(0.06) * iResolution.y) / 3.0;  // workspace parallax: stars barely move
    col += uForeground * step(0.997, lw_hash(floor(g))) * (1.0 - day) * smoothstep(0.4, 0.8, uv.y) * 0.7;
  }
  for (int r = 0; r < 3; r++) {
    float row = float(r);
    // workspace parallax: the far row moves least, the near row most
    vec2 p = vec2(uv.x * asp + lw_pan(0.35 + 0.3 * row).x + iTime * 0.002 * (row + 1.0), uv.y - 0.1 - 0.06 + 0.04 + row * -0.02);
    vec4 b = building(p * (1.0 + (2.0 - row) * 0.25), 2.0 - row, hr);
    if (b.x > 1.5) { col = mix(col, vec3(1.0, 0.25, 0.2), b.y); continue; }
    if (b.x > 0.0) {
      vec3 wall = mix(uBackground, uPalette[0], 0.6 + 0.2 * b.z) * (0.55 + 0.2 * row);
      wall *= 0.85 + 0.25 * smoothstep(0.0, 0.5, uv.y);                 // a little light from the sky above
      wall = mix(wall, mix(wall, skyLow, 0.5), (2.0 - row) * 0.25); // haze on distant rows
      float wh = lw_hash(floor(p * 70.0));
      vec3 window = mix(mix(vec3(1.0, 0.8, 0.5), vec3(0.75, 0.85, 1.0), step(0.7, wh)), uPalette[3], 0.35);
      col = mix(wall, window, b.y * (0.6 + 0.2 * row));
    }
  }
  if (traffic > 0.5 && uv.y < 0.15 && uv.y > 0.1) {
    uv.y -= 0.1;
    float lane = step(uv.y, 0.025);
    float x = (uv.x * asp + lw_pan(1.0).x) * 12.0 + iTime * (lane > 0.5 ? 1.2 : -0.9);
    float car = step(0.75, lw_hash(vec2(floor(x), lane))) * smoothstep(0.5, 0.2, abs(fract(x) - 0.5));
    vec3 lightc = lane > 0.5 ? uPalette[15] : uPalette[1];
    col = lw_blend(col, lightc, car * (0.85 - 0.65 * day) * smoothstep(0.012, 0.0, abs(uv.y - (lane > 0.5 ? 0.014 : 0.036))));
  }
  return col;
}
