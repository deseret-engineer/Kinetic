float ground(vec2 p) {
  float h = (lw_fbm(p * 0.08) * 2.0 - 1.0) * 1.6 * height;
  float ridge = 1.0 - abs(lw_noise(p * 0.05 + 9.0) * 2.0 - 1.0);   // ridged mountain chains
  h += ridge * ridge * ridge * 5.5 * height * smoothstep(4.0, 14.0, abs(p.x) + 3.0);  // keep the flight path lower
  h += (lw_noise(p * 0.6) - 0.5) * 0.25;
  return water > 0.5 ? max(h, -0.15) : h;
}

void mainVertex(int id, vec4 state, inout vec4 pos, inout float size, inout vec4 color) {
  int tri = id / 3, corner = id % 3, cell = tri / 2, half_ = tri % 2;
  int cx = cell % GX, cz = cell / GX;
  float travel = iTime * speed * 2.0;
  float zoff = fract(travel / CELL) * CELL;                 // scroll smoothly, recycle rows
  float zbase = floor(travel / CELL) * CELL;
  // the cell's corners, split into two triangles along a diagonal
  ivec2 c[3];
  if (half_ == 0) { c[0] = ivec2(0, 0); c[1] = ivec2(1, 0); c[2] = ivec2(0, 1); }
  else           { c[0] = ivec2(1, 0); c[1] = ivec2(1, 1); c[2] = ivec2(0, 1); }
  vec3 P[3];
  for (int k = 0; k < 3; k++) {
    vec2 g = vec2(float(cx + c[k].x - GX / 2), float(cz + c[k].y)) * CELL;
    // jitter grid points (seeded by their world cell) for an irregular low-poly look
    vec2 world = vec2(g.x, g.y + zbase);
    world += (lw_hash2(floor(world / CELL + 0.5)) - 0.5) * CELL * 0.6;
    P[k] = vec3(world.x, ground(world), world.y - zbase - zoff);
  }
  vec3 n = normalize(cross(P[2] - P[0], P[1] - P[0]));
  vec3 p = P[corner];
  // camera: a little above the ground, looking slightly down; the pointer steers the view
  vec2 look = uPointer.z > 0.05 ? (uPointer.xy - 0.5) * uPointer.z : vec2(0.0);
  vec3 cam = vec3(look.x * 3.0, 2.6 + look.y * 1.2, -1.0);
  vec3 v = p - cam;
  float pitch = 0.16;
  v = vec3(v.x, v.y * cos(pitch) + v.z * sin(pitch), -v.y * sin(pitch) + v.z * cos(pitch));
  pos = lw_perspective(v, 1.6, iResolution.x / iResolution.y);
  pos.z = clamp(v.z / (FAR + 5.0), 0.0, 1.0) * pos.w;      // linear depth over the visible range

  vec2 s = sun_state();
  vec3 L = normalize(vec3(-0.7, max(s.x, 0.08) * 0.7, 0.45));
  float lit = max(dot(n, L), 0.0) * smoothstep(-0.2, 0.15, s.x);
  float hgt = P[0].y + P[1].y + P[2].y;
  bool lake = water > 0.5 && hgt < -0.44;
  vec3 base = lake ? mix(uPalette[4], uBackground, 0.5)
            : mix(mix(uPalette[2], uPalette[3], smoothstep(-0.5, 4.0, hgt)), uForeground, smoothstep(7.0, 11.0, hgt));
  vec3 sunc = mix(vec3(1.0), vec3(1.0, 0.7, 0.45), s.y);
  vec3 col = base * (0.18 + 0.82 * lit * sunc) + (lake ? pow(max(dot(reflect(-L, n), normalize(-v)), 0.0), 8.0) * 0.3 * sunc : vec3(0.0));
  float haze = 1.0 - exp(-v.z / FAR * 3.0 * fog);
  col = mix(col, sky_color(0.02), clamp(haze, 0.0, 1.0));
  color = vec4(col, 1.0);
  // barycentric coordinates, for the wireframe
  lw_data = vec4(corner == 0 ? 1.0 : 0.0, corner == 1 ? 1.0 : 0.0, corner == 2 ? 1.0 : 0.0, haze);
}
