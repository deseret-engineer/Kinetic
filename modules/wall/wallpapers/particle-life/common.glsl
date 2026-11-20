int species_of(int id) { return id % int(species); }
vec3 species_color(int s) { return uPalette[1 + s % 6] * (s >= 6 ? 0.7 : 1.0) + (s >= 6 ? uAccent * 0.3 : vec3(0.0)); }
// attraction of species a toward b, in -1..1: the rules of this little universe
float attraction(int a, int b) { return lw_rand(a * 8 + b, int(seed) * 977 + 13) * 2.0 - 1.0; }
