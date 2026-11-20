#!/usr/bin/env python3
"""Build the kinetic.bar shell plugin from Omarchy's stock bar.

Kinetic does not keep a frozen copy of the 3,600-line stock bar. It copies the
bar that ships with the installed Omarchy and applies a short list of anchored
hooks to Bar.qml. The pill itself lives in separate files (qml/) that are
copied beside it. After an Omarchy update, rebuilding picks up every upstream
change. If an anchor is gone (upstream rewrote that part), the build stops,
names the hook, and leaves the previous plugin in place.

    build.py [--stock DIR] [--out DIR] [--check]
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
KINETIC = os.path.dirname(os.path.dirname(HERE))
HOME = os.path.expanduser("~")
OMARCHY = os.environ.get("OMARCHY_PATH") or "/usr/share/omarchy"

# ---------------------------------------------------------------------------
# The hooks. Each one is (name, anchor, replacement): the anchor must occur in
# the stock Bar.qml exactly once. "{anchor}" in a replacement stands for the
# anchor text itself, so most hooks only add lines around it.
# ---------------------------------------------------------------------------

ROOT_PROPS = '''{anchor}
  // ---- Kinetic: the pill background (bar.background "pill", options in bar.pill) ----
  // Options and defaults come from Kinetic's schema.json (KineticEnv.SCHEMA).
  property bool pill: false
  property var pillConfig: KineticSchema.normalize(null, KineticEnv.SCHEMA)
  readonly property bool pillIslands: pill && pillConfig.shape === "islands"
  // Space the strip gains around the widgets in pill mode, across the bar.
  readonly property int pillOuterEdge: pill ? pillConfig.margin + pillConfig.padding : 0
  readonly property int pillInnerEdge: pill ? pillConfig.padding + pillConfig.gap : 0
  readonly property real pillCrossSize: barSize + 2 * pillConfig.padding
  readonly property real pillRadius: pillConfig.radius === "full" ? pillCrossSize / 2 : Math.min(pillConfig.radius, pillCrossSize / 2)
  // How far the widgets sit in from a capsule's rounded end.
  readonly property real pillEndInset: Math.round(pillRadius * 0.6)
  readonly property real pillIslandInset: pillConfig.islandPadding + Math.round(pillRadius * 0.35)
  // The margin the start and end sections keep from the bar's ends.
  readonly property real sectionEndMargin: pillIslands ? pillIslandInset : Style.space(8)

  // Glass. While Kinetic's wallpaper engine is drawing, it bends the real
  // pixels behind the pill (PillGlass sends it the capsules); otherwise
  // Hyprland blurs behind the bar.
  property string wallSignal: ""
  readonly property bool wallShowing: wallSignal.indexOf("on") === 0
  readonly property bool pillClear: pill && pillConfig.opacity < 1
  readonly property bool engineGlass: pillClear && (pillConfig.glass === "wallpaper" || (pillConfig.glass === "auto" && wallShowing))
  readonly property bool hyprBlur: pillClear && !engineGlass && (pillConfig.glass === "auto" || pillConfig.glass === "blur")
  readonly property string pillBlurKey: hyprBlur ? "on " + pillConfig.blurThreshold.toFixed(3) : "off"
  property string pillSyncedBlur: ""
  // Startup settles first (config injected, engine signal read), so the
  // toggle is written once instead of flapping through Hyprland reloads.
  property bool pillBlurReady: false
  onPillBlurKeyChanged: if (pillBlurReady) syncPillBlur()
  Timer {
    interval: 800
    running: true
    onTriggered: { root.pillBlurReady = true; root.syncPillBlur() }
  }

  function syncPillBlur() {
    if (pillBlurKey === pillSyncedBlur) return
    pillSyncedBlur = pillBlurKey
    Quickshell.execDetached([KineticEnv.BLUR].concat(pillBlurKey.split(" ")))
  }

  FileView {
    path: KineticEnv.WALL_SIGNAL
    blockLoading: true
    watchChanges: true
    printErrors: false
    onFileChanged: reload()
    onLoaded: root.wallSignal = text().trim()
    onLoadFailed: root.wallSignal = ""
  }
'''

HOOKS = [
    ("imports",
     'import "BarModel.js" as BarModel\n',
     '{anchor}import "schema.js" as KineticSchema\nimport "KineticEnv.js" as KineticEnv\n'),

    ("root properties",
     '  property string glassSyncedLook: ""\n',
     ROOT_PROPS),

    ("thickness",
     '  function thicknessFor(screen) {\n    if (root.vertical || !screen) return root.barSize\n',
     '  function thicknessFor(screen) {\n'
     '    return baseThicknessFor(screen) + root.pillOuterEdge + root.pillInnerEdge\n'
     '  }\n\n'
     '  // The stock thickness; the pill adds its margins on top (Kinetic).\n'
     '  function baseThicknessFor(screen) {\n'
     '    if (root.vertical || !screen) return root.barSize\n'),

    ("config",
     '    var look = config.background || (config.transparent === true ? "none" : "opaque")\n',
     '{anchor}'
     '    pillConfig = KineticSchema.normalize(config.pill, KineticEnv.SCHEMA)\n'
     '    pill = look === "pill"\n'),

    ("double-click cycle",
     '    Util.execDetached("omarchy-bar background cycle")\n',
     '    // Kinetic: the pill joins the cycle after opaque: clear, glassy, opaque, pill.\n'
     '    if (root.pill) Util.execDetached("omarchy-bar background none")\n'
     '    else if (!root.glassy && !root.requestedTransparent) Quickshell.execDetached([KineticEnv.KINETIC, "bar", "on"])\n'
     '    else Util.execDetached("omarchy-bar background cycle")\n'),

    ("glass per screen",
     '    // The bar itself, along its screen edge.\n',
     '    PillGlass {\n'
     '      id: pillGlass\n'
     '      bar: root\n'
     '      screenName: barWindow.screen ? barWindow.screen.name : ""\n'
     '    }\n\n'
     '{anchor}'),

    ("strip background",
     '      Rectangle {\n'
     '        anchors.fill: parent\n'
     '        color: root.transparent ? "transparent" : (root.glassy ? root.glassBackground : root.background)\n'
     '      }\n',
     '      Rectangle {\n'
     '        anchors.fill: parent\n'
     '        visible: !root.pill\n'
     '        color: root.transparent ? "transparent" : (root.glassy ? root.glassBackground : root.background)\n'
     '      }\n'),

    ("widgets inside the pill (open)",
     '      Loader {\n'
     '        anchors.fill: parent\n'
     '        sourceComponent: root.vertical ? verticalBar : horizontalBar\n',
     '      // Kinetic: the widgets sit inside the pill.\n'
     '      PillContent {\n'
     '        bar: root\n'
     '        strip: barStrip\n'
     '        glass: pillGlass\n\n'
     '{anchor}'),

    ("widgets inside the pill (close)",
     '          Component.onDestruction: if (peeking) root.setBarHovered(false)\n'
     '        }\n'
     '      }\n',
     '{anchor}'
     '      }\n'),

    ("islands (horizontal)",
     '        CenterModules {\n'
     '          anchors.fill: parent\n'
     '          window: barWindow\n'
     '          pool: horizontalPool\n'
     '          drawn: !barWindow.centerBesideRight\n'
     '        }\n\n'
     '        LeftModules {\n'
     '          window: barWindow\n'
     '          pool: horizontalPool\n'
     '          anchors.left: parent.left\n'
     '          anchors.leftMargin: Style.space(8)\n',
     '        PillIslands {\n'
     '          anchors.fill: parent\n'
     '          bar: root\n'
     '          glass: pillGlass\n'
     '          startList: hLeftModules\n'
     '          endList: rightModules\n'
     '          centerGroup: barWindow.centerBesideRight ? hBesideSections : hCenterModules\n'
     '        }\n\n'
     '        CenterModules {\n'
     '          id: hCenterModules\n'
     '          anchors.fill: parent\n'
     '          window: barWindow\n'
     '          pool: horizontalPool\n'
     '          drawn: !barWindow.centerBesideRight\n'
     '        }\n\n'
     '        LeftModules {\n'
     '          id: hLeftModules\n'
     '          window: barWindow\n'
     '          pool: horizontalPool\n'
     '          anchors.left: parent.left\n'
     '          anchors.leftMargin: root.sectionEndMargin\n'),

    ("islands (horizontal right margin)",
     '          id: rightModules\n'
     '          anchors.right: parent.right\n'
     '          anchors.rightMargin: Style.space(8)\n',
     '          id: rightModules\n'
     '          anchors.right: parent.right\n'
     '          anchors.rightMargin: root.sectionEndMargin\n'),

    ("islands (beside the right section)",
     '        CenterSections {\n'
     '          window: barWindow\n'
     '          pool: horizontalPool\n'
     '          active: barWindow.centerBesideRight\n',
     '        CenterSections {\n'
     '          id: hBesideSections\n'
     '          window: barWindow\n'
     '          pool: horizontalPool\n'
     '          active: barWindow.centerBesideRight\n'),

    ("islands (vertical)",
     '        CenterModules {\n'
     '          anchors.fill: parent\n'
     '          window: barWindow\n'
     '          pool: verticalPool\n'
     '        }\n\n'
     '        LeftModules {\n'
     '          window: barWindow\n'
     '          pool: verticalPool\n'
     '          anchors.top: parent.top\n'
     '          anchors.topMargin: Style.space(8)\n',
     '        PillIslands {\n'
     '          anchors.fill: parent\n'
     '          bar: root\n'
     '          glass: pillGlass\n'
     '          startList: vTopModules\n'
     '          endList: vBottomModules\n'
     '          centerGroup: vCenterModules\n'
     '        }\n\n'
     '        CenterModules {\n'
     '          id: vCenterModules\n'
     '          anchors.fill: parent\n'
     '          window: barWindow\n'
     '          pool: verticalPool\n'
     '        }\n\n'
     '        LeftModules {\n'
     '          id: vTopModules\n'
     '          window: barWindow\n'
     '          pool: verticalPool\n'
     '          anchors.top: parent.top\n'
     '          anchors.topMargin: root.sectionEndMargin\n'),

    ("islands (vertical bottom)",
     '        RightModules {\n'
     '          window: barWindow\n'
     '          pool: verticalPool\n'
     '          anchors.bottom: parent.bottom\n'
     '          anchors.bottomMargin: Style.space(8)\n',
     '        RightModules {\n'
     '          id: vBottomModules\n'
     '          window: barWindow\n'
     '          pool: verticalPool\n'
     '          anchors.bottom: parent.bottom\n'
     '          anchors.bottomMargin: root.sectionEndMargin\n'),

    ("center group extent (CenterModules)",
     '    CenterSections {\n'
     '      anchors.fill: parent\n'
     '      window: centerRoot.window\n'
     '      pool: centerRoot.pool\n'
     '      active: centerRoot.drawn\n'
     '    }\n',
     '    // Kinetic: the center group\'s extent, for the pill islands.\n'
     '    readonly property real groupStart: (root.vertical ? y : x) + centerSectionsItem.groupStart\n'
     '    readonly property real groupEnd: (root.vertical ? y : x) + centerSectionsItem.groupEnd\n'
     '    readonly property bool hasContent: centerSectionsItem.hasContent\n\n'
     '    CenterSections {\n'
     '      id: centerSectionsItem\n'
     '      anchors.fill: parent\n'
     '      window: centerRoot.window\n'
     '      pool: centerRoot.pool\n'
     '      active: centerRoot.drawn\n'
     '    }\n'),

    ("center group extent (CenterSections)",
     '    readonly property real boxStart: root.vertical ? y + centerBox.y : x + centerBox.x\n',
     '{anchor}'
     '    // Kinetic: the whole center group\'s extent in the parent, for the pill islands.\n'
     '    readonly property real groupStart: boxStart - (beginList.visible ? (root.vertical ? beginList.height : beginList.width) : 0)\n'
     '    readonly property real groupEnd: boxStart + (root.vertical ? centerBox.height : centerBox.width)\n'
     '      + (endList.visible ? (root.vertical ? endList.height : endList.width) : 0)\n'
     '    readonly property bool hasContent: active && groupEnd - groupStart > 0.5\n'),
]


class HookError(Exception):
    pass


def patch(text):
    for name, anchor, replacement in HOOKS:
        count = text.count(anchor)
        if count != 1:
            raise HookError(f"hook '{name}': its anchor occurs {count} times in the stock Bar.qml (expected once)")
        text = text.replace(anchor, replacement.replace("{anchor}", anchor))
    return text


def js_string(s):
    return json.dumps(s)


def env_js():
    """KineticEnv.js: this install's paths and the pill schema, for the bar and the panel."""
    def schema(path):
        with open(path, encoding="utf-8") as f:
            s = json.load(f)
        s.pop("//", None)
        return s

    config = os.environ.get("XDG_CONFIG_HOME") or os.path.join(HOME, ".config")
    state = os.environ.get("XDG_STATE_HOME") or os.path.join(HOME, ".local/state")
    bindir = os.environ.get("KINETIC_BIN") or os.path.join(HOME, ".local/bin")
    values = {
        "KINETIC": os.path.join(bindir, "kinetic"),
        "WALL": os.path.join(bindir, "kinetic-wall"),
        "BLUR": os.path.join(bindir, "kinetic-bar-blur"),
        "WALL_SIGNAL": os.path.join(state, "kinetic/signals/wall"),
        "PRESET_STATE": os.path.join(state, "kinetic/preset"),
        "MOTION_CONFIG": os.path.join(config, "kinetic/motion.json"),
    }
    lines = [".pragma library",
             "// Generated by Kinetic (modules/bar/build.py); do not edit. This install's paths and the option schemas."]
    lines += [f"var {k} = {js_string(v)}" for k, v in values.items()]
    lines.append(f"var SCHEMA = {json.dumps(schema(os.path.join(HERE, 'schema.json')), separators=(',', ':'))}")
    lines.append(f"var MOTION_SCHEMA = {json.dumps(schema(os.path.join(KINETIC, 'modules/motion/schema.json')), separators=(',', ':'))}")
    return "\n".join(lines) + "\n"


def write_env(dest):
    """The shared pieces a Kinetic shell plugin needs: KineticEnv.js and schema.js."""
    with open(os.path.join(dest, "KineticEnv.js"), "w", encoding="utf-8") as f:
        f.write(env_js())
    shutil.copy2(os.path.join(KINETIC, "lib", "schema.js"), dest)


def build(stock, out, check=False):
    bar_qml = os.path.join(stock, "Bar.qml")
    if not os.path.isfile(bar_qml):
        raise HookError(f"no stock bar at {stock}")
    with open(bar_qml, encoding="utf-8") as f:
        stock_text = f.read()
    patched = patch(stock_text)
    if check:
        return patched

    parent = os.path.dirname(out.rstrip("/"))
    os.makedirs(parent, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix=".kinetic.bar.", dir=parent)
    try:
        shutil.copytree(stock, tmp, dirs_exist_ok=True)
        with open(os.path.join(tmp, "Bar.qml"), "w", encoding="utf-8") as f:
            f.write(patched)
        for name in os.listdir(os.path.join(HERE, "qml")):
            shutil.copy2(os.path.join(HERE, "qml", name), tmp)
        write_env(tmp)
        with open(os.path.join(stock, "manifest.json"), encoding="utf-8") as f:
            manifest = json.load(f)
        manifest.update({
            "id": "kinetic.bar",
            "name": "Kinetic Bar",
            "author": "Kinetic",
            "description": "Omarchy's bar with Kinetic's pill background and wallpaper glass. Generated by `kinetic bar build`.",
        })
        manifest["omarchy"] = dict(manifest.get("omarchy") or {}, clonedFrom="omarchy.bar")
        manifest["kinetic"] = {"stockSha256": hashlib.sha256(stock_text.encode()).hexdigest()}
        with open(os.path.join(tmp, "manifest.json"), "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2)
            f.write("\n")
        with open(os.path.join(tmp, "AGENTS.md"), "w", encoding="utf-8") as f:
            f.write("# Generated\n\nThis plugin is generated by `kinetic bar build` from Omarchy's stock bar plus\n"
                    "Kinetic's hooks (modules/bar in the Kinetic repo). Edits here are lost on the next build.\n")
        old = out.rstrip("/") + ".old"
        if os.path.lexists(old):
            shutil.rmtree(old)
        if os.path.lexists(out):
            os.rename(out, old)
        os.rename(tmp, out)
        if os.path.lexists(old):
            shutil.rmtree(old)
    except BaseException:
        shutil.rmtree(tmp, ignore_errors=True)
        raise
    return patched


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stock", default=os.path.join(OMARCHY, "shell/plugins/bar"))
    ap.add_argument("--out", default=os.path.join(HOME, ".config/omarchy/plugins/kinetic.bar"))
    ap.add_argument("--check", action="store_true", help="only check that every hook still applies")
    ap.add_argument("--print", action="store_true", help="print the patched Bar.qml")
    ap.add_argument("--env", metavar="DIR", help="only write KineticEnv.js and schema.js into DIR (for the panel)")
    args = ap.parse_args()
    if args.env:
        write_env(args.env)
        return 0
    try:
        text = build(args.stock, args.out, check=args.check or args.print)
    except HookError as e:
        print(f"kinetic bar build: {e}", file=sys.stderr)
        print("The previous kinetic.bar (if any) is untouched.", file=sys.stderr)
        return 1
    if args.print:
        sys.stdout.write(text)
    elif args.check:
        print(f"all {len(HOOKS)} hooks apply to {args.stock}/Bar.qml")
    else:
        print(f"built {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
