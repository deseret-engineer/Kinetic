void mainImage(out vec4 c, in vec2 fc) {
  vec2 q = gl_PointCoord * 2.0 - 1.0;
  float r = dot(q, q);
  c = lw_Color * smoothstep(1.0, 0.2, r);
}
