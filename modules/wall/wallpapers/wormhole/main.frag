// Wormhole: a tube of hexagonal panels rushing past. Panel edges glow, rings of
// light travel down the tunnel on the beat, and the far end burns white.
vec2 hex_edge(vec2 p) {                     // distance to the nearest hexagon edge, and a cell id
  vec2 s = vec2(1.0, 1.7320508);
  vec2 a = mod(p, s) - s * 0.5, b = mod(p - s * 0.5, s) - s * 0.5;
  vec2 g = dot(a, a) < dot(b, b) ? a : b;
  vec2 id = p - g;
  g = abs(g);
  return vec2(0.5 - max(dot(g, normalize(s)), g.x), lw_rand(int(floor(id.x * 2.0 + 0.5)) * 7919 + int(floor(id.y * 2.0 + 0.5)), 3));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
  p -= (uPointer.xy - 0.5) * 0.15 * uPointer.z;
  float t = iTime * speed;
  p += 0.03 * vec2(sin(t * 0.3), cos(t * 0.23));             // the tunnel bends gently
  float r = length(p), a = atan(p.y, p.x);
  float z = 0.3 / max(r, 1e-3);
  float bass = uAudio.x * react + uSound.x * 0.8;
  float u = a / 6.2831 * rings + twist * z * 0.15 + t * 0.04 * twist;
  float v = z * 2.0 + t * 1.6;
  vec2 g = vec2(u, v);
  float pat, id = 0.0;
  if (walls < 0.5) {
    vec2 h = hex_edge(g * vec2(1.0, 0.6));
    id = h.y;
    pat = smoothstep(0.06, 0.0, h.x) + 0.15 * step(0.85, id) * (0.5 + 0.5 * sin(t * 3.0 + id * 40.0));
  } else if (walls < 1.5) {
    pat = smoothstep(0.35, 0.5, abs(fract(v) - 0.5));
  } else {
    pat = 0.5 + 0.5 * sin(v * 3.0 + lw_noise(g) * 3.0);
  }
  vec3 c = mix(uPalette[4], uPalette[5], 0.5 + 0.5 * sin(v * 0.25 + a));
  c = mix(c, uAccent, smoothstep(0.3, 1.0, bass));
  float fog = exp(-z * 0.08) * smoothstep(0.0, 0.1, r);
  // rings of light that travel down the tunnel
  float ring = pow(0.5 + 0.5 * sin(v * 0.5 - t * 2.0), 24.0) * (0.6 + bass);
  vec3 col = uBackground * (0.6 + 0.4 * smoothstep(0.0, 0.6, r));
  col = lw_blend(col, c * 0.25, fog * 0.6);                                    // panel faces
  col = lw_blend(col, c, pat * fog * (0.9 + bass));
  col = lw_blend(col, mix(c, vec3(1.0), 0.4), ring * fog);
  // speed streaks
  vec2 sp = vec2(cos(a), sin(a)) * 24.0;
  float streak = smoothstep(0.9, 1.0, lw_noise(sp + vec2(z * 0.3 + t * 0.5, 0.0))) * smoothstep(0.1, 0.5, r);
  col = lw_blend(col, uForeground, streak * 0.25);
  col = lw_blend(col, mix(uPalette[15], vec3(1.0), 0.5), exp(-r * 14.0) * (0.6 + bass));
  col *= 1.0 - 0.35 * r * r;
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
