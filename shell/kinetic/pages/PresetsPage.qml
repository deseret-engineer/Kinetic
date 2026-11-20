import QtQuick
import qs.Commons
import qs.Ui
import "../Model.js" as Model

// Presets: a wallpaper, a bar and motion, applied in one click; save the current setup as one.
Column {
  id: page
  property var ctx: null
  spacing: Style.space(10)

  component PlainText: Text {
    textFormat: Text.PlainText
    color: page.ctx.fg
    font.family: page.ctx.fontFamily
    font.pixelSize: Style.font.body
    elide: Text.ElideRight
  }

  PlainText {
    width: parent.width
    text: page.ctx.tr("presets.about")
    wrapMode: Text.Wrap
    elide: Text.ElideNone
    opacity: 0.6
    font.pixelSize: Style.font.bodySmall
  }

  Repeater {
    model: page.ctx.presets
    delegate: Item {
      id: preset
      required property var modelData
      readonly property bool current: modelData.id === page.ctx.currentPreset
      width: page.width
      implicitHeight: text.implicitHeight + Style.space(12)

      Rectangle {
        anchors.fill: parent
        radius: Style.cornerRadius
        color: preset.current ? Style.selectionFillFor(page.ctx.fg, page.ctx.accent)
             : (presetMouse.containsMouse ? Qt.rgba(page.ctx.fg.r, page.ctx.fg.g, page.ctx.fg.b, 0.06) : "transparent")
      }
      MouseArea {
        id: presetMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: page.ctx.send("preset", ["apply", preset.modelData.id])
      }
      Column {
        id: text
        anchors.left: parent.left
        anchors.leftMargin: Style.space(8)
        anchors.right: remove.visible ? remove.left : parent.right
        anchors.rightMargin: Style.space(8)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Style.space(2)
        PlainText {
          width: parent.width
          text: preset.modelData.name + (preset.modelData.mine ? "  ·  " + page.ctx.tr("presets.mine") : "")
          font.bold: preset.current
        }
        PlainText {
          width: parent.width
          visible: preset.modelData.description !== ""
          text: preset.modelData.description
          wrapMode: Text.Wrap
          elide: Text.ElideNone
          opacity: 0.6
          font.pixelSize: Style.font.caption
        }
        PlainText {
          width: parent.width
          text: (preset.modelData.wallpaper || page.ctx.tr("presets.noWallpaper")) + "  ·  " + String(preset.modelData.bar || "")
          opacity: 0.45
          font.pixelSize: Style.font.caption
        }
      }
      PanelActionButton {
        id: remove
        anchors.right: parent.right
        anchors.rightMargin: Style.space(4)
        anchors.verticalCenter: parent.verticalCenter
        visible: !!preset.modelData.mine
        iconText: "󰆴"
        fontSize: Style.font.body
        foreground: page.ctx.fg
        fontFamily: page.ctx.fontFamily
        tooltipText: page.ctx.tr("presets.remove")
        onClicked: page.ctx.send("preset", ["remove", preset.modelData.id])
      }
    }
  }

  PanelSectionHeader { text: page.ctx.tr("presets.save"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
  Row {
    width: parent.width
    spacing: Style.space(6)
    TextField {
      id: name
      width: parent.width - saveButton.width - parent.spacing
      placeholderText: page.ctx.tr("presets.namePlaceholder")
      maximumLength: 40
      foreground: page.ctx.fg
      onActiveFocusChanged: page.ctx.editingValue = activeFocus
      onAccepted: saveButton.save()
      Keys.onEscapePressed: function(event) { page.ctx.refocus(); event.accepted = true }
    }
    Button {
      id: saveButton
      text: page.ctx.tr("presets.saveButton")
      bordered: true
      enabled: Model.presetId(name.text) !== ""
      foreground: page.ctx.fg
      fontFamily: page.ctx.fontFamily
      function save() {
        var id = Model.presetId(name.text)
        if (id === "") return
        page.ctx.send("preset", ["save", id])
        name.text = ""
        page.ctx.refocus()
      }
      onClicked: save()
    }
  }
}
