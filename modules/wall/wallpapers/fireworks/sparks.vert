// Shell types: 0 peony (sphere), 1 ring, 2 willow (slow, drooping, golden), 3 crackle (glittering)
vec3 shell_color(int key, int type) {
  if (type == 2) return mix(vec3(1.0, 0.72, 0.35), uForeground, 0.2);
  if (colors < 0.5) return uPalette[1 + key % 6] * 1.2 + 0.1;
  if (colors < 1.5) {
    vec3 c[5] = vec3[5](vec3(1.0, 0.3, 0.25), vec3(0.35, 0.6, 1.0), vec3(0.4, 1.0, 0.5), vec3(1.0, 0.85, 0.4), vec3(0.85, 0.45, 1.0));
    return c[key % 5];
  }
  return uAccent * 1.2;
}

// `point` is the point size; `size` is the Spark size setting
void mainVertex(int id, vec4 state, inout vec4 pos, inout float point, inout vec4 color) {
  int slot = id / PER, j = id % PER;
  float asp = iResolution.x / iResolution.y, age, gap = 1.0 / rate;
  int key;
  bool rise = true;
  vec2 burst;
  if (slot < SLOTS) {                       // the regular schedule: slot s fires every SLOTS launches
    float k = floor((iTime / gap - float(slot)) / float(SLOTS));
    key = int(k) * SLOTS + slot;
    age = iTime - (k * float(SLOTS) + float(slot)) * gap - lw_rand(key, 1) * gap * 0.8;
  } else if (slot < SLOTS + VOLLEY) {       // a volley right after a workspace switch
    int v = slot - SLOTS;
    key = 100000 + int(uWorkspace.z) * 17 + v;
    age = volley > 0.5 ? uWorkspace.y - float(v) * 0.16 : -1.0;
  } else if (slot == SLOTS + VOLLEY) {      // the last sound onset: bursts at once, no rocket
    key = 300000 + int(uSound.w);
    age = uSound.y < 1e3 ? uSound.y + 10.0 : -1.0;
    rise = false;
    burst = vec2(0.15 + 0.7 * lw_rand(key, 21), 0.5 + 0.3 * lw_rand(key, 22));
  } else {                                  // the last click: bursts in place, no rocket
    key = 200000 + int(iTime - uPointer.w);
    age = uPointer.z > 0.0 || uPointer.w < 10.0 ? uPointer.w + 10.0 : -1.0;
    rise = false;
    burst = abs(iMouse.zw) / iResolution.xy;
  }
  int type = int(lw_rand(key, 2) * 4.0);
  float trise = 1.0 + 0.5 * lw_rand(key, 3);
  vec2 launch = vec2(0.1 + 0.8 * lw_rand(key, 4), HORIZON);
  if (rise) burst = vec2(launch.x + (lw_rand(key, 5) - 0.5) * 0.1, 0.5 + 0.25 * lw_rand(key, 6));
  else age -= 10.0 - trise;                  // skip straight to the burst
  float life = type == 2 ? 3.6 : 2.4;
  if (age < 0.0 || age > trise + life) { point = 0.0; return; }

  float b, sz = 1.0;
  vec3 c = shell_color(key, type);
  if (age < trise) {                        // the rocket climbs, easing out; a short trail of embers
    if (j >= 12 || !rise) { point = 0.0; return; }
    float t = max(age - float(j) * 0.025, 0.0) / trise;
    float e = 1.0 - (1.0 - t) * (1.0 - t);
    pos.xy = mix(launch, burst, vec2(t, e));
    pos.x += sin(age * 9.0 + float(key)) * 0.002;
    c = mix(vec3(1.0, 0.75, 0.45), uForeground, 0.3);
    b = 1.2 * (1.0 - float(j) / 12.0);
  } else {
    if (j >= int(sparks)) { point = 0.0; return; }
    float tau = age - trise;
    float ru = lw_rand(id, key), rp = lw_rand(id, key + 1) * 6.2831853;
    vec2 dir;
    if (type == 1) {                        // ring, tilted at random
      float tilt = lw_rand(key, 7) * 3.14159;
      dir = vec2(cos(rp), sin(rp) * 0.35);
      dir = vec2(dir.x * cos(tilt) - dir.y * sin(tilt), dir.x * sin(tilt) + dir.y * cos(tilt));
    } else {                                // a sphere seen from the side: denser in the middle
      float z = ru * 2.0 - 1.0;
      dir = sqrt(1.0 - z * z) * vec2(cos(rp), sin(rp));
    }
    float v0 = (type == 2 ? 0.34 : 0.48) * (0.85 + 0.3 * lw_rand(id, key + 2)) * (0.8 + 0.4 * lw_rand(key, 8));
    float kd = type == 2 ? 2.6 : 1.7, g = 0.11 * gravity * (type == 2 ? 1.4 : 1.0);
    float ex = (1.0 - exp(-kd * tau)) / kd;  // drag-limited travel
    vec2 off = dir * v0 * ex - vec2(0.0, g * (tau - ex) / kd);
    pos.xy = burst + vec2(off.x / asp, off.y);
    float fade = 1.0 - tau / life;
    b = pow(max(fade, 0.0), 1.2) * 1.8;
    if (type == 3 && tau > 0.6) b *= step(0.55, lw_rand(id, key + int(tau * 25.0))) * 2.2; // crackle
    if (tau < 0.08) { b += (0.08 - tau) * 25.0; c = mix(c, vec3(1.0), 0.6); }               // the flash
    c = mix(c, vec3(1.0, 0.6, 0.3), clamp(tau / life - 0.5, 0.0, 0.5));                      // embers cool
    sz = 1.0 + 0.5 * step(0.92, lw_rand(id, key + 3));
  }
  if (pos.y < HORIZON) { point = 0.0; return; }
  color = vec4(c * b, 1.0);
  point = size * sz * max(1.0, iResolution.y / 1000.0);
}
