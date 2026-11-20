void mainImage(out vec4 o, in vec2 fc) {
  vec2 uv = fc / iResolution.xy;
  vec4 s = texture(iChannel0, uv);
  vec3 sky = sky_color(uv.y - 0.35);
  // the sun's disc, when it is up and in view
  vec2 st = sun_state();
  vec2 sp = vec2(0.7, 0.35 + st.x * 0.45);
  float d = length((uv - sp) * vec2(iResolution.x / iResolution.y, 1.0));
  sky += mix(vec3(1.0), vec3(1.0, 0.6, 0.3), st.y) * (smoothstep(0.035, 0.03, d) * 0.8 + exp(-d * 7.0) * 0.25) * step(-0.1, st.x);
  vec3 col = mix(sky, s.rgb, s.a);
  o = vec4(lw_dither(col, fc), 1.0);
}
