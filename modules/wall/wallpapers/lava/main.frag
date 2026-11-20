// Lava Lamp: a smooth-union field of moving blobs, shaded with a fake normal.
float field(vec2 p, float t) {
  float f = 0.0;
  for (int i = 0; i < 16; i++) {
    if (float(i) >= blobs) break;
    float fi = float(i);
    float h = lw_hash(vec2(fi, 3.7));
    float period = 18.0 + 14.0 * h;
    float y = fract(t / period + h) ;
    y = -0.15 + 1.3 * (0.5 - 0.5 * cos(y * 6.2831)); // rise and sink
    float x = 0.5 + 0.32 * sin(t * (0.07 + 0.05 * h) + fi * 2.3) * (0.4 + 0.6 * lw_hash(vec2(fi, 9.1)));
    float r = (0.07 + 0.08 * lw_hash(vec2(fi, 1.3))) * goo;
    vec2 d = p - vec2(x * (iResolution.x / iResolution.y), y);
    f += r * r / (dot(d, d) + r * r * 0.15);
  }
  return f / (1.0 + 0.15 * f); // soft cap keeps the core smooth
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float asp = iResolution.x / iResolution.y;
  vec2 p = vec2(uv.x * asp, uv.y);
  float t = iTime * speed;

  vec3 a, b;
  if (palette < 0.5) { a = uPalette[1]; b = uPalette[3]; }
  else if (palette < 1.5) { a = uPalette[4]; b = uPalette[6]; }
  else if (palette < 2.5) { a = uAccent; b = mix(uAccent, uPalette[15], 0.4); }
  else { a = wax; b = mix(wax, vec3(1.0), 0.35); }

  vec3 col = mix(uBackground, mix(uBackground, a, 0.18), smoothstep(0.0, 1.0, 1.0 - uv.y));
  float f = field(p, t);
  float e = 2.0 / iResolution.y;
  vec2 grad = vec2(field(p + vec2(e, 0.0), t) - f, field(p + vec2(0.0, e), t) - f) / e;
  float edge = smoothstep(0.95, 1.05, f);
  vec3 n = normalize(vec3(-grad * 0.05 * smoothstep(1.0, 1.3, f) * (1.0 - smoothstep(1.6, 3.0, f)), 1.0));
  float light = 0.55 + 0.45 * dot(n, normalize(vec3(-0.4, 0.6, 0.7)));
  vec3 waxCol = mix(a, b, smoothstep(1.0, 2.6, f)) * light;
  waxCol += pow(max(dot(n, normalize(vec3(-0.3, 0.5, 1.0))), 0.0), 24.0) * 0.4;
  col = mix(col, waxCol, edge);
  col = lw_blend(col, a, 0.25 * smoothstep(0.4, 0.95, f) * (1.0 - edge)); // glow

  if (lamp > 0.5) {
    float x = abs(uv.x - 0.5) * asp;
    float glass = smoothstep(0.004, 0.0, abs(x - (0.28 + 0.1 * uv.y)));
    col = lw_blend(col, uForeground, glass * 0.25);
    col = mix(col, uBackground * 0.7, smoothstep(0.28 + 0.1 * uv.y, 0.29 + 0.1 * uv.y, x) * 0.85);
  }
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
