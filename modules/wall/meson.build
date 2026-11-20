project('kinetic-wall', 'c', version: '0.1.0-dev',
  default_options: ['c_std=gnu11', 'buildtype=release', 'warning_level=2', 'b_lto=true'])

cc = meson.get_compiler('c')
add_project_arguments('-D_GNU_SOURCE', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
  '-DLW_VERSION="' + meson.project_version() + '"', language: 'c')

wl_client = dependency('wayland-client')
wl_egl = dependency('wayland-egl')
egl = dependency('egl')
gles = dependency('glesv2')
png = dependency('libpng')
jpeg = dependency('libjpeg')
webp = dependency('libwebp')
m = cc.find_library('m')
pipewire = dependency('libpipewire-0.3', required: get_option('audio'))

wlp = dependency('wayland-protocols').get_variable('pkgdatadir')
scanner = find_program('wayland-scanner')
protos = [
  'protocols/wlr-layer-shell-unstable-v1.xml',
  wlp / 'stable/xdg-shell/xdg-shell.xml',
  wlp / 'stable/viewporter/viewporter.xml',
  wlp / 'stable/tablet/tablet-v2.xml',
  wlp / 'staging/fractional-scale/fractional-scale-v1.xml',
  wlp / 'staging/cursor-shape/cursor-shape-v1.xml',
]
proto_src = []
foreach p : protos
  base = p.split('/')[-1].replace('.xml', '')
  proto_src += custom_target(base + '-h', input: p, output: base + '-protocol.h',
    command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  proto_src += custom_target(base + '-c', input: p, output: base + '-protocol.c',
    command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

src = [
  'src/main.c', 'src/util.c', 'src/toml.c', 'src/image.c', 'src/gfx.c', 'src/wallpaper.c',
  'src/engine.c', 'src/wayland.c', 'src/ipc.c', 'src/offscreen.c', 'src/overlay.c',
  'src/lens.c', 'src/plugin.c', 'src/motion.c', 'src/mod_theme.c', 'src/mod_hyprland.c', 'src/mod_power.c', 'src/mod_shell.c',
  'src/json.c', 'src/layers.c',
]
deps = [wl_client, wl_egl, egl, gles, png, jpeg, webp, m]
if pipewire.found()
  src += 'src/mod_audio.c'
  deps += pipewire
  add_project_arguments('-DLW_AUDIO=1', language: 'c')
endif

executable('kinetic-wall', src, proto_src, dependencies: deps, install: true)
install_subdir('wallpapers', install_dir: get_option('datadir') / 'kinetic')
