#!/usr/bin/env python3
"""Renders the README's images, offscreen, with the wallpaper engine.

    docs/media.py [--theme NAME] [name ...]   all of them, or: gallery wallpapers glass parallax passthrough

Needs a built engine (make -C modules/wall) and ImageMagick. Nothing
touches the desktop: every frame is `kinetic-wall thumbnail`, run with its own
scratch state, so the colors are always those of one Omarchy theme (Hackerman,
unless --theme names another) and no saved wallpaper settings creep in.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs")
ENGINE = os.path.join(ROOT, "modules/wall/build/kinetic-wall")
WALLPAPERS = os.path.join(ROOT, "modules/wall/wallpapers")
ENV = dict(os.environ, KINETIC_WALL_PATH=WALLPAPERS)
THEME = "hackerman"  # the README's images all use one theme
FONT = "Noto-Sans-Medium"
BG = "#0d0f14"
INK = "#e6e9ef"
MUTED = "#9aa3b2"


def run(*args, env=None):
    subprocess.run([str(a) for a in args], check=True, env=env or ENV, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)


def use_theme(name, tmp):
    """Render with this Omarchy theme: a scratch state folder whose current theme is it."""
    for base in (os.path.expanduser("~/.config/omarchy/themes"), "/usr/share/omarchy/themes"):
        d = os.path.join(base, name)
        if os.path.isfile(os.path.join(d, "colors.toml")):
            break
    else:
        sys.exit(f"no Omarchy theme named {name}")
    state = os.path.join(tmp, "state")
    os.makedirs(os.path.join(state, "omarchy/current"))
    os.symlink(d, os.path.join(state, "omarchy/current/theme"))
    with open(os.path.join(state, "omarchy/current/theme.name"), "w") as f:
        f.write(name + "\n")
    ENV.update(XDG_STATE_HOME=state, XDG_CONFIG_HOME=os.path.join(tmp, "config"), XDG_CACHE_HOME=os.path.join(tmp, "cache"))


def frame(wall, path, w, h, t, *extra):
    run(ENGINE, "thumbnail", wall, path, f"{w}x{h}", t, *extra)


def timelapse(wall, tmp, path, w, h, t):
    """A simulation as it stands t seconds after it starts. A thumbnail only simulates the
    wallpaper's preview_warmup before t, so this renders a copy whose warmup is all of t:
    one encounter, played from the beginning, frame after frame."""
    walls = os.path.join(tmp, "timelapse")
    toml = os.path.join(walls, wall, "wallpaper.toml")
    if not os.path.isdir(os.path.join(walls, wall)):
        shutil.copytree(os.path.join(WALLPAPERS, wall), os.path.join(walls, wall))
    with open(toml) as f:
        s = f.read()
    with open(toml, "w") as f:
        f.write(re.sub(r"(?m)^preview_warmup = .*$", f"preview_warmup = {t:.3f}", s))
    run(ENGINE, "thumbnail", wall, path, f"{w}x{h}", f"{t:.3f}", env=dict(ENV, KINETIC_WALL_PATH=walls))


def gif(frames, out, delay, colors=160):
    """frames -> an optimized looping GIF (delay in hundredths of a second)."""
    run("magick", "-delay", delay, "-loop", 0, *frames, "-colors", colors, "-layers", "Optimize", out)


def label(src, text, dst, w):
    """A caption strip under an image."""
    run("magick", src, "(", "-size", f"{w}x34", "-background", BG, "-fill", INK, "-font", FONT, "-pointsize", 15,
        "-gravity", "center", f"label:{text}", ")", "-append", dst)


# ---------- the wallpaper gallery ----------
GALLERY = ["aurora", "orbit", "synthwave", "topo", "reaction", "skyline",
           "swarm", "tide", "wormhole", "lava", "particle-life", "plexus"]
NAMES = {"particle-life": "Particle life"}


def gallery(tmp):
    tiles = []
    for wall in GALLERY:
        p = os.path.join(tmp, f"g-{wall}.png")
        frame(wall, p, 400, 250, 8)
        q = os.path.join(tmp, f"gl-{wall}.png")
        label(p, NAMES.get(wall, wall.capitalize()), q, 400)
        tiles.append(q)
    run("magick", "montage", *tiles, "-tile", "4x", "-geometry", "+6+6", "-background", BG,
        "-depth", 8, "-strip", os.path.join(OUT, "wallpapers.png"))


# ---------- a few of them moving ----------
# Galaxy Collision as a time-lapse: a whole encounter (approach, first pass and tidal
# tails, merger: about 55 s) in the loop, the others in real time.
COLLISION = 55


def wallpapers_gif(tmp):
    walls, n, w, h = ["aurora", "galaxy", "wormhole", "topo"], 48, 360, 225
    frames = []
    for i in range(n):
        t = 6 + i / 12
        tiles = []
        for wall in walls:
            p = os.path.join(tmp, f"m-{wall}-{i}.png")
            if wall == "galaxy":
                timelapse(wall, tmp, p, w, h, max(1 / 60, COLLISION * i / (n - 1)))
            else:
                frame(wall, p, w, h, t)
            tiles.append(p)
        f = os.path.join(tmp, f"m-{i:03}.png")
        run("magick", "montage", *tiles, "-tile", "2x", "-geometry", "+3+3", "-background", BG, f)
        frames.append(f)
    gif(frames, os.path.join(OUT, "wallpapers.gif"), 8)


# ---------- the glass ----------
def glass(tmp):
    w, h = 560, 350
    bar = "16,10,528,30,15"
    win = "70,72,420,230,14"
    looks = [
        ("Clear", ["30", "0", "0"], "bevel=40,curve=1,sat=1.1,spec=0.5,light=120"),
        ("Frosted", ["18", "2.5", "0.15"], "bevel=36,curve=1,sat=1.1,tint=0.15,spec=0.3,light=120"),
        ("Prismatic", ["42", "0.6", "0.9"], "bevel=60,curve=1.6,sat=1.25,spec=0.6,light=60"),
    ]
    panels = []
    for name, optics, extra in looks:
        p = os.path.join(tmp, f"glass-{name}.png")
        frame("topo", p, w, h, 10, "--glass", *optics, f"{bar};{win}", extra)
        q = os.path.join(tmp, f"glass-l-{name}.png")
        label(p, name, q, w)
        panels.append(q)
    run("magick", *panels, "+append", "+repage", "-bordercolor", BG, "-border", 6, "-depth", 8, "-strip",
        os.path.join(OUT, "glass.png"))


# ---------- a workspace switch, and a pass-through trip ----------
def bezier(p, x):
    """Hyprland's cubic bezier (0,0) p1 p2 (1,1): y at x."""
    x1, y1, x2, y2 = p
    lo, hi = 0.0, 1.0
    for _ in range(40):
        t = (lo + hi) / 2
        bx = 3 * (1 - t) ** 2 * t * x1 + 3 * (1 - t) * t * t * x2 + t ** 3
        lo, hi = (t, hi) if bx < x else (lo, t)
    t = (lo + hi) / 2
    return 3 * (1 - t) ** 2 * t * y1 + 3 * (1 - t) * t * t * y2 + t ** 3


GLIDE = (0.65, 0.05, 0.36, 1)  # the parallax preset's switch curve
LAYOUTS = [  # windows per workspace: x, y, w, h as fractions of the screen
    [(0.04, 0.12, 0.44, 0.80), (0.52, 0.12, 0.44, 0.38), (0.52, 0.54, 0.44, 0.38)],
    [(0.04, 0.12, 0.92, 0.80)],
    [(0.04, 0.12, 0.44, 0.80), (0.52, 0.12, 0.44, 0.80)],
    [(0.04, 0.12, 0.29, 0.80), (0.355, 0.12, 0.29, 0.80), (0.67, 0.12, 0.29, 0.80)],
]


def desktop(tmp, i, pos, parallax, w, h, caption):
    """One frame: the wallpaper panned by pos * parallax screens, the windows of
    each workspace at (ws - pos) screens, and a caption."""
    bg = os.path.join(tmp, f"d-{i:03}.png")
    frame("topo", bg, w, h, 10, "--parallax", f"{pos * parallax:.5f},0")
    draw = []
    for ws, layout in enumerate(LAYOUTS):
        dx = (ws - pos) * w
        if dx <= -w or dx >= w:
            continue
        for fx, fy, fw, fh in layout:
            x0, y0 = dx + fx * w, fy * h
            draw += ["-draw", f"roundrectangle {x0:.1f},{y0:.1f} {x0 + fw * w:.1f},{y0 + fh * h:.1f} 10,10"]
    out = os.path.join(tmp, f"f-{i:03}.png")
    run("magick", bg, "-fill", "#141824e6", "-stroke", "#8fb3ff", "-strokewidth", 2, *draw,
        "-stroke", "none", "-fill", "#0d0f14b0", "-draw", f"rectangle 0,{h - 30} {w},{h}",
        "-fill", INK, "-font", FONT, "-pointsize", 14, "-gravity", "southwest", "-annotate", "+12+8", caption, out)
    return out


def parallax(tmp):
    w, h, ms, fps = 640, 400, 500, 30
    frames = []
    steps = int(ms / 1000 * fps)
    seq = [0.0] * 12 + [bezier(GLIDE, k / steps) for k in range(steps + 1)] + [1.0] * 24
    seq += [1 - bezier(GLIDE, k / steps) for k in range(steps + 1)] + [0.0] * 12
    for i, pos in enumerate(seq):
        frames.append(desktop(tmp, i, pos, 0.25, w, h,
                              f"Workspace {1 + pos:.2f}   ·   windows move one screen, the wallpaper a quarter"))
    gif(frames, os.path.join(OUT, "parallax.gif"), 100 // fps)


def passthrough(tmp):
    w, h, fps = 640, 400, 30
    trip_ms = 500 + 110 * 2  # the switch, plus 110 ms for each workspace beyond the first
    steps = int(trip_ms / 1000 * fps)
    seq = [0.0] * 12 + [3 * bezier(GLIDE, k / steps) for k in range(steps + 1)] + [3.0] * 30
    frames = [desktop(tmp, i, pos, 0.25, w, h, f"Super + 4 from workspace 1   ·   passing {1 + pos:.2f}")
              for i, pos in enumerate(seq)]
    gif(frames, os.path.join(OUT, "pass-through.gif"), 100 // fps)


JOBS = {"gallery": gallery, "wallpapers": wallpapers_gif, "glass": glass, "parallax": parallax,
        "passthrough": passthrough}

if __name__ == "__main__":
    if not os.access(ENGINE, os.X_OK):
        sys.exit("build the engine first: make -C modules/wall")
    if not shutil.which("magick"):
        sys.exit("needs ImageMagick (magick)")
    args = sys.argv[1:]
    theme = THEME
    if "--theme" in args:
        i = args.index("--theme")
        if i + 1 >= len(args):
            sys.exit("--theme needs a name")
        theme = args[i + 1]
        del args[i:i + 2]
    names = args or list(JOBS)
    for n in names:
        if n not in JOBS:
            sys.exit(f"no image named {n} ({', '.join(JOBS)})")
    with tempfile.TemporaryDirectory() as tmp:
        use_theme(theme, tmp)
        for n in names:
            JOBS[n](tmp)
            print(n)
