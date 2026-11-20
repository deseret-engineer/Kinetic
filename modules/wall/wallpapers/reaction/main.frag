void mainImage(out vec4 o, in vec2 fc) {
  vec3 s = texture(iChannel0, fc / iResolution.xy).xyz;   // B, dB/dx, dB/dy
  float b = s.x;
  vec3 L = normalize(vec3(-0.5, 0.7, 0.6));
  vec3 n = normalize(vec3(-s.yz * 4.0, 1.0));
  float light = mix(1.0, 0.55 + 0.6 * max(dot(n, L), 0.0), relief);
  float t = smoothstep(0.08, 0.4, b);
  vec3 ink;
  if (colors < 0.5) ink = lw_palette(b * 1.6 + fc.x / iResolution.x * 0.3);
  else if (colors < 1.5) ink = uAccent;
  else ink = mix(uPalette[4], uAccent, smoothstep(0.2, 0.45, b));
  vec3 col = mix(uBackground, ink * light, t * (uDark > 0.5 ? 0.9 : 0.8));
  col += pow(max(reflect(-L, n).z, 0.0), 24.0) * relief * t * 0.25;
  o = vec4(lw_dither(col, fc), 1.0);
}
