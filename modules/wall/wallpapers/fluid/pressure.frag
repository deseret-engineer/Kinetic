// One Jacobi step of the pressure Poisson equation, warm-started from last frame.
void mainImage(out vec4 o, in vec2 fc) {
  ivec2 p = ivec2(fc), n = ivec2(iResolution.xy);
  if (iFrame < 2) { o = vec4(0.0); return; }
  float l = texelFetch(iChannel0, fl_wrap(p - ivec2(1, 0), n), 0).x;
  float r = texelFetch(iChannel0, fl_wrap(p + ivec2(1, 0), n), 0).x;
  float b = texelFetch(iChannel0, fl_wrap(p - ivec2(0, 1), n), 0).x;
  float t = texelFetch(iChannel0, fl_wrap(p + ivec2(0, 1), n), 0).x;
  float pr = (l + r + b + t - texelFetch(iChannel1, p, 0).x) * 0.25;
  if (iPass == 0) pr *= 0.99;
  o = vec4(pr, 0.0, 0.0, 1.0);
}
