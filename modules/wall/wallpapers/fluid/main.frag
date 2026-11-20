// Shade the ink like a glossy liquid: color from the dye, a normal from its
// density gradient, a soft specular highlight and a faint shadow under the folds.
void mainImage(out vec4 o, in vec2 fc) {
  vec2 uv = fc / iResolution.xy, e = 1.0 / iChannelResolution[0].xy;
  vec3 d = texture(iChannel0, uv).rgb;
  float dens = max(max(d.r, d.g), d.b);
  vec3 tint = d / max(dens, 1e-4);

  float hl = lw_luma(texture(iChannel0, uv - vec2(e.x, 0.0)).rgb), hr = lw_luma(texture(iChannel0, uv + vec2(e.x, 0.0)).rgb);
  float hb = lw_luma(texture(iChannel0, uv - vec2(0.0, e.y)).rgb), ht = lw_luma(texture(iChannel0, uv + vec2(0.0, e.y)).rgb);
  vec3 n = normalize(vec3((hl - hr) * 3.0, (hb - ht) * 3.0, 1.0));
  vec3 L = normalize(vec3(-0.5, 0.7, 0.8));
  float diff = 0.75 + 0.25 * dot(n, L);
  float spec = pow(max(dot(n, normalize(L + vec3(0.0, 0.0, 1.0))), 0.0), 40.0);

  float a = 1.0 - exp(-dens * 2.0);
  vec3 ink = tint * (0.85 + 0.45 * min(dens, 1.0)) * mix(1.0, diff, gloss);
  vec3 col = lw_blend(uBackground, ink, a);
  col += spec * gloss * 0.5 * smoothstep(0.02, 0.3, dens);
  float vig = 1.0 - 0.25 * dot(uv - 0.5, uv - 0.5);
  o = vec4(lw_dither(col * vig, fc), 1.0);
}
