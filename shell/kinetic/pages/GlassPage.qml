import QtQuick
import qs.Commons
import qs.Ui
import "../components"
import "../Model.js" as Model

// Rendered glass: the wallpaper bent behind the bar's capsules and behind windows.
// One switch for all of it, one look for both (kind "glass" sets the engine's
// window options and the bar's matching ones together), then what only the bar,
// only the windows, and the background have.
Column {
  id: page
  property var ctx: null
  spacing: Style.space(12)

  // the shared look, in this order; the engine's items carry the ranges and labels
  readonly property var lookKeys: ["window_refract", "window_frost", "window_dispersion", "window_curve",
                                   "window_saturation", "window_brightness", "window_specular", "window_light"]
  function engineItems(keys) {
    var out = []
    for (var i = 0; i < keys.length; i++) {
      var o = page.ctx ? page.ctx.engineOption(keys[i]) : null
      if (o) out.push(o)
    }
    return out
  }
  function barItems(keys) {
    var out = [], rows = page.ctx ? page.ctx.barRows : []
    for (var i = 0; i < keys.length; i++)
      for (var j = 0; j < rows.length; j++) if (rows[j].key === keys[i]) out.push(rows[j])
    return out
  }

  component PlainText: Text {
    textFormat: Text.PlainText
    color: page.ctx.fg
    font.family: page.ctx.fontFamily
    font.pixelSize: Style.font.body
    elide: Text.ElideNone
    wrapMode: Text.Wrap
  }

  // ---------- on / off ----------
  Item {
    width: parent.width
    implicitHeight: Math.max(master.implicitHeight, masterText.implicitHeight)
    Column {
      id: masterText
      anchors.left: parent.left
      anchors.right: master.left
      anchors.rightMargin: Style.space(12)
      anchors.verticalCenter: parent.verticalCenter
      spacing: Style.space(2)
      PlainText { width: parent.width; text: page.ctx.tr("glass.master"); font.bold: true }
      PlainText {
        width: parent.width
        text: page.ctx.tr(page.ctx.glassOn ? "glass.masterOn" : "glass.masterOff")
        opacity: 0.6
        font.pixelSize: Style.font.caption
      }
    }
    ToggleSwitch {
      id: master
      anchors.right: parent.right
      anchors.verticalCenter: parent.verticalCenter
      checked: page.ctx.glassOn
      foreground: page.ctx.fg
      onToggled: page.ctx.setGlass(!checked)
    }
  }

  PlainText { width: parent.width; text: page.ctx.tr("glass.about"); opacity: 0.7; font.pixelSize: Style.font.bodySmall }

  Column {
    width: parent.width
    spacing: Style.space(12)
    visible: page.ctx.glassOn

    // ---------- one look for the bar and the windows ----------
    PanelSectionHeader { text: page.ctx.tr("glass.look"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
    PlainText { width: parent.width; text: page.ctx.tr("glass.lookAbout"); opacity: 0.6; font.pixelSize: Style.font.caption }
    Repeater {
      model: page.engineItems(page.lookKeys)
      delegate: SettingRow {
        required property var modelData
        ctx: page.ctx
        item: modelData
        kind: "glass"
      }
    }

    // ---------- the bar ----------
    PanelSectionHeader { text: page.ctx.tr("glass.bar"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
    PlainText {
      width: parent.width
      text: page.ctx.pillOn ? page.ctx.tr("bar.glass." + page.ctx.glassNow) : page.ctx.tr("glass.barOff")
      opacity: 0.6
      font.pixelSize: Style.font.caption
    }
    Button {
      visible: !page.ctx.pillOn
      text: page.ctx.tr("glass.toBar")
      bordered: true
      foreground: page.ctx.fg
      fontFamily: page.ctx.fontFamily
      onClicked: page.ctx.tab = "bar"
    }
    Repeater {
      model: page.ctx.pillOn ? page.barItems(["glass", "bevel", "blurThreshold"]) : []
      delegate: SettingRow {
        required property var modelData
        ctx: page.ctx
        item: modelData
        kind: "bar"
      }
    }

    // ---------- windows ----------
    PanelSectionHeader { text: page.ctx.tr("glass.windows"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
    PlainText { width: parent.width; text: page.ctx.tr("glass.transparent"); opacity: 0.6; font.pixelSize: Style.font.caption }
    Repeater {
      // the apps list matters only for see-through windows
      model: page.engineItems(page.ctx.windowGlass === "see-through"
                              ? ["window_glass", "window_glass_apps", "window_bevel", "window_tint", "window_hypr_blur"]
                              : ["window_glass", "window_bevel", "window_tint", "window_hypr_blur"])
      delegate: SettingRow {
        required property var modelData
        ctx: page.ctx
        item: modelData
        kind: "config"
      }
    }

    // ---------- the background ----------
    PanelSectionHeader { text: page.ctx.tr("glass.background"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
    Repeater {
      model: page.engineItems(["glass_still"])
      delegate: SettingRow {
        required property var modelData
        ctx: page.ctx
        item: modelData
        kind: "config"
      }
    }
  }
}
