import QtQuick
import qs.Commons
import qs.Ui
import "../components"

// The pill bar: background, presets and every option from Kinetic's schema.
Column {
  id: page
  property var ctx: null
  property bool withGlass: true   // the app shows the glass options on its Glass page instead
  readonly property var cfg: ctx ? ctx.shellCfg : null
  spacing: Style.space(12)

  component PlainText: Text {
    textFormat: Text.PlainText
    color: page.ctx.fg
    font.family: page.ctx.fontFamily
    font.pixelSize: Style.font.body
    elide: Text.ElideRight
  }

  // The pill needs Kinetic's build of the bar.
  Column {
    width: parent.width
    spacing: Style.space(6)
    visible: !page.ctx.kineticBar
    PlainText {
      width: parent.width
      text: page.ctx.tr("bar.notKinetic").replace("%1", page.cfg ? page.cfg.id : "") // before the strings load, tr gives back the key, which has no %1
      wrapMode: Text.Wrap
      elide: Text.ElideNone
      opacity: 0.75
      font.pixelSize: Style.font.bodySmall
    }
    Button {
      text: page.ctx.tr("bar.useKinetic")
      bordered: true
      foreground: page.ctx.fg
      fontFamily: page.ctx.fontFamily
      onClicked: page.ctx.send("bar", ["use"])
    }
  }

  PanelSectionHeader { text: page.ctx.tr("bar.background"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
  ButtonGroup {
    options: [
      { value: "pill", label: page.ctx.tr("bar.bg.pill") },
      { value: "glassy", label: page.ctx.tr("bar.bg.glassy") },
      { value: "opaque", label: page.ctx.tr("bar.bg.opaque") },
      { value: "none", label: page.ctx.tr("bar.bg.none") }
    ]
    value: page.cfg ? page.cfg.background : "opaque"
    foreground: page.ctx.fg
    fontFamily: page.ctx.fontFamily
    onChanged: function(v) { page.ctx.send("bar", v === "pill" ? ["on"] : ["off", v]) }
  }

  PlainText {
    width: parent.width
    visible: page.ctx.pillOn
    text: page.ctx.tr("bar.glass." + page.ctx.glassNow)
    wrapMode: Text.Wrap
    elide: Text.ElideNone
    opacity: 0.6
    font.pixelSize: Style.font.caption
  }

  PanelSectionHeader { text: page.ctx.tr("presets"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
  Flow {
    width: parent.width
    spacing: Style.space(4)
    Repeater {
      model: page.ctx.barPresets
      delegate: Button {
        required property var modelData
        text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
        bordered: true
        foreground: page.ctx.fg
        fontFamily: page.ctx.fontFamily
        fontSize: Style.font.bodySmall
        onClicked: page.ctx.send("bar", ["preset", modelData])
      }
    }
    Button {
      iconText: "󰑓"
      tooltipText: page.ctx.tr("bar.resetAll")
      bordered: true
      foreground: page.ctx.fg
      onClicked: page.ctx.send("bar", ["reset"])
    }
  }

  // Every option, grouped as the schema groups them.
  Repeater {
    model: page.ctx.pillOn ? page.ctx.barGroups.filter(function(g) { return page.withGlass || g.group !== "Glass" }) : []
    delegate: Column {
      required property var modelData
      width: page.width
      spacing: Style.space(10)
      PanelSectionHeader { text: page.ctx.tr("bar.group." + modelData.group); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
      Repeater {
        model: modelData.items
        delegate: SettingRow {
          required property var modelData
          ctx: page.ctx
          item: modelData
          kind: "bar"
        }
      }
    }
  }
}
