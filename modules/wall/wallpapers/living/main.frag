// Living Still: the user's own wallpaper with a slow push-in, pointer parallax,
// a slide when the workspace changes, and drifting motes of light.
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float asp = iResolution.x / iResolution.y;
  float t = iTime * speed;

  float zoom = 1.07 + 0.025 * sin(t * 0.045);
  vec2 shift = (uPointer.xy - 0.5) * -0.014 * parallax;
  shift.x += (uWorkspace.x - uWorkspace.z) * 0.03;
  shift += 0.006 * vec2(sin(t * 0.07), cos(t * 0.05));
  vec2 c = (lw_cover(uv, iChannelResolution[0].xy, iResolution.xy) - 0.5) / zoom + 0.5 + shift;
  // faint heat shimmer keeps large flat areas from looking frozen
  c += 0.0012 * (vec2(lw_noise(uv * 5.0 + t * 0.12), lw_noise(uv * 5.0 - t * 0.1 + 9.0)) - 0.5);
  vec3 col = texture(iChannel0, c).rgb;

  // a slow band of light sweeping across
  float sweep = smoothstep(0.35, 0.0, abs(fract(t * 0.012) * 2.4 - 0.7 - (uv.x * 0.8 + uv.y * 0.3)));
  col *= 1.0 + 0.07 * sweep;

  for (int i = 0; i < 3; i++) {
    float fi = float(i), scale = 4.0 + fi * 3.5;
    vec2 g = vec2(uv.x * asp, uv.y) * scale + vec2(sin(t * 0.03 + fi) * 0.4 - shift.x * 30.0 * (fi + 1.0), -t * (0.015 + 0.012 * fi));
    vec2 id = floor(g);
    float h = lw_hash(id + fi * 19.0);
    vec2 p = fract(g) - (lw_hash2(id + fi * 7.0) * 0.6 + 0.2);
    float r = 0.05 + 0.13 * fract(h * 13.0);
    float mote = smoothstep(r, r * 0.25, length(p)) * step(0.6, h) * (0.5 + 0.5 * sin(t * 0.6 + h * 40.0));
    col += mix(uAccent, uPalette[15], 0.6) * mote * 0.07 * motes;
  }
  col *= 1.0 - 0.35 * dot(uv - 0.5, uv - 0.5);
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
