// One Jacobi step of the pressure Poisson equation; runs `quality` times a frame,
// warm-started from the previous frame's solution. Clamped edges = Neumann walls.
void mainImage(out vec4 o, in vec2 fc) {
  ivec2 p = ivec2(fc), m = ivec2(iResolution.xy) - 1;
  if (iFrame < 2) { o = vec4(0.0); return; }
  float l = texelFetch(iChannel0, clamp(p - ivec2(1, 0), ivec2(0), m), 0).x;
  float r = texelFetch(iChannel0, clamp(p + ivec2(1, 0), ivec2(0), m), 0).x;
  float b = texelFetch(iChannel0, clamp(p - ivec2(0, 1), ivec2(0), m), 0).x;
  float t = texelFetch(iChannel0, clamp(p + ivec2(0, 1), ivec2(0), m), 0).x;
  float div = texelFetch(iChannel1, p, 0).x;
  float pr = (l + r + b + t - div) * 0.25;
  if (iPass == 0) pr *= 0.995; // keep the warm start from drifting
  o = vec4(pr, 0.0, 0.0, 1.0);
}
