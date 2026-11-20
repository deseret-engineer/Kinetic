void mainVertex(int id, vec4 state, inout vec4 pos, inout float size, inout vec4 color) {
  float mode = floor(state.w / 1000.0);
  int c = colony_of(id);
  pos = vec4(state.xy, 0.0, 1.0);
  size = max(2.0, iResolution.y / 360.0);
  vec3 body = mix(colony_color(c), uForeground, 0.25);
  lw_data = vec4(mode > 0.5 ? 1.0 : 0.0, cos(state.z), sin(state.z), 0.0);
  color = vec4(body, 1.0);
}
