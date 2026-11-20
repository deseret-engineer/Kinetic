// Texels 0..MAXG-1 are the galaxy cores (heavy), texel META remembers which
// encounter is running, and every texel from STAR0 on is a star.
#define MAXG 5
#define META 5
#define STAR0 8
const float G = 0.0004;
int ngal() { return int(clamp(galaxies, 1.0, float(MAXG))); }
float encounter() { return floor(iTime / (cycle * 60.0)) + seed * 1000.0; }
int esalt(float e) { return int(mod(e, 1000000.0)) + int(uEpoch) * 7919; }   // Start over rolls a new one
// core 0 weighs 1, the others less, by how varied the galaxies are
float gmass(int i, float e) { return i == 0 ? 1.0 : mix(1.0, 0.35 + 0.6 * lw_rand(3 + i, esalt(e)), variety); }
// which galaxy a star was born in (fixed for the encounter), by mass
int home(int id, float e) {
  int n = ngal();
  float tot = 0.0;
  for (int i = 0; i < MAXG; i++) if (i < n) tot += gmass(i, e);
  float u = lw_rand(id, esalt(e) + 77) * tot;
  for (int i = 0; i < MAXG - 1; i++) {
    if (i >= n - 1) break;
    u -= gmass(i, e);
    if (u < 0.0) return i;
  }
  return n - 1;
}
