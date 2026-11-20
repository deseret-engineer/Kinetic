// Cell: r = type, g = color variant, b = moisture / growth / fire life, a = 1 for
// sand (what the fill monitor measures).
#define EMPTY 0
#define SAND 1
#define WATER 2
#define STONE 3
#define STEM 4
#define SEED 5
#define FIRE 6
#define FLOWER 7
#define LEAF 8
#define TIP 9
#define WILT 10
#define WALL 11

int ctype(vec4 c) { return int(c.r * 255.0 + 0.5); }
vec4 cell(int t, float g, float b) { return vec4(float(t) / 255.0, g, b, t == SAND ? 1.0 : 0.0); }

vec3 sand_color(float g) {
  int k = int(g * 255.0) / 32;   // which spout it came from
  float v = fract(g * 255.0 / 32.0);
  vec3 c;
  if (colors < 0.5) {
    vec3 d[8] = vec3[8](vec3(0.93, 0.79, 0.55), vec3(0.85, 0.6, 0.38), vec3(0.75, 0.42, 0.28), vec3(0.96, 0.9, 0.78),
                        vec3(0.66, 0.55, 0.42), vec3(0.9, 0.7, 0.45), vec3(0.6, 0.35, 0.3), vec3(0.98, 0.85, 0.6));
    c = d[k];
  } else if (colors < 1.5) {
    c = uPalette[1 + k % 6];
  } else {
    c = 0.6 + 0.4 * cos(6.2832 * (float(k) * 0.13 + vec3(0.0, 0.33, 0.67)));
  }
  return c * (0.85 + 0.25 * v);
}
