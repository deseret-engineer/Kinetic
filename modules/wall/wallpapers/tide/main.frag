// Tide: a sea in perspective whose horizon sits at your battery level. Waves are
// noise normals on a plane; they reflect the sky and a glittering path of light.
// Low charge turns the sea toward the theme's red; charging sends up bubbles.
float waves(vec2 q, float t) {
  return lw_noise(q * 0.9 + vec2(t * 0.25, t * 0.1)) * 0.6 + lw_noise(q * 2.3 - vec2(t * 0.3, -t * 0.2)) * 0.3 +
         lw_noise(q * 6.0 + vec2(t * 0.6, 0.0)) * 0.1;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  float asp = iResolution.x / iResolution.y;
  float t = iTime * speed;
  float charge = level >= 0.0 ? level : mix(0.62, uBattery.x, uBattery.w);
  float horizon = 0.18 + 0.55 * charge;
  float low = smoothstep(0.25, 0.08, charge);
  float charging = level >= 0.0 ? 0.0 : uBattery.y;
  vec3 deep = mix(mix(uBackground, uPalette[4], 0.35), mix(uBackground, uPalette[1], 0.4), low);
  vec3 lite = mix(mix(uPalette[6], uPalette[3], low), uPalette[2], charging * 0.5);

  // sky and the light low over the water
  vec2 sunp = vec2(asp * 0.68, horizon + 0.16);
  vec3 sky = mix(mix(uBackground, lite, 0.22), uBackground, smoothstep(horizon, 1.0, uv.y));
  float sd = length(vec2(uv.x * asp, uv.y) - sunp);
  sky = lw_blend(sky, lite, 0.4 * exp(-sd * 6.0) + 0.9 * smoothstep(0.05, 0.045, sd));
  vec3 col = sky;
  if (uv.y < horizon) {
    float dy = horizon - uv.y;
    float z = 0.35 / dy;                                  // distance along the sea
    vec2 q = vec2((uv.x - 0.5) * asp * z, z);
    float e = 0.02;
    float h = waves(q, t);
    vec2 n2 = vec2(waves(q + vec2(e, 0.0), t) - h, waves(q + vec2(0.0, e), t) - h) / e;
    float rough = 0.45 / (1.0 + z * 0.05);
    vec3 n = normalize(vec3(-n2.x * rough, 1.0, -n2.y * rough));
    vec3 view = normalize(vec3((uv.x - 0.5) * asp, -dy, 1.0));
    vec3 r = reflect(view, n);
    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(-view, n), 0.0), 5.0);
    vec3 refl = mix(mix(uBackground, lite, 0.25), uBackground, clamp(r.y * 2.0, 0.0, 1.0));
    vec3 sdir = normalize(vec3((sunp.x / asp - 0.5) * asp, sunp.y - horizon, 1.0));
    float glint = pow(max(dot(r, sdir), 0.0), 300.0) * 3.0 + pow(max(dot(r, sdir), 0.0), 30.0) * 0.25;
    vec3 water = mix(deep * (0.55 + 0.45 * smoothstep(0.0, horizon, uv.y)), refl, fres);
    water = lw_blend(water, lite, glint * smoothstep(0.8, 2.5, z));
    float foam = smoothstep(0.72, 0.85, h) * smoothstep(8.0, 1.0, z) * 0.25;
    water = lw_blend(water, uForeground, foam);
    col = mix(water, sky, smoothstep(horizon - 0.004, horizon, uv.y) * 0.6);   // haze at the horizon
    // bubbles rise while charging
    if (charging > 0.0) {
      vec2 g = vec2(uv.x * asp * 14.0, uv.y * 14.0 - t * 1.2);
      vec2 id = floor(g);
      float hb = lw_hash(id);
      vec2 c = vec2(0.5 + 0.3 * sin(t + hb * 30.0), 0.5);
      float rr = 0.06 + 0.12 * hb, d = length(fract(g) - c);
      float bubble = smoothstep(rr, rr * 0.75, d) - 0.7 * smoothstep(rr * 0.75, rr * 0.45, d);
      col = lw_blend(col, uPalette[2], bubble * step(0.6, hb) * 0.45 * charging);
    }
  }
  col *= 1.0 - 0.2 * dot(uv - 0.5, uv - 0.5);
  fragColor = vec4(lw_dither(col, fragCoord), 1.0);
}
