void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  vec2 e = 1.0 / iChannelResolution[0].xy;
  float hl = texture(iChannel0, uv - vec2(e.x, 0.0)).r, hr = texture(iChannel0, uv + vec2(e.x, 0.0)).r;
  float hb = texture(iChannel0, uv - vec2(0.0, e.y)).r, ht = texture(iChannel0, uv + vec2(0.0, e.y)).r;
  vec2 grad = vec2(hr - hl, ht - hb);
  vec2 ruv = uv + grad * 0.03 * refraction;

  vec3 under;
  if (surface < 0.5) {
    under = texture(iChannel1, lw_cover(ruv, iChannelResolution[1].xy, iResolution.xy)).rgb;
  } else {
    float n = lw_fbm(ruv * vec2(iResolution.x / iResolution.y, 1.0) * 3.0);
    under = mix(uPalette[0], mix(uPalette[4], uPalette[6], n), 0.35 + 0.3 * n);
  }
  vec3 n = normalize(vec3(-grad * 4.0, 1.0));
  float spec = pow(max(dot(reflect(vec3(0.0, 0.0, -1.0), n), normalize(vec3(-0.3, 0.4, 0.85))), 0.0), 60.0);
  float fres = 0.04 + 0.4 * (1.0 - n.z);
  vec3 col = mix(under, uForeground, fres * 0.6) + spec * 0.6 * mix(uForeground, uAccent, 0.3);
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
