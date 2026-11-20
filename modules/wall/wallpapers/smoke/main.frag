// Shade the smoke: density from the dye, light from above-left with self-shadowing.
void mainImage(out vec4 o, in vec2 fc) {
  vec2 uv = fc / iResolution.xy;
  vec4 d = texture(iChannel0, uv);
  float dens = max(max(d.r, d.g), d.b);
  vec3 tint = d.rgb / max(dens, 1e-4);

  // shadow: march a few steps toward the light through the density
  vec2 L = vec2(-0.6, 0.8) * 0.012;
  float occ = 0.0;
  for (int i = 1; i <= 4; i++) {
    vec4 s = texture(iChannel0, uv + L * float(i));
    occ += max(max(s.r, s.g), s.b);
  }
  float light = mix(1.0, exp(-occ * 0.9), shading);
  // rim from the density gradient
  vec2 e = 1.5 / iResolution.xy;
  float gx = dot(texture(iChannel0, uv + vec2(e.x, 0.0)).rgb - texture(iChannel0, uv - vec2(e.x, 0.0)).rgb, vec3(0.33));
  float gy = dot(texture(iChannel0, uv + vec2(0.0, e.y)).rgb - texture(iChannel0, uv - vec2(0.0, e.y)).rgb, vec3(0.33));
  vec3 n = normalize(vec3(-gx * 6.0, -gy * 6.0, 1.0));
  float diff = 0.6 + 0.4 * max(dot(n, normalize(vec3(-0.6, 0.8, 0.6))), 0.0);

  float a = 1.0 - exp(-dens * 2.2);
  vec3 smoke = tint * light * mix(1.0, diff, shading);
  smoke += tint * d.a * 0.25 * (colors > 1.5 && colors < 2.5 ? 2.0 : 0.6); // hot cores glow
  vec3 col = lw_blend(uBackground, smoke, a);
  o = vec4(lw_dither(col, fc), 1.0);
}
