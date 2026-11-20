import QtQuick
import qs.Commons
import qs.Ui
import "../components"

// The running wallpaper: transport, presets, parameters, its own frame rate and resolution.
Column {
  id: page
  property var ctx: null
  readonly property var wp: ctx ? ctx.wp : null
  spacing: Style.space(12)

  component PlainText: Text {
    textFormat: Text.PlainText
    color: page.ctx.fg
    font.family: page.ctx.fontFamily
    font.pixelSize: Style.font.body
    elide: Text.ElideRight
  }
  component Tool: Button { bordered: true; foreground: page.ctx.fg }

  PlainText {
    width: parent.width
    visible: !page.wp
    text: page.ctx.tr("noWallpaper")
    wrapMode: Text.Wrap
    opacity: 0.6
  }

  PlainText {
    width: parent.width
    visible: !!page.wp
    text: page.wp ? page.wp.description : ""
    wrapMode: Text.Wrap
    elide: Text.ElideNone
    opacity: 0.7
    font.pixelSize: Style.font.bodySmall
  }

  Row {
    spacing: Style.space(4)
    Tool { iconText: "󰒮"; tooltipText: page.ctx.tr("prev"); onClicked: page.ctx.send("wall", ["prev"]) }
    Tool {
      iconText: page.ctx.info && page.ctx.info.paused ? "󰐊" : "󰏤"
      tooltipText: page.ctx.info && page.ctx.info.paused ? page.ctx.tr("resume") : page.ctx.tr("pause")
      onClicked: page.ctx.send("wall", ["toggle-pause"])
    }
    Tool { iconText: "󰒭"; tooltipText: page.ctx.tr("next"); onClicked: page.ctx.send("wall", ["next"]) }
    Tool { iconText: "󰒝"; tooltipText: page.ctx.tr("random"); onClicked: page.ctx.send("wall", ["random"]) }
    Tool { iconText: "󰑓"; tooltipText: page.ctx.tr("resetAll"); visible: !!page.wp; onClicked: page.ctx.send("wall", ["param-reset"]) }
    Tool { iconText: "󰜉"; tooltipText: page.ctx.tr("restart"); visible: !!page.wp; onClicked: page.ctx.send("wall", ["restart"]) }
  }

  PanelSectionHeader {
    visible: !!page.wp && page.wp.presets.length > 0
    text: page.ctx.tr("presets")
    foreground: page.ctx.fg
    fontFamily: page.ctx.fontFamily
  }
  Flow {
    width: parent.width
    spacing: Style.space(4)
    visible: !!page.wp && page.wp.presets.length > 0
    Repeater {
      model: page.wp ? page.wp.presets : []
      delegate: Button {
        required property var modelData
        text: modelData
        bordered: true
        foreground: page.ctx.fg
        fontFamily: page.ctx.fontFamily
        fontSize: Style.font.bodySmall
        onClicked: page.ctx.send("wall", ["preset", modelData])
      }
    }
  }

  PanelSectionHeader { visible: !!page.wp; text: page.ctx.tr("parameters"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
  PlainText {
    visible: !!page.wp && page.wp.params.length === 0
    text: page.ctx.tr("noParams")
    opacity: 0.5
  }
  Repeater {
    model: page.wp ? page.wp.params : []
    delegate: SettingRow {
      required property var modelData
      ctx: page.ctx
      item: modelData
      kind: "param"
    }
  }

  PanelSectionHeader { visible: !!page.wp; text: page.ctx.tr("performance"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
  SettingRow {
    visible: !!page.wp
    ctx: page.ctx
    kind: "param"
    item: page.wp ? {
      name: "@fps", type: "int", label: page.ctx.tr("fps"), min: 0, max: 120, step: 1,
      value: page.wp.fps, default: page.wp.fps_default,
      description: page.wp.fps <= 0 ? page.ctx.tr("fpsStatic") : ""
    } : ({})
  }
  SettingRow {
    visible: !!page.wp
    ctx: page.ctx
    kind: "param"
    item: page.wp ? {
      name: "@scale", type: "float", label: page.ctx.tr("scale"), min: 0.1, max: 1, step: 0.05,
      value: page.wp.scale, default: page.wp.scale_default
    } : ({})
  }
}
