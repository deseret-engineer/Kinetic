// Ants carrying food lay their colony's scent, strongest just after they found it;
// an ant that has just picked food up marks one bite taken (b).
void mainVertex(int id, vec4 state, inout vec4 pos, inout float size, inout vec4 color) {
  float mode = floor(state.w / 1000.0), timer = state.w - mode * 1000.0;
  int c = colony_of(id);
  pos = vec4(state.xy, 0.0, 1.0);
  size = mode > 0.5 && mode < 2.5 || mode > 1.5 ? 1.0 : 0.0;
  float k = exp(-timer * 0.06) * 0.25;
  color = vec4(c == 0 ? k : 0.0, c == 1 ? k : 0.0, mode > 1.5 && mode < 2.5 ? 1.0 : 0.0, c == 2 ? k : 0.0);
}
