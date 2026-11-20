void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float asp = iResolution.x / iResolution.y;
  vec2 p = (vec2(uv.x * asp, uv.y) + lw_pan(0.5)) * scale;  // workspace parallax moves the field under the particles
  float t = iTime * 0.015 * speed, e = 0.03;
  vec2 v = vec2(lw_fbm(p + vec2(0.0, e) + t) - lw_fbm(p - vec2(0.0, e) + t),
                -(lw_fbm(p + vec2(e, 0.0) - t) - lw_fbm(p - vec2(e, 0.0) - t))) / (2.0 * e);
  vec2 dm = (uv - uPointer.xy) * vec2(asp, 1.0);
  v += uPointer.z * vec2(-dm.y, dm.x) * 9.0 * exp(-dot(dm, dm) * 25.0);
  fragColor = vec4(v, 0.0, 1.0);
}
