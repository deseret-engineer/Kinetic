// Vertex layout: three layers back to front, each [6 ground][16 plants]; each
// plant [127 x 18 stem][127 x 12 leaf][127 x 6 flower]. lw_data = (u, v, type, extra).
vec2 corner(int k) {
  return vec2(k == 1 || k == 2 || k == 4 ? 1.0 : 0.0, k == 2 || k == 4 || k == 5 ? 1.0 : 0.0);
}

float rh(int s, int i, int salt) { return lw_rand(s * 4099 + i, salt); }

void mainVertex(int id, vec4 state, inout vec4 pos, inout float size, inout vec4 color) {
  float asp = iResolution.x / iResolution.y, px = 1.0 / iResolution.y;
  int L = id / BLOCK, r = id % BLOCK;
  float haze = L == 0 ? 0.38 : L == 1 ? 0.16 : 0.0;
  vec3 hz = sky_color(0.3);
  if (r < 6) {                                         // the ground of this layer
    vec2 c = corner(r);
    float top = layer_base(L) + 0.05;
    pos = vec4(c.x, c.y * top, 0.0, 1.0);
    lw_data = vec4(c.x, c.y * top, 3.0, float(L));
    vec3 g = colors < 0.5 ? vec3(0.2, 0.33, 0.12) : mix(uBackground, uPalette[2], 0.4);
    color = vec4(mix(g * (0.8 + 0.12 * float(L)), hz, haze), 1.0);
    return;
  }
  r -= 6;
  if (r < GRASS * 3) {                                  // a meadow blade: one triangle, swaying
    int j = r / 3, v = r % 3;
    float fj = float(j), sc = layer_scale(L);
    float x = (fj + lw_rand(j, 40 + L)) / float(GRASS);
    float hgt = (0.012 + 0.03 * pow(lw_rand(j, 50 + L), 2.0)) * sc;
    float lean = (lw_rand(j, 60 + L) - 0.5) * 0.6 - wind_at(x) * 0.35;
    vec2 base = vec2(x * asp, ground_y(L, x) - 0.006 * sc);
    float w = 0.0022 * sc;
    vec2 P = v == 2 ? base + vec2(sin(lean), cos(lean)) * hgt : base + vec2(v == 0 ? -w : w, 0.0);
    pos = vec4(P.x / asp, P.y, 0.0, 1.0);
    lw_data = vec4(0.0, 0.0, 4.0, 0.0);
    vec3 g = colors < 0.5 ? hsv(0.22 + 0.08 * lw_rand(j, 70 + L), 0.6, 0.35 + 0.3 * lw_rand(j, 80 + L))
                          : mix(uBackground, uPalette[2], 0.3 + 0.4 * lw_rand(j, 80 + L));
    color = vec4(mix(g * (v == 2 ? 1.25 : 0.7), hz, haze), 1.0);
    return;
  }
  r -= GRASS * 3;
  int s = L * PER_LAYER + r / PV, k = r % PV;
  vec4 st = texelFetch(iChannel1, ivec2(s, 0), 0);
  if (st.z < 0.5) { size = 0.0; return; }
  int type, i, sub, cn;
  if (k < NB * 18) { type = 0; i = k / 18; sub = (k % 18) / 6; cn = k % 6; }
  else if (k < NB * 30) { k -= NB * 18; type = 1; i = k / 12; sub = (k % 12) / 6; cn = k % 6; }
  else { k -= NB * 30; type = 2; i = k / 6; sub = 0; cn = k % 6; }
  vec4 b0 = texelFetch(iChannel0, ivec2(2 * i, s), 0);
  if (b0.w <= 0.0) { size = 0.0; return; }
  vec4 b1 = texelFetch(iChannel0, ivec2(2 * i + 1, s), 0);
  vec4 ga = texelFetch(iChannel1, ivec2(s, 1), 0), gb = texelFetch(iChannel1, ivec2(s, 2), 0);
  Shape sh = shape_of(ga, gb);
  Life lf = life_of(st.y, sh);
  int d = depth_of(i);
  float sc = layer_scale(L) * ga.y;
  vec2 root = vec2(st.x * asp, ground_y(L, st.x) - 0.004 * sc);
  vec2 c = corner(cn);
  float len = b0.w, cur = b1.x;
  vec2 P;
  float leafhue = 0.24 + 0.1 * fract(ga.z * 7.3);
  vec3 green = hsv(leafhue, 0.62, 0.55);
  if (colors > 0.5) green = mix(green, uPalette[2], 0.45);

  if (type == 0) {                                     // a slice of the branch, as a tapered ribbon
    float sA = len * (float(sub) + c.y) / 3.0;
    vec2 ctr = arc_at(b0.xy, b0.z, cur, sA);
    float a = b0.z + cur * sA;
    float w = mix(b1.y, b1.z, sA / len);
    float wd = max(w, 1.5 * px);
    P = root + ctr + vec2(-sin(a), cos(a)) * (c.x * 2.0 - 1.0) * (wd * 0.5 + px);
    lw_data = vec4((c.x * 2.0 - 1.0) * (wd * 0.5 + px) / (wd * 0.5), sA / len, 0.0, wd / px);
    vec3 bark = colors < 0.5 ? vec3(0.3, 0.22, 0.16) : mix(uBackground, uForeground, 0.35);
    float woody = sh.kind == 2 ? smoothstep(4.5, 1.5, float(d)) : sh.kind == 1 ? smoothstep(3.5, 1.0, float(d)) : 0.0;
    vec3 col = mix(green * 0.85, bark, woody);
    if (sh.kind == 3) col = mix(hsv(leafhue + 0.02, 0.55, 0.62), vec3(0.75, 0.65, 0.4), lf.wilt);
    else col = mix(col, vec3(0.45, 0.38, 0.25), lf.wilt * 0.5 * (1.0 - woody));
    color = vec4(col, min(w / (1.5 * px), 1.0));
  } else if (type == 1) {                              // two leaves along the branch
    float grown = smoothstep(0.3, 1.0, b1.w) * (1.0 - smoothstep(0.75, 1.0, lf.wilt));
    if (d < sh.leafd || sh.leaf <= 0.0 || grown <= 0.0 || (sh.kind == 0 && d > 1)) { size = 0.0; return; }
    float t = sub == 0 ? 0.45 : 0.95;
    float side = (sub == 0 ? 1.0 : -1.0) * (rh(s, i, 1) < 0.5 ? 1.0 : -1.0);
    vec2 at = arc_at(b0.xy, b0.z, cur, len * t);
    float a = b0.z + cur * len * t + side * (0.7 + 0.4 * rh(s, i + sub * 200, 2)) + lf.wilt * -0.8 * side;
    float ll = sh.leaf * sc * grown * (0.75 + 0.5 * rh(s, i + sub * 200, 3));
    float wr = sh.kind == 0 ? 0.2 : sh.kind == 1 ? 0.6 : 0.62;
    vec2 dir = vec2(cos(a), sin(a)), nrm = vec2(-dir.y, dir.x);
    float u = c.x * 2.0 - 1.0;
    P = root + at + dir * (c.y * 1.08 - 0.04) * ll + nrm * u * ll * wr * 0.5;
    lw_data = vec4(u, c.y * 1.08 - 0.04, 1.0, 0.0);
    vec3 leaf = green * (0.8 + 0.4 * rh(s, i + sub * 200, 4));
    vec3 autumn = hsv(0.02 + 0.11 * rh(s, i, 5), 0.85, 0.8);
    color = vec4(mix(leaf, autumn, smoothstep(0.1, 0.7, lf.wilt)), 1.0);
  } else {                                             // a flower (or seed head, or berry) on each tip
    bool tip = d == sh.maxd || (sh.kind != 3 && texelFetch(iChannel0, ivec2(2 * (2 * i + 1), s), 0).w <= 0.0 &&
                                texelFetch(iChannel0, ivec2(2 * (2 * i + 2), s), 0).w <= 0.0 && d >= sh.maxd - 1);
    float open = sh.kind == 3 ? smoothstep(0.5, 1.0, lf.g) : lf.bloom;
    float berry = sh.kind == 1 ? smoothstep(0.3, 0.6, lf.wilt) * (1.0 - smoothstep(0.85, 1.0, lf.wilt)) : 0.0;
    float fs = sh.flower * sc * max(open, berry * 0.8) * (0.8 + 0.4 * rh(s, i, 6));
    if (!tip || fs <= 0.0 || ((sh.kind == 2 || sh.kind == 3) && rh(s, i, 7) < 0.45)) { size = 0.0; return; }
    vec2 at = arc_at(b0.xy, b0.z, cur, len);
    float a = b0.z + cur * len;
    vec2 dir = vec2(cos(a), sin(a)), nrm = vec2(-dir.y, dir.x);
    vec2 q = c * 2.0 - 1.0;
    float elong = sh.kind == 3 ? 2.6 : 1.0;
    if (sh.kind == 3) fs *= 0.45;
    P = root + at + dir * (q.y * elong + (sh.kind == 3 ? elong * 0.8 : 0.3)) * fs + nrm * q.x * fs;
    float petals = sh.kind == 0 ? float(5 + int(fract(ga.z * 13.0) * 4.0)) : sh.kind == 1 ? 5.0 : sh.kind == 2 ? 5.0 : 0.0;
    lw_data = vec4(q, 2.0, berry > open ? -1.0 : petals);
    vec3 fc = colors < 0.5 ? hsv(bloom_hue(ga.z), 0.6, 1.0) : uPalette[1 + int(ga.z * 6.0) % 6] * 1.15;
    if (sh.kind == 2) fc = colors < 0.5 ? mix(vec3(1.0, 0.78, 0.85), vec3(1.0), fract(ga.z * 3.0)) : mix(uForeground, uAccent, 0.4);
    if (sh.kind == 3) fc = mix(vec3(0.85, 0.78, 0.55), hsv(leafhue, 0.4, 0.7), 0.4 - 0.4 * lf.wilt);
    if (berry > open) fc = colors < 0.5 ? hsv(0.95 + 0.08 * ga.z, 0.85, 0.7) : uAccent;
    color = vec4(fc, 1.0);
  }
  color.rgb = mix(color.rgb, hz, haze);
  pos = vec4(P.x / asp, P.y, 0.0, 1.0);
}
