// Starfield: four star layers at different depths. Workspace switches pan the
// camera sideways, so nearer layers slide further than distant ones.
vec4 stars(vec2 p, float scale, float seed, float t) {
  p *= scale;
  vec2 id = floor(p), f = fract(p);
  float h = lw_hash(id + seed);
  if (h < 0.5) return vec4(0.0);                 // half the cells hold no star
  vec2 c = lw_hash2(id + seed + 7.1) * 0.44 + 0.28;
  float d = length(f - c);
  if (d > 0.27) return vec4(0.0);                // outside the largest possible glow (0.1 * 2.7)
  float size = 0.03 + 0.07 * pow(lw_hash(id + seed + 1.7), 5.0);
  float b = (0.65 + 0.35 * sin(t * (1.0 + h * 3.0) + h * 50.0));
  b *= smoothstep(size, 0.0, d) + 0.3 * smoothstep(size * 2.7, 0.0, d) * step(0.06, size);
  vec2 o = abs(f - c);
  b += step(0.07, size) * 0.5 * (smoothstep(0.012, 0.0, o.y) + smoothstep(0.012, 0.0, o.x)) * smoothstep(0.27, 0.0, d);
  vec3 tint = mix(uForeground, mix(uPalette[4], uPalette[3], lw_hash(id + seed + 9.0)), 0.4);
  return vec4(tint, b);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
  float t = iTime * speed;
  vec2 par = (uPointer.xy - 0.5) * 0.025;

  vec3 col = mix(uBackground, uPalette[0], uDark * 0.6);
  vec4 nb = texture(iChannel0, fragCoord / iResolution.xy);
  vec3 neb = nb.rgb;
  col = lw_blend(col, neb, nebula * min(lw_luma(neb) * 2.5, 1.0));

  for (int i = 0; i < 4; i++) {
    float depth = float(i) + 1.0;
    vec4 s = stars(p + lw_pan(0.12 * depth) + vec2(t * 0.003 * depth, 0.0) + par * depth, 5.0 + float(i) * 6.0, float(i) * 13.0, t);  // nearer layers pan further
    col = lw_blend(col, s.rgb, s.a * (1.05 - 0.17 * float(i)) * (1.0 - 0.6 * nb.a * float(i > 0)));
  }

  // an occasional shooting star
  float k = floor(t / 11.0), ft = fract(t / 11.0) * 11.0;
  vec2 start = vec2(lw_hash(vec2(k, 1.0)) * 1.6 - 0.6, 0.25 + 0.2 * lw_hash(vec2(k, 2.0)));
  vec2 dir = normalize(vec2(-1.0, -0.4));
  vec2 rel = p - (start + dir * ft * 1.5);
  float along = dot(rel, -dir), across = abs(dot(rel, vec2(-dir.y, dir.x)));
  float meteor = step(0.0, along) * exp(-along * 14.0) * smoothstep(0.004, 0.0, across) * smoothstep(0.9, 0.5, ft);
  col = lw_blend(col, uPalette[15], meteor);
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
