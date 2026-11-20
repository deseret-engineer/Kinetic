// Still: the Omarchy background, cropped to cover the screen as Omarchy does.
// Static, so the glass has real pixels to bend even without a live wallpaper.
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 uv = fragCoord / iResolution.xy;
  vec3 col = texture(iChannel0, lw_cover(uv, iChannelResolution[0].xy, iResolution.xy)).rgb;
  fragColor = vec4(col, 1.0);
}
