void mainImage(out vec4 c, in vec2 fc) {
  vec2 q = gl_PointCoord * 2.0 - 1.0;
  c = lw_Color * max(0.0, 1.0 - dot(q, q) * 0.7);
}
