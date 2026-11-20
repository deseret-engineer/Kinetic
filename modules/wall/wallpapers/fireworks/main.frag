vec3 sky(vec2 uv) {
  vec3 s = texture(iChannel0, uv).rgb;
  vec3 g = textureLod(iChannel0, uv, 3.0).rgb * 0.5 + textureLod(iChannel0, uv, 5.0).rgb * 0.6;
  return s + g * glow;
}

void mainImage(out vec4 o, in vec2 fc) {
  vec2 uv = fc / iResolution.xy;
  // the whole night brightens a little with every burst: the top mip level is the average
  vec3 ambient = textureLod(iChannel0, vec2(0.5), 20.0).rgb * 3.0 * glow;
  vec3 col;
  if (uv.y >= HORIZON || water < 0.5) {
    vec3 c = 1.0 - exp(-(sky(uv) + ambient * (1.0 - uv.y * 0.6)) * 2.2);
    col = lw_blend(uBackground, c, max(max(c.r, c.g), c.b));
  } else {                                    // water: a rippled, darkened mirror of the sky
    float d = HORIZON - uv.y;
    vec2 r = vec2(uv.x + sin(uv.y * 300.0 / (d + 0.05) * 0.05 + iTime * 2.0) * 0.004 * (d * 8.0 + 0.3), HORIZON + d * 1.6);
    vec3 c = 1.0 - exp(-(sky(r) * 0.45 * (1.0 - d * 3.0) + ambient * 0.3) * 2.2);
    vec3 base = mix(uBackground, vec3(0.0), uDark * 0.35);
    col = lw_blend(base, c, max(max(c.r, c.g), c.b));
    col = mix(col, uBackground, smoothstep(0.004, 0.0, abs(d)) * 0.4);
  }
  o = vec4(lw_dither(col, fc), 1.0);
}
