// `point` is the point size; `size` is the Particle size setting
void mainVertex(int id, vec4 state, inout vec4 pos, inout float point, inout vec4 color) {
  int s = species_of(id);
  color = vec4(species_color(s) * 1.1, 1.0);
  point = size * iResolution.y / 600.0; // dots render at half resolution
}
