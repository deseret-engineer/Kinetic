// Topography: a shaded relief map of procedurally generated land. Each terrain is
// a height function over map coordinates; contours every 1/lines, bold index
// contours, hillshading from the north-west, a coastline and water below sea level.
//
// The seed shifts the noise domain and rotates every octave, so each seed is a
// different landscape. Drift moves the view over a fixed map; morph slowly
// cross-fades the noise into a neighboring slice, so the land reshapes in place.

mat2 ROT;            // per-seed octave rotation
vec2 OFF;            // per-seed domain offset
float W;             // morph phase

// noise that morphs: two slices of the noise field, cross-faded by the phase
float n2(vec2 p) {
  if (morph <= 0.0) return lw_noise(p);
  float k = floor(W), f = smoothstep(0.0, 1.0, fract(W));
  return mix(lw_noise(p + vec2(37.1, 17.7) * k), lw_noise(p + vec2(37.1, 17.7) * (k + 1.0)), f);
}

// fractal octave i: the broad octaves morph; fine detail stays put, which no
// one can see, and costs half as much
float oct(vec2 p, int i) { return i < 3 ? n2(p) : lw_noise(p); }

// two octaves only: for broad shapes and faint texture, where full detail would be wasted
float n2o(vec2 p) { return (n2(p) + 0.5 * n2(ROT * p * 2.03 + 5.1)) / 1.5; }

float fbm(vec2 p) {
  float a = 0.5, s = 0.0, norm = 0.0;
  for (int i = 0; i < 8; i++) {
    if (float(i) >= detail) break;
    s += a * oct(p, i);
    norm += a;
    p = ROT * p * 2.03 + vec2(17.3, 9.1);
    a *= roughness;
  }
  return s / norm;
}

// ridged multifractal: sharp crests where the noise crosses its midline, with
// each octave weighted by the one before so the peaks gather the fine detail
float ridged(vec2 p) {
  float a = 0.5, s = 0.0, norm = 0.0, w = 1.0;
  for (int i = 0; i < 8; i++) {
    if (float(i) >= detail) break;
    float r = 1.0 - abs(oct(p, i) * 2.0 - 1.0);
    r *= r;
    s += a * r * w;
    norm += a;
    w = clamp(r * 2.0, 0.0, 1.0);
    p = ROT * p * 2.03 + vec2(17.3, 9.1);
    a *= roughness;
  }
  return s / norm;
}

float billow(vec2 p) {
  float a = 0.5, s = 0.0, norm = 0.0;
  for (int i = 0; i < 8; i++) {
    if (float(i) >= detail) break;
    s += a * abs(oct(p, i) * 2.0 - 1.0);
    norm += a;
    p = ROT * p * 2.03 + vec2(17.3, 9.1);
    a *= roughness;
  }
  return s / norm;
}

// Features on a jittered grid. Every point within reach of the pixel adds its
// own shape, so nothing jumps where one cell's territory meets the next.
// Shapes must fade out within 1.1 cells (3x3 search) or 0.65 cells (2x2).
float islands(vec2 p) {                  // a cone of land around each center
  vec2 g = floor(p), f = fract(p);
  float m = 0.0;
  for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
      vec2 o = vec2(float(i), float(j));
      vec2 c = o + 0.15 + 0.7 * lw_hash2(g + o + OFF);
      float id = lw_hash(g + o + OFF + 3.1);
      float r = 0.5 + 0.55 * id;
      m = max(m, (0.35 + 0.65 * id) * smoothstep(r, 0.0, length(c - f)));
    }
  return m;
}

float craters(vec2 p) {                  // bowls with raised rims; a third of the cells are empty
  // a crater reaches at most 0.63 cells from its center, and no center sits
  // within 0.15 of a cell edge, so only the 2x2 cells nearest the pixel matter
  vec2 g = floor(p), f = fract(p), base = step(0.5, f) - 1.0;
  float s = 0.0;
  for (int j = 0; j <= 1; j++)
    for (int i = 0; i <= 1; i++) {
      vec2 o = base + vec2(float(i), float(j));
      float id = lw_hash(g + o + OFF + 3.1);
      if (id < 0.35) continue;
      vec2 c = o + 0.15 + 0.7 * lw_hash2(g + o + OFF);
      float r = 0.14 + 0.28 * fract(id * 13.7);
      float d = length(c - f) / r;
      if (d > 1.5) continue;
      float bowl = d < 1.0 ? (d * d - 1.0) * 0.6 : 0.0;
      float e = max(0.0, 1.0 - (d - 1.0) * (d - 1.0) * 4.0);          // the rim, fading to nothing at d = 1.5
      s += bowl + e * e * 0.35;
    }
  return s;
}

float raw_height(vec2 q) {
  vec2 p = q;
  if (warp > 0.0) p += warp * 1.2 * (vec2(lw_noise(q * 0.8 + 5.2) + 0.5 * lw_noise(q * 1.6 + 2.2),
                                              lw_noise(q * 0.8 + 1.3) + 0.5 * lw_noise(q * 1.6 + 7.9)) / 1.5 - 0.5); // domain warp
  if (terrain < 0.5) return fbm(p);
  if (terrain < 1.5) return ridged(p * 0.8) + n2o(p * 0.35 + 11.0) * 0.3;  // crests over massifs and valleys
  if (terrain < 2.5) return billow(p * 0.8) * 1.15;
  if (terrain < 3.5) {                                           // mesas: flat-topped buttes on a plain
    float n = fbm(p * 0.8);
    float wall = smoothstep(0.486, 0.514, n), cap = smoothstep(0.558, 0.578, n);
    return 0.12 * n2o(p * 2.5 + 4.0) + 0.5 * wall * wall + 0.3 * cap * cap;  // a talus slope below each cliff
  }
  if (terrain < 4.5) {                                           // islands: land pooled around scattered centers
    vec2 wob = vec2(n2(p * 1.7), n2(p * 1.7 + 9.0)) - 0.5;          // coasts with bays and capes
    return n2o(p * 1.6) * 0.35 + islands(p * 1.1 + wob * 0.7);
  }
  if (terrain < 5.5) {                                           // craters on a dusty plain
    float h = 0.25 * n2o(p) + craters(p * 1.2) * 0.5 + craters(p * 3.1 + 7.0) * 0.22;
    return detail > 4.5 ? h + craters(p * 7.0 + 3.0) * 0.08 : h;  // the smallest craters come with detail 5+
  }
  if (terrain < 6.5) {                                           // dunes: long asymmetric ridges across the wind
    float a = OFF.x * 0.05;
    vec2 dir = vec2(cos(a), sin(a));
    float ph = dot(p, dir) * 14.0 + fbm(p * 0.7) * 6.0;
    float dune = pow(0.5 + 0.5 * sin(ph + 0.7 * sin(ph)), 1.5);            // gentle windward side, steep slip face
    return 0.25 + 0.45 * dune * (0.4 + n2o(p * 0.6)) + 0.12 * n2o(p * 0.4);
  }
  if (terrain < 7.5) {                                           // canyons: a high plateau cut by winding gorges
    // gorges follow the midline of one smooth noise, about one per map unit,
    // nudged by a little extra noise so they meander
    vec2 mw = vec2(lw_noise(p * 1.3 + 3.0) + 0.5 * lw_noise(p * 2.6 + 8.0), lw_noise(p * 1.3 + 6.0) + 0.5 * lw_noise(p * 2.6 + 1.0)) / 1.5 - 0.5;
    float river = abs(n2(p * 0.8 + mw * 1.1 + 31.0) - 0.5);
    float side = abs(n2(p * 1.6 + mw * 0.8 + 57.0) - 0.5);          // side canyons, only near a gorge
    float cut = 0.5 * smoothstep(0.035, 0.012, river) + 0.5 * smoothstep(0.1, 0.04, river);
    cut = max(cut, 0.45 * smoothstep(0.03, 0.008, side) * smoothstep(0.16, 0.07, river));
    float x = ((fbm(p * 0.7) - 0.455) * 0.4 - 0.3 * cut) * 14.0, k = floor(x), f = fract(x);  // rock strata: benches and cliffs
    return (k + smoothstep(0.2, 0.95, f)) / 14.0;
  }
  // glacial: smooth U-shaped valleys between rugged peaks
  float r = ridged(p * 0.9);
  return pow(r, 1.3) + 0.12 * n2o(p * 0.4) + 0.05 * n2(p * 3.0);
}

// Each terrain's raw heights, measured (median and the spread of the middle half)
// and mapped to a common range, so sea level and contour spacing mean the same
// for every algorithm: sea 0.5 floods about half the map, 0.4 about a third.
float height(vec2 q) {
  int t = int(terrain + 0.5);
  float med[9] = float[9](0.449, 0.535, 0.378, 0.080, 0.496, 0.127, 0.457, -0.022, 0.402);
  float iqr[9] = float[9](0.149, 0.306, 0.212, 0.510, 0.384, 0.102, 0.243, 0.125, 0.314);
  float h = 0.5 + (raw_height(q) - med[t]) / iqr[t] * 0.23;
  // squeeze the rare highs and lows into the map's range instead of cutting them
  // off flat, so peaks keep their slopes, contours and shading. The limits sit
  // half a contour inside 0 and 1, so no line runs along a flattened summit.
  float lo = 0.5 / lines, hi = 1.0 - lo, k = 0.15;
  if (h > hi - k) return hi - k + k * tanh((h - hi + k) / k);
  if (h < lo + k) return lo + k - k * tanh((lo + k - h) / k);
  return h;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float asp = iResolution.x / iResolution.y;
  float s = seed > 0.5 ? seed : uEpoch;
  float ra = lw_rand(int(s), 1) * 6.2832;
  ROT = mat2(cos(0.5 + ra * 0.1), sin(0.5 + ra * 0.1), -sin(0.5 + ra * 0.1), cos(0.5 + ra * 0.1));
  OFF = vec2(lw_rand(int(s), 2), lw_rand(int(s), 3)) * 200.0;
  W = iTime * morph * 0.02;

  vec2 p = vec2(uv.x * asp, uv.y) * 1.6 / zoom;  // workspace parallax: the engine pans the whole map (parallax = 1)
  float a = radians(heading);
  p += vec2(cos(a), sin(a)) * iTime * speed * 0.018;                     // drift: the map slides past
  float h = height(p + OFF);
  // slope from screen-space derivatives: free, instead of two more height samples
  vec2 grad = vec2(dFdx(h), dFdy(h)) / (1.6 / zoom / iResolution.y);
  float shade = clamp(0.6 + dot(normalize(vec3(-grad * 0.45, 1.0)), normalize(vec3(-0.6, 0.7, 0.5))) * 0.55, 0.0, 1.3);

  float v = h * lines;
  float w = max(fwidth(v), 1e-4);
  float k = floor(v + 0.5);
  float isMajor = major > 0.5 ? step(abs(mod(k, major)), 0.5) : 0.0;
  float line = smoothstep(w * (1.0 + isMajor), 0.0, abs(fract(v + 0.5) - 0.5));
  line *= smoothstep(0.55, 0.25, w);         // lines closer than ~2-4 px would alias: let the shading show the cliff

  vec3 col = uBackground;
  bool wet = h < sea;
  if (fill > 0.5) {
    float hh = smoothstep(sea, 0.95, h);
    vec3 low = mix(uBackground, uPalette[2], 0.22), mid = mix(uBackground, uPalette[3], 0.25), high = mix(uBackground, uForeground, 0.3);
    vec3 land = hh < 0.5 ? mix(low, mid, hh * 2.0) : mix(mid, high, hh * 2.0 - 1.0);
    land = mix(land, mix(land, uPalette[2], 0.1), floor(v) / lines);      // gentle terraces
    land = mix(land, mix(uBackground, uForeground, 0.5), 0.55 * smoothstep(0.86, 0.96, h));  // pale summits
    col = land * mix(1.0, shade, 0.85);
    if (wet) {                                                            // water, deeper is darker
      float depth = (sea - h) / max(sea, 1e-3);
      col = mix(mix(uBackground, uPalette[4], 0.3), uBackground * 0.8, smoothstep(0.0, 0.5, depth));
      col += uPalette[4] * 0.12 * smoothstep(0.03, 0.0, depth);           // shallows
    }
  }
  float coast = sea > 0.0 ? smoothstep(fwidth(h) * 1.5, 0.0, abs(h - sea)) : 0.0;
  vec3 ink = mix(mix(uForeground, uBackground, 0.55), uAccent, isMajor);
  if (wet) line *= 0.35;
  col = lw_blend(col, ink, line * (0.4 + 0.4 * isMajor));
  col = lw_blend(col, uPalette[4], coast * 0.7 * coastline);
  if (grid > 0.5) {                                                       // map grid
    vec2 g = abs(fract(uv * vec2(asp, 1.0) * 5.0 + 0.5) - 0.5) / fwidth(uv.y * 5.0);
    col = mix(col, mix(col, uForeground, 0.06), smoothstep(1.2, 0.0, min(g.x, g.y)));
  }
  vec2 su = (fragCoord - lw_Shift) / iResolution.xy;                     // the screen, not the panned map
  col *= 1.0 - 0.2 * dot(su - 0.5, su - 0.5);
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
