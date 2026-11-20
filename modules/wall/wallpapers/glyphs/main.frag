// Glyphs: every cell draws a random 3x5 bitmap, mirrored so it reads as a character.
float glyph(vec2 f, float seed) {
  vec2 m = (f - vec2(0.2, 0.14)) / vec2(0.6, 0.72);
  if (m.x < 0.0 || m.x > 1.0 || m.y < 0.0 || m.y > 1.0) return 0.0;
  vec2 g = floor(m * vec2(3.0, 5.0));
  g.x = abs(g.x - 1.0);
  return step(0.48, lw_hash(g + seed * 91.7));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  float size = iResolution.y / rows;
  vec2 cell = vec2(size * 0.62, size);
  vec2 id = floor(fragCoord / cell), f = fract(fragCoord / cell);
  float row = rows - id.y;
  vec3 col = mix(uBackground, uPalette[0], 0.5);
  float ink = 0.0, head = 0.0;
  for (int k = 0; k < 2; k++) { // two streams per column
    float cs = lw_hash(vec2(id.x, 1.0 + float(k) * 9.0));
    float span = rows * (1.5 + cs);
    float pos = fract(iTime * speed * (0.03 + 0.05 * cs) + cs * 13.0) * span;
    float d = pos - mod(row, span);  // each stream repeats down the column: endless when panned vertically too
    float len = 0.07 + 0.12 * lw_hash(vec2(id.x, 5.0 + float(k)));
    ink = max(ink, d > 0.0 ? exp(-d * len) : 0.0);
    head = max(head, smoothstep(1.0, 0.0, abs(d - 0.5)));
  }
  float rate = 0.4 + 3.0 * lw_hash(id + 4.0);
  float gl = glyph(f, lw_hash(id + floor(iTime * rate)));
  vec3 trail = mix(uAccent, uPalette[2], 0.25 * lw_hash(vec2(id.x, 3.0)));
  col = lw_blend(col, trail, gl * ink * 0.9);
  col = lw_blend(col, uPalette[15], gl * head);
  col = lw_blend(col, trail, ink * 0.05); // faint column glow
  fragColor = vec4(col, 1.0);
}
