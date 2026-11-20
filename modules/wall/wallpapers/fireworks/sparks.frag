void mainImage(out vec4 c, in vec2 fc) {
  vec2 q = gl_PointCoord * 2.0 - 1.0;
  c = lw_Color * (0.45 + 0.55 * max(0.0, 1.0 - dot(q, q))); // small points stay solid
}
