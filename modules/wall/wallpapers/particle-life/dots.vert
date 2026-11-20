void mainVertex(int id, vec4 state, inout vec4 pos, inout float size, inout vec4 color) {
  int s = species_of(id);
  color = vec4(species_color(s) * 1.1, 1.0);
  size = size * iResolution.y / 600.0; // dots render at half resolution
}
