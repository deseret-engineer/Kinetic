# Third-party notices

Kinetic is MIT licensed (see [LICENSE](LICENSE)). It includes, or follows closely,
a few pieces of other people's work, listed here with their notices. The app shows
this list under **Settings → Attributions**.

## Included

### wlr-layer-shell protocol

`modules/wall/protocols/wlr-layer-shell-unstable-v1.xml` is a wire-compatible
subset of the wlr-layer-shell protocol from
[wlr-protocols](https://gitlab.freedesktop.org/wlroots/wlr-protocols). The
wallpaper engine uses it to put its layer on the desktop.

```
Copyright © 2017 Drew DeVault

Permission to use, copy, modify, distribute, and sell this
software and its documentation for any purpose is hereby granted
without fee, provided that the above copyright notice appear in
all copies and that both that copyright notice and this permission
notice appear in supporting documentation, and that the name of
the copyright holders not be used in advertising or publicity
pertaining to distribution of the software without specific,
written prior permission.  The copyright holders make no
representations about the suitability of this software for any
purpose.  It is provided "as is" without express or implied
warranty.

THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.
```

### Hyprland's corner rounding

Kinetic's Hyprland plugin (`modules/glass/plugin/kinetic-glass.cpp`) draws window
glass with the same corner test and edge smoothing as Hyprland's own
`rounding.glsl`, so the glass ends exactly where the window does. The plugin is
built against Hyprland's headers. [Hyprland](https://github.com/hyprwm/Hyprland):

```
BSD 3-Clause License

Copyright (c) 2022-2026, vaxerski
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Credited

- **lowbias32** by Chris Wellons, from
  [hash-prospector](https://github.com/skeeto/hash-prospector) (public domain):
  the integer hash `lw_uhash` that every wallpaper can use.
- **The Art of Code** (Martijn Steinrucken): the short `fract`/`dot` hash behind
  `lw_hash`, a one-liner widely shared on Shadertoy.
- **[Omarchy](https://github.com/omacom/omarchy)** (MIT License): Kinetic's bar
  is built on your machine from Omarchy's own bar, and Kinetic runs inside the
  Omarchy shell.

## Built with

Kinetic builds against these on your machine and does not include them:
[Hyprland](https://github.com/hyprwm/Hyprland), [Quickshell](https://quickshell.org) and
[Qt](https://www.qt.io), [Wayland](https://wayland.freedesktop.org) and
wayland-protocols, [PipeWire](https://pipewire.org), libglvnd (EGL and OpenGL ES),
[libpng](http://www.libpng.org), [libjpeg-turbo](https://libjpeg-turbo.org) and
[libwebp](https://chromium.googlesource.com/webm/libwebp).
