import QtQuick
import qs.Commons
import qs.Ui
import "../components"

// The selection light: a soft glow around windows, drawn by Kinetic's Hyprland
// plugin under them (and under the glass, which bends it). Its options are the
// engine's "Light" group; the rest appear once the light is on.
Column {
  id: page
  property var ctx: null
  spacing: Style.space(12)

  readonly property bool lightOn: { var o = page.ctx ? page.ctx.engineOption("light_mode") : null; return !!o && String(o.value) !== "off" }
  readonly property var groups: [
    { title: "light.which", keys: ["light_mode", "light_unfocused", "light_fade", "light_urgent"] },
    { title: "light.color", keys: ["light_color", "light_strength"] },
    { title: "light.shape", keys: ["light_outside", "light_inside", "light_edge", "light_softness"] },
    { title: "light.motion", keys: ["light_animation", "light_speed", "light_sound"] }
  ]
  function items(keys) {
    var out = []
    for (var i = 0; i < keys.length; i++) {
      var o = page.ctx ? page.ctx.engineOption(keys[i]) : null
      if (o && (page.lightOn || keys[i] === "light_mode")) out.push(o)
    }
    return out
  }

  Repeater {
    model: page.groups
    delegate: Column {
      required property var modelData
      width: page.width
      spacing: Style.space(10)
      visible: page.items(modelData.keys).length > 0
      PanelSectionHeader { text: page.ctx.tr(modelData.title); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
      Repeater {
        model: page.items(modelData.keys)
        delegate: SettingRow {
          required property var modelData
          ctx: page.ctx
          item: modelData
          kind: "config"
        }
      }
    }
  }
}
