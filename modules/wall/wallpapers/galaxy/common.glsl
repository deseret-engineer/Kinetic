const float G = 0.0004;
float encounter() { return floor(iTime / (cycle * 60.0)) + seed * 1000.0; }
int esalt(float e) { return int(mod(e, 1000000.0)); }
float mass_b(float e) { return 0.35 + 0.6 * lw_rand(3, esalt(e)); }
// which galaxy a star was born in (fixed for the encounter): 0 = A, 1 = B
float home(int id, float e) { return lw_rand(id, esalt(e) + 77) < 1.0 / (1.0 + mass_b(e)) ? 0.0 : 1.0; }
