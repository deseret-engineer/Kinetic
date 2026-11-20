void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 cell = floor(fragCoord);
  vec2 h = lw_hash2(cell);
  float t = iTime * speed;
  vec2 q = 0.5 + 0.4 * vec2(sin(t * (0.3 + h.x) + h.y * 6.28), cos(t * (0.25 + h.y) + h.x * 6.28));
  fragColor = vec4(q, 0.0, 1.0);
}
