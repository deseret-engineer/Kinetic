// An ant: a dot, with a pale crumb when it carries food.
void mainImage(out vec4 c, in vec2 fc) {
  vec2 q = gl_PointCoord * 2.0 - 1.0;
  float a = smoothstep(1.0, 0.6, length(q));
  vec3 col = lw_Color.rgb;
  if (lw_Data.x > 0.5) col = mix(col, vec3(1.0, 0.95, 0.6), smoothstep(0.7, 0.2, length(q)));
  c = vec4(col * a, a);
}
