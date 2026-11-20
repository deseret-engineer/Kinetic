import QtQuick
import qs.Commons
import qs.Ui
import "../Model.js" as Model

// What is showing, its state, and the wallpaper's on/off. `openApp` adds a
// button that opens the Kinetic app (the bar widget shows it).
Item {
  id: hero
  property var ctx: null
  property bool openApp: false
  readonly property bool on: !!ctx && !!ctx.info && ctx.info.enabled
  implicitHeight: Math.max(icon.implicitHeight, text.implicitHeight, power.implicitHeight)

  component PlainText: Text {
    textFormat: Text.PlainText
    color: hero.ctx.fg
    font.family: hero.ctx.fontFamily
    font.pixelSize: Style.font.body
    elide: Text.ElideRight
  }

  PlainText {
    id: icon
    anchors.left: parent.left
    anchors.verticalCenter: parent.verticalCenter
    text: "󰸉"
    font.pixelSize: Style.font.display
    opacity: hero.on ? 1 : 0.5
  }
  Column {
    id: text
    anchors.left: icon.right
    anchors.leftMargin: Style.space(12)
    anchors.right: controls.left
    anchors.rightMargin: Style.space(10)
    anchors.verticalCenter: parent.verticalCenter
    spacing: Style.space(2)
    PlainText {
      width: parent.width
      text: hero.ctx.wp && hero.on ? hero.ctx.wp.name : hero.ctx.tr("title")
      font.pixelSize: Style.font.title
      font.bold: true
    }
    PlainText {
      width: parent.width
      text: hero.ctx.offline ? hero.ctx.tr("offline")
        : !hero.ctx.info ? ""
        : hero.ctx.info.still ? hero.ctx.tr("offStill")
        : !hero.on ? hero.ctx.tr("off")
        : Model.stateLabel(hero.ctx.wp)
      wrapMode: hero.ctx.offline || (!!hero.ctx.info && !hero.on) ? Text.Wrap : Text.NoWrap
      elide: hero.ctx.offline || (!!hero.ctx.info && !hero.on) ? Text.ElideNone : Text.ElideRight
      opacity: 0.6
      font.pixelSize: Style.font.bodySmall
    }
  }
  Row {
    id: controls
    anchors.right: parent.right
    anchors.verticalCenter: parent.verticalCenter
    spacing: Style.space(8)
    Button {
      visible: hero.openApp
      anchors.verticalCenter: parent.verticalCenter
      iconText: "󰒓"
      tooltipText: hero.ctx.tr("openApp")
      bordered: true
      foreground: hero.ctx.fg
      onClicked: hero.ctx.openApp("wallpaper")
    }
    ToggleSwitch {
      id: power
      anchors.verticalCenter: parent.verticalCenter
      visible: !hero.ctx.offline
      checked: hero.on
      foreground: hero.ctx.fg
      onToggled: hero.ctx.send("wall", [checked ? "off" : "on"])
    }
  }
}
