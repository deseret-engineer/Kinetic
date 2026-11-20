// One generation. The buffer is half the screen in each axis, so it holds a
// grid for any cell size of 2 px or more; only the top-left grid region is used.
// r = alive, g = cell age, b = trail, a = seed/cell signature (detects reseeds).
ivec2 gridSize() { return ivec2(floor(iResolution.xy * 2.0 / cell)); }

float aliveAt(ivec2 c, ivec2 g) {
  if (wrap > 0.5) c = (c % g + g) % g;
  else if (c.x < 0 || c.y < 0 || c.x >= g.x || c.y >= g.y) return 0.0;
  return texelFetch(iChannel0, c, 0).r;
}

bool hasBit(float mask, int n) { return (int(mask + 0.5) >> n & 1) == 1; }

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  ivec2 c = ivec2(fragCoord), g = gridSize();
  if (c.x >= g.x || c.y >= g.y) { fragColor = vec4(0.0); return; }
  float sig = seed * 40.0 + cell;
  vec4 prev = texelFetch(iChannel0, c, 0);
  vec2 cellUV = (vec2(c) + 0.5) / vec2(g);

  if (iFrame < 2 || abs(prev.a - sig) > 0.5) {
    float alive = step(lw_hash(vec2(c) + seed * 17.31 + uEpoch * 7.13 + 0.5), density);
    fragColor = vec4(alive, alive, 0.0, sig);
    return;
  }

  // hold a mouse button on the desktop to paint live cells
  float asp = iResolution.x / iResolution.y;
  bool painted = iMouse.z > 0.0 && length((cellUV - iMouse.xy / iResolution.xy) * vec2(asp, 1.0)) < max(0.012, 1.5 * cell / iResolution.y);

  bool tick = floor(iTime * speed) != floor((iTime - iTimeDelta) * speed);
  if (!tick) {
    fragColor = painted ? vec4(1.0, max(prev.g, 1.0), 1.0, sig) : prev;
    return;
  }

  int n = 0;
  for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++)
      if (x != 0 || y != 0) n += int(aliveAt(c + ivec2(x, y), g));

  bool alive = prev.r > 0.5;
  bool next = alive ? hasBit(survive, n) : hasBit(birth, n);
  float t = floor(iTime * speed);

  // Running back (the screen filled up): nothing is born, lonely cells die,
  // and random holes open so even a solid screen erodes from the inside.
  if (uReverse > 0.0 && lw_hash(vec2(c) * 0.71 + t * 0.37) < uReverse) {
    bool hole = lw_hash(floor(vec2(c) / 3.0) + t * 0.913 + uEpoch) < 0.02;
    next = alive && n >= 4 && !hole;
  }

  // occasional random patches keep sparse rules alive; sounds set some off too
  vec2 blk = floor(vec2(c) / 6.0);
  float spark = sparks * 0.0006 * (1.0 - uReverse) + uSound.x * 0.004 + step(uSound.y, 0.12) * uSound.z * 0.01;
  if (spark > 0.0 && lw_hash(blk + t * 0.137) < spark && lw_hash(vec2(c) + t) < 0.5) next = true;
  if (painted) next = true;

  float age = next ? (alive ? prev.g + 1.0 : 1.0) : 0.0;
  float trail = next ? 1.0 : prev.b * trails;
  fragColor = vec4(next ? 1.0 : 0.0, min(age, 1000.0), trail, sig);
  fragColor.b = max(fragColor.b, 0.0);
  // neighbor count is useful for coloring; pack it into g's fraction
  fragColor.g += float(n) / 10.0;
}
