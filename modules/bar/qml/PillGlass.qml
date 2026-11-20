import QtQuick
import Quickshell
import Quickshell.Io
import "KineticEnv.js" as KineticEnv

// Tells the wallpaper engine where this screen's capsules are, so it can draw
// real glass behind them (kinetic-wall lens). One per bar surface. Capsules
// register themselves; any move is debounced into one message. Nothing is
// sent while the engine is not the one drawing the glass.
Item {
  id: glass

  property var bar: null
  property string screenName: ""
  property var surfaces: []
  property string sent: "off"  // nothing to take back yet

  readonly property bool active: !!bar && bar.engineGlass && !bar.barHidden
  visible: false

  function add(s) { surfaces = surfaces.concat([s]); schedule() }
  function remove(s) { surfaces = surfaces.filter(function(x) { return x !== s }); schedule() }
  function schedule() { debounce.restart() }

  onActiveChanged: schedule()
  onScreenNameChanged: schedule()
  Connections {
    target: glass.bar
    function onPillConfigChanged() { glass.schedule() }
    // The engine (re)started: whatever it had is gone, so send again.
    function onWallSignalChanged() { glass.sent = glass.active ? "" : "off"; glass.schedule() }
  }

  function round(v) { return Math.round(v * 100) / 100 }

  function spec() {
    if (!active || !screenName) return "off"
    var rects = []
    for (var i = 0; i < surfaces.length; i++) {
      var s = surfaces[i]
      if (!s || !s.visible || s.width < 2 || s.height < 2) continue
      var p = s.mapToItem(null, 0, 0)
      rects.push([round(p.x), round(p.y), round(s.width), round(s.height), round(s.r)].join(","))
    }
    if (rects.length === 0) return "off"
    var c = bar.pillConfig
    var extras = "bevel=" + round(c.bevel) + ",curve=" + round(c.curvature) + ",sat=" + round(c.saturation)
      + ",bright=" + round(c.brightness) + ",spec=" + round(c.specular) + ",light=" + round(c.lightAngle)
    return [round(c.refraction), round(c.frost), round(c.dispersion), rects.slice(0, 16).join(";"), extras].join(" ")
  }

  function publish() {
    if (!screenName) return
    var s = spec()
    if (s === sent) return
    if (proc.running) { debounce.restart(); return }
    sent = s
    proc.command = [KineticEnv.WALL, "lens", screenName].concat(s.split(" "))
    proc.running = true
  }

  Timer { id: debounce; interval: 120; onTriggered: glass.publish() }
  Process { id: proc }
  Component.onDestruction: if (sent !== "off" && screenName) Quickshell.execDetached([KineticEnv.WALL, "lens", screenName, "off"])
}
