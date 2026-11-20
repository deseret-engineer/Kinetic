// Line integral convolution: average fine noise along the streamline through
// each pixel, which turns the field into brush strokes. A traveling weight
// along the line makes light flow downstream; sparse seeds add bright comets.
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  vec2 stepUV = 3.0 / iResolution.y * vec2(iResolution.y / iResolution.x, 1.0);
  const int N = 16;
  float acc = 0.0, wsum = 0.0, comet = 0.0;
  vec2 p = uv, v0 = texture(iChannel0, uv).xy;
  float t = iTime * speed;
  for (int i = 0; i < N; i++) {
    vec2 v = texture(iChannel0, p).xy;
    float sp = length(v);
    if (sp < 1e-4) break;
    p -= v / sp * stepUV;
    float fi = float(i) / float(N);
    vec2 np = p * iResolution.y / 1.6;
    float n = lw_noise(np);
    float w = 1.0 - fi;
    acc += n * w;
    wsum += w;
    // comets: sparse, slowly re-rolled seeds whose pulse runs downstream
    vec2 gp = p * iResolution.y / 9.0;
    vec2 cell = floor(gp);
    float h = lw_hash(cell + floor(t * 0.2 + lw_hash(cell) * 5.0));
    float seed = step(1.0 - particles * 0.12, h) * smoothstep(0.35, 0.1, length(fract(gp) - 0.5));
    float phase = fract(lw_hash(cell + 3.1) + t * 0.5) * tail;
    comet += seed * smoothstep(0.12, 0.0, abs(fi - phase)) * (1.0 - fi * 0.7);
  }
  float lic = wsum > 0.0 ? acc / wsum : 0.5;
  lic = smoothstep(0.38, 0.68, lic);
  float pulse = 0.55 + 0.45 * sin(lw_noise(uv * 6.0) * 6.28 - t * 1.5);
  float sp = length(v0);
  vec3 c = colors < 0.5 ? mix(uPalette[4], uPalette[5], 0.5 + 0.5 * sin(atan(v0.y, v0.x)))
         : colors < 1.5 ? uAccent : mix(uPalette[6], uPalette[1], smoothstep(0.5, 3.0, sp));
  vec3 col = lw_blend(uBackground, c, lic * lines * pulse * 0.6);
  col = lw_blend(col, mix(c, uPalette[15], 0.4), min(comet, 1.2));
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
