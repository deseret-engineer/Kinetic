// Nebula: warped fbm clouds in three theme colors, crossed by dark dust lanes,
// along a faint galactic band.
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
  vec2 q = (p + lw_pan(0.05)) * 1.5 + vec2(iTime * speed * 0.006, 0.0);  // the farthest layer of the parallax
  vec2 w = vec2(lw_fbm(q + 3.1), lw_fbm(q + 7.7));
  float n = lw_fbm(q + w * 1.4);
  float n2 = lw_fbm(q * 2.3 + n * 1.6 + 5.0);
  float band = exp(-pow((p.y - 0.25 * p.x + 0.05) * 3.2, 2.0));            // the galactic plane
  float dens = pow(n, 2.2) * (0.35 + 0.9 * band);
  vec3 neb = mix(mix(uPalette[5], uPalette[4], n2), uAccent, smoothstep(0.55, 0.85, n2) * 0.6) * dens * 1.6;
  neb += mix(uPalette[4], uForeground, 0.5) * pow(n2, 4.0) * band * 0.5;
  float dust = smoothstep(0.45, 0.7, lw_fbm(q * 3.0 + w * 2.0 + 11.0)) * band;
  neb *= 1.0 - 0.75 * dust;
  fragColor = vec4(neb, dust);
}
