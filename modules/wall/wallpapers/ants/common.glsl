// Ant Colony: shared code.
int colony_of(int i) { return i % int(colonies + 0.5); }

vec2 nest_pos(int c) {
  int n = int(colonies + 0.5);
  if (n == 1) return vec2(0.5, 0.45);
  if (n == 2) return c == 0 ? vec2(0.27, 0.42) : vec2(0.73, 0.58);
  return c == 0 ? vec2(0.22, 0.3) : c == 1 ? vec2(0.78, 0.32) : vec2(0.5, 0.75);
}

vec3 colony_color(int c) { return c == 0 ? uPalette[1] : c == 1 ? uPalette[4] : uPalette[5]; }

float scent(vec4 f, int c) { return c == 0 ? f.r : c == 1 ? f.g : f.a; }

float asp() { return iResolution.x / iResolution.y; }
