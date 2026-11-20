// Two texels per branch: (base.xy, angle, length) and (curvature, width at base,
// width at tip, growth). Hidden branches get length -1.
void mainImage(out vec4 o, in vec2 fc) {
  int i = int(fc.x) / 2, half_ = int(fc.x) % 2, s = int(fc.y);
  vec4 st = texelFetch(iChannel0, ivec2(s, 0), 0);
  if (st.z < 0.5) { o = vec4(0.0, 0.0, 0.0, -1.0); return; }
  Branch b = walk(s, i, st, texelFetch(iChannel0, ivec2(s, 1), 0), texelFetch(iChannel0, ivec2(s, 2), 0));
  if (!b.ok || b.len <= 1e-5) { o = vec4(0.0, 0.0, 0.0, -1.0); return; }
  o = half_ == 0 ? vec4(b.p, b.a, b.len) : vec4(b.c, b.w0, b.w1, b.gd);
}
