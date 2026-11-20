// Plant life: grow, bloom, wilt, die, and reseed empty slots from a mature
// neighbor in the same layer, with small mutations. Row 0: x, age, alive, wait.
// Rows 1-2: genes. Every row of a slot makes the same random choices.
vec4 P(int s, int r) { return texelFetch(iChannel0, ivec2(s, r), 0); }

float rnd(int s, int salt) { return lw_rand(s * 7919 + iFrame * 131 + int(uEpoch) * 977, salt); }

int pick_kind(int L, float r) {
  vec4 w = L == 0 ? vec4(0.10, 0.30, 0.45, 0.15) : L == 1 ? vec4(0.30, 0.30, 0.15, 0.25) : vec4(0.45, 0.15, 0.04, 0.36);
  return r < w.x ? 0 : r < w.x + w.y ? 1 : r < w.x + w.y + w.z ? 2 : 3;
}

void wild_genes(int s, int L, out vec4 ga, out vec4 gb) {
  ga = vec4(float(pick_kind(L, rnd(s, 21))), 0.8 + 0.45 * rnd(s, 22), rnd(s, 23), 0.8 + 0.4 * rnd(s, 24));
  gb = vec4(0.5 + rnd(s, 25), (rnd(s, 26) - 0.5) * 0.25, floor(rnd(s, 27) * 100000.0), 0.8 + 0.45 * rnd(s, 28));
}

bool crowded(int L, float x, float sep) {
  for (int j = 0; j < PER_LAYER; j++) {
    vec4 q = P(L * PER_LAYER + j, 0);
    if (q.z > 0.5 && abs(q.x - x) < sep) return true;
  }
  return false;
}

void mainImage(out vec4 o, in vec2 fc) {
  int s = int(fc.x), row = int(fc.y), L = s / PER_LAYER;
  vec4 st = P(s, 0), ga = P(s, 1), gb = P(s, 2);
  float sep = 0.03 / layer_scale(L);

  if (iFrame < 2) {                       // a fresh garden, already part-grown
    wild_genes(s, L, ga, gb);
    float x = (float(s % PER_LAYER) + 0.15 + 0.7 * rnd(s, 2)) / float(PER_LAYER);
    Shape sh = shape_of(ga, gb);
    bool alive = rnd(s, 1) < (ga.x > 1.5 && ga.x < 2.5 ? 0.5 : 0.85);
    st = vec4(x, sh.grow * (0.3 + 0.9 * rnd(s, 3)), alive ? 1.0 : 0.0, rnd(s, 4) * 2.0);
    o = row == 0 ? st : row == 1 ? ga : gb;
    return;
  }

  float dt = min(iTimeDelta, 0.5) / 60.0 * pace * mix(0.25, 1.0, daylight());
  if (st.z > 0.5) {
    Shape sh = shape_of(ga, gb);
    st.y += dt;
    if (st.y > sh.life) st = vec4(st.x, 0.0, 0.0, 0.3 + 1.5 * rnd(s, 5));
  } else {
    st.w -= dt;
    // a click plants a seed in the nearest free slot of the front layer
    bool clicked = uPointer.z > 0.05 && uPointer.w < iTimeDelta && L == 2;
    int first = -1;
    if (clicked)
      for (int j = 0; j < PER_LAYER; j++)
        if (P(2 * PER_LAYER + j, 0).z < 0.5) { first = 2 * PER_LAYER + j; break; }
    if (clicked && first == s) {
      wild_genes(s, L, ga, gb);
      if (ga.x > 1.5 && ga.x < 2.5) ga.x = 0.0;
      st = vec4(clamp(uPointer.x, 0.01, 0.99), 0.0, 1.0, 0.0);
    } else if (st.w <= 0.0) {
      int parent = -1, start = int(rnd(s, 6) * float(PER_LAYER));
      for (int j = 0; j < PER_LAYER; j++) {
        int k = L * PER_LAYER + (start + j) % PER_LAYER;
        vec4 q = P(k, 0);
        if (q.z > 0.5 && q.y > shape_of(P(k, 1), P(k, 2)).grow * 0.8) { parent = k; break; }
      }
      bool wildseed = rnd(s, 7) < wild * 0.5;
      if (parent >= 0 && !wildseed) {                      // a seed from a neighbor, slightly mutated
        vec4 pa = P(parent, 1), pb = P(parent, 2);
        float x = P(parent, 0).x + (rnd(s, 8) - 0.5) * 0.25;
        x = x < 0.0 ? -x : x > 1.0 ? 2.0 - x : x;
        if (!crowded(L, x, sep)) {
          ga = vec4(pa.x, clamp(pa.y * (0.92 + 0.16 * rnd(s, 9)), 0.6, 1.5), fract(pa.z + (rnd(s, 10) - 0.5) * 0.1),
                    clamp(pa.w * (0.9 + 0.2 * rnd(s, 11)), 0.5, 1.6));
          gb = vec4(clamp(pb.x * (0.85 + 0.3 * rnd(s, 12)), 0.2, 2.0), clamp(pb.y + (rnd(s, 13) - 0.5) * 0.06, -0.3, 0.3),
                    floor(rnd(s, 14) * 100000.0), clamp(pb.w * (0.92 + 0.16 * rnd(s, 15)), 0.6, 1.6));
          if (rnd(s, 16) < 0.04) ga.x = float(pick_kind(L, rnd(s, 17)));
          st = vec4(x, 0.0, 1.0, 0.0);
        } else st.w = 0.15;
      } else if (wildseed || parent < 0) {                 // a seed blown in from outside
        float x = rnd(s, 18);
        if (!crowded(L, x, sep) && (wildseed || rnd(s, 19) < 0.3)) {
          wild_genes(s, L, ga, gb);
          st = vec4(x, 0.0, 1.0, 0.0);
        } else st.w = 0.5;
      }
    }
  }
  o = row == 0 ? st : row == 1 ? ga : gb;
}
