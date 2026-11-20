// Shapes, anti-aliased with screen-space derivatives; output is premultiplied.
void mainImage(out vec4 c, in vec2 fc) {
  vec4 d = lw_Data;
  vec3 col = lw_Color.rgb;
  float a;
  if (d.z < 0.5) {                                   // stem: a shaded cylinder
    float u = abs(d.x);
    a = clamp((1.0 - u) / max(fwidth(d.x), 1e-4), 0.0, 1.0);
    col *= 0.8 + 0.35 * sqrt(max(1.0 - u * u, 0.0)) - 0.15 * d.x;
  } else if (d.z < 1.5) {                            // leaf: pointed oval, a midrib, lit from one side
    float v = clamp(d.y, 0.0, 1.0);
    float hw = pow(sin(3.14159 * v), 0.75) * (1.0 - 0.25 * v);
    float e = hw - abs(d.x);
    a = clamp(e / max(fwidth(d.x), 1e-4), 0.0, 1.0) * step(0.0, d.y) * step(d.y, 1.0);
    col *= (0.85 + 0.25 * sign(d.x)) * (1.0 - 0.25 * exp(-d.x * d.x * 300.0) * step(0.05, v));
  } else if (d.z < 2.5) {                            // flower
    vec2 q = d.xy;
    float r = length(q), th = atan(q.y, q.x);
    float e;
    if (d.w < -0.5) {                                // berry
      e = 0.75 - r;
      col *= 1.0 + 0.6 * smoothstep(0.3, 0.0, length(q - vec2(-0.25, 0.25)));
    } else if (d.w < 0.5) {                          // seed head
      e = 0.9 - r;
      col *= 0.8 + 0.3 * lw_hash(floor(q * 6.0));
    } else {
      float pr = 0.55 + 0.45 * pow(abs(cos(th * d.w * 0.5)), 0.7);
      e = pr - r;
      col *= 0.75 + 0.35 * smoothstep(0.0, pr, r);
      float ctr = smoothstep(0.24, 0.18, r);
      col = mix(col, vec3(1.0, 0.82, 0.3) * 0.9, ctr);
    }
    a = clamp(e / max(fwidth(r), 1e-4), 0.0, 1.0);
  } else if (d.z > 3.5) {                            // meadow blade
    a = 1.0;
  } else {                                           // ground: rolling, with a grassy edge
    int L = int(d.w + 0.5);
    float x = d.x, gy = ground_y(L, x);
    float asp = iResolution.x / iResolution.y;
    gy += 0.004 * layer_scale(L) * (lw_noise(vec2(x * asp * 260.0 / layer_scale(L), float(L) * 9.0)) - 0.3);
    a = clamp((gy - d.y) / max(fwidth(d.y), 1e-4), 0.0, 1.0);
    col *= 0.75 + 0.35 * smoothstep(gy - 0.12, gy, d.y) + 0.08 * (lw_noise(vec2(x * 90.0, d.y * 140.0)) - 0.5);
  }
  c = vec4(col * a * lw_Color.a, a * lw_Color.a);
}
