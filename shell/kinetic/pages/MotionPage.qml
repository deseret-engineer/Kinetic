import QtQuick
import qs.Commons
import qs.Ui
import "../components"

// Motion: Hyprland's window and workspace animations, and how far the live
// wallpaper moves when you switch workspace (parallax).
Column {
  id: page
  property var ctx: null
  readonly property bool on: !!ctx && ctx.motionCfg.enabled
  spacing: Style.space(12)

  component PlainText: Text {
    textFormat: Text.PlainText
    color: page.ctx.fg
    font.family: page.ctx.fontFamily
    font.pixelSize: Style.font.body
    elide: Text.ElideRight
  }

  Item {
    width: parent.width
    implicitHeight: Math.max(about.implicitHeight, power.implicitHeight)
    PlainText {
      id: about
      anchors.left: parent.left
      anchors.right: power.left
      anchors.rightMargin: Style.space(10)
      anchors.verticalCenter: parent.verticalCenter
      text: page.on ? page.ctx.tr("motion.on") : page.ctx.tr("motion.off")
      wrapMode: Text.Wrap
      elide: Text.ElideNone
      opacity: 0.7
      font.pixelSize: Style.font.bodySmall
    }
    ToggleSwitch {
      id: power
      anchors.right: parent.right
      anchors.verticalCenter: parent.verticalCenter
      checked: page.on
      foreground: page.ctx.fg
      onToggled: page.ctx.send("motion", [page.on ? "off" : "on"])
    }
  }

  PanelSectionHeader { text: page.ctx.tr("presets"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
  Flow {
    width: parent.width
    spacing: Style.space(4)
    Repeater {
      model: page.ctx.motionPresets
      delegate: Button {
        required property var modelData
        text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
        bordered: true
        foreground: page.ctx.fg
        fontFamily: page.ctx.fontFamily
        fontSize: Style.font.bodySmall
        onClicked: page.ctx.send("motion", ["preset", modelData])
      }
    }
    Button {
      iconText: "󰑓"
      tooltipText: page.ctx.tr("motion.resetAll")
      bordered: true
      foreground: page.ctx.fg
      onClicked: page.ctx.send("motion", ["reset"])
    }
  }

  Repeater {
    model: page.on ? page.ctx.motionGroups : []
    delegate: Column {
      required property var modelData
      width: page.width
      spacing: Style.space(10)
      PanelSectionHeader { text: page.ctx.tr("motion.group." + modelData.group); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
      Repeater {
        model: modelData.items
        delegate: SettingRow {
          required property var modelData
          ctx: page.ctx
          item: modelData
          kind: "motion"
        }
      }
    }
  }
}
