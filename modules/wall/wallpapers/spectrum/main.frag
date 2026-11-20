// Spectrum: iChannel0 is the live spectrum (row 0) and waveform (row 1), in the
// same layout Shadertoy uses. Bass sits in the middle and mirrors outwards.
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float asp = iResolution.x / iResolution.y;
  float base = 0.3;

  vec3 col = uBackground;
  float halo = exp(-length((uv - vec2(0.5, base)) * vec2(asp, 1.6)) * 2.2);
  col = lw_blend(col, mix(uAccent, uPalette[5], 0.5), halo * (0.06 + 0.3 * uAudio.x));

  float x = abs(uv.x - 0.5) * 2.0;
  float bi = floor(x * bars), bf = fract(x * bars);
  float f = (bi + 0.5) / bars;
  float a = texture(iChannel0, vec2(pow(f, 1.7) * 0.7 + 0.004, 0.25)).r;
  float idle = 0.012 + 0.008 * sin(iTime * 1.4 + bi * 0.45);
  float h = max(pow(a, 1.5) * height, idle);
  float gap = smoothstep(0.0, 0.14, bf) * smoothstep(1.0, 0.86, bf);
  float y = uv.y - base;

  vec3 bc = mix(uAccent, uPalette[5], f);
  bc = mix(bc, uPalette[15], 0.4 * smoothstep(h * 0.5, h, y));
  float px = 1.5 / iResolution.y;
  float bar = step(0.0, y) * smoothstep(h + px, h - px, y) * gap;
  float mirror = step(y, 0.0) * smoothstep(-h * 0.6, 0.0, y) * gap * 0.22;
  float glow = exp(-max(y - h, 0.0) * 45.0) * step(0.0, y) * gap * 0.25;
  col = lw_blend(col, bc, bar + mirror + glow);

  // waveform trace near the top
  float wave = texture(iChannel0, vec2(uv.x, 0.75)).r - 0.5;
  float trace = smoothstep(3.0 * px, 0.0, abs(uv.y - 0.82 - wave * 0.12));
  col = lw_blend(col, uForeground, trace * 0.35);
  fragColor = vec4(col, 1.0);
}
