// Orbit: an analytic sphere and ring plane (no ray marching), so it is cheap at
// native resolution. The planet takes the theme accent; rings cast and receive shadows.
float ringDensity(float r) {
  float d = smoothstep(1.28, 1.36, r) * smoothstep(2.3, 2.18, r);
  d *= 0.5 + 0.5 * lw_noise(vec2(r * 34.0, 2.0));
  d *= 1.0 - 0.85 * smoothstep(0.035, 0.0, abs(r - 1.9));
  return d * (0.75 + 0.25 * lw_noise(vec2(r * 9.0, 7.0)));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
  float t = iTime * speed;
  vec2 par = uPointer.xy - 0.5;
  float slide = uWorkspace.x - uWorkspace.z; // non-zero only while switching workspaces

  // space
  vec3 col = mix(uBackground, uPalette[0], 0.5 * uDark);
  vec2 sp = p + par * 0.01 + vec2(slide * 0.05, 0.0);
  for (int i = 0; i < 2; i++) {
    vec2 g = sp * (26.0 + float(i) * 22.0);
    vec2 id = floor(g);
    float h = lw_hash(id + float(i) * 31.0);
    float d = length(fract(g) - 0.5 - (lw_hash2(id + 5.0) - 0.5) * 0.7);
    col = lw_blend(col, uForeground, step(0.72, h) * smoothstep(0.1, 0.0, d) * (0.5 + 0.5 * sin(t * 1.3 + h * 60.0)) * 0.8);
  }
  float haze = lw_fbm(sp * 1.4 + 3.0);
  col = lw_blend(col, mix(uPalette[4], uPalette[5], haze), pow(haze, 3.0) * 0.35);

  vec2 center = vec2(0.26, -0.02) + par * 0.035 + vec2(slide * 0.35, 0.0);
  vec2 q = (p - center) / size;
  vec3 N = normalize(vec3(0.22 + 0.03 * sin(t * 0.05), 0.9, 0.36)); // ring normal = spin axis
  vec3 L = normalize(vec3(-0.62, 0.38, 0.55));
  vec3 U = normalize(cross(N, vec3(0.0, 0.0, 1.0))), V = cross(N, U);

  // ring plane hit for the view ray (orthographic, looking down -z)
  float rz = -(N.x * q.x + N.y * q.y) / N.z;
  vec3 rh = vec3(q, rz);
  float rr = length(rh);
  float dens = ringDensity(rr);
  float toSun = -dot(rh, L);
  float inShadow = step(0.0, toSun) * step(length(rh + L * toSun), 1.0);
  vec3 ringCol = mix(uPalette[15], uAccent, 0.45) * (0.35 + 0.65 * (1.0 - inShadow)) * (0.8 + 0.2 * lw_noise(vec2(rr * 70.0, 1.0)));

  float r2 = dot(q, q);
  bool onPlanet = r2 < 1.0;
  float pz = onPlanet ? sqrt(1.0 - r2) : -1e3;
  if (dens > 0.0 && rz < pz) col = mix(col, ringCol, dens * 0.9); // ring behind the planet

  if (onPlanet) {
    vec3 n = vec3(q, pz);
    float lat = dot(n, N);
    float lon = atan(dot(n, V), dot(n, U)) + t * 0.06;
    float band = lw_noise(vec2(lat * 7.0 + 0.07 * sin(lon * 4.0 + lat * 18.0), 1.0)) * 0.65 +
                 lw_noise(vec2(lat * 24.0 + 0.04 * sin(lon * 7.0 - lat * 30.0), 4.0)) * 0.35;
    vec3 base = mix(mix(uAccent, uPalette[1], 0.3) * 0.55, mix(uAccent, uPalette[15], 0.4), band);
    base = mix(base, uPalette[3] * 0.9, 0.25 * smoothstep(0.55, 0.9, abs(lat)));
    float diff = smoothstep(-0.08, 0.75, dot(n, L));
    // shadow the rings cast on the cloud tops
    float k = -dot(N, n) / dot(N, L);
    float shade = k > 0.0 ? 1.0 - 0.65 * ringDensity(length(n + L * k)) : 1.0;
    vec3 surf = base * (0.04 + 0.96 * diff * shade);
    surf += uAccent * 0.25 * pow(1.0 - pz, 3.0) * diff; // limb scattering
    float aa = smoothstep(1.0, 1.0 - 2.5 / (size * iResolution.y), sqrt(r2));
    col = mix(col, surf, aa);
  } else {
    float rim = exp(-(sqrt(r2) - 1.0) * 16.0) * smoothstep(-0.3, 0.6, dot(normalize(vec3(q, 0.0)), L));
    col += uAccent * rim * 0.22 * uDark;
  }
  if (dens > 0.0 && rz >= pz) col = mix(col, ringCol, dens * 0.9); // ring in front
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
