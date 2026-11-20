import QtQuick
import qs.Commons
import qs.Ui
import "../Model.js" as Model

// Looks: a wallpaper and a bar, applied in one click; save the current one.
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
    text: page.ctx.tr("looks.about")
    wrapMode: Text.Wrap
    elide: Text.ElideNone
    opacity: 0.6
    font.pixelSize: Style.font.bodySmall
  }

  Repeater {
    model: page.ctx.looks
    delegate: Item {
      id: look
      required property var modelData
      readonly property bool current: modelData.id === page.ctx.currentLook
      width: page.width
      implicitHeight: text.implicitHeight + Style.space(12)

      Rectangle {
        anchors.fill: parent
        radius: Style.cornerRadius
        color: look.current ? Style.selectionFillFor(page.ctx.fg, page.ctx.accent)
             : (lookMouse.containsMouse ? Qt.rgba(page.ctx.fg.r, page.ctx.fg.g, page.ctx.fg.b, 0.06) : "transparent")
      }
      MouseArea {
        id: lookMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: page.ctx.send("look", ["apply", look.modelData.id])
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
          text: look.modelData.name + (look.modelData.mine ? "  ·  " + page.ctx.tr("looks.mine") : "")
          font.bold: look.current
        }
        PlainText {
          width: parent.width
          visible: look.modelData.description !== ""
          text: look.modelData.description
          wrapMode: Text.Wrap
          elide: Text.ElideNone
          opacity: 0.6
          font.pixelSize: Style.font.caption
        }
        PlainText {
          width: parent.width
          text: (look.modelData.wallpaper || page.ctx.tr("looks.noWallpaper")) + "  ·  " + String(look.modelData.bar || "")
          opacity: 0.45
          font.pixelSize: Style.font.caption
        }
      }
      PanelActionButton {
        id: remove
        anchors.right: parent.right
        anchors.rightMargin: Style.space(4)
        anchors.verticalCenter: parent.verticalCenter
        visible: !!look.modelData.mine
        iconText: "󰆴"
        fontSize: Style.font.body
        foreground: page.ctx.fg
        fontFamily: page.ctx.fontFamily
        tooltipText: page.ctx.tr("looks.remove")
        onClicked: page.ctx.send("look", ["remove", look.modelData.id])
      }
    }
  }

  PanelSectionHeader { text: page.ctx.tr("looks.save"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
  Row {
    width: parent.width
    spacing: Style.space(6)
    TextField {
      id: name
      width: parent.width - saveButton.width - parent.spacing
      placeholderText: page.ctx.tr("looks.namePlaceholder")
      maximumLength: 40
      foreground: page.ctx.fg
      onActiveFocusChanged: page.ctx.editingValue = activeFocus
      onAccepted: saveButton.save()
      Keys.onEscapePressed: function(event) { page.ctx.refocus(); event.accepted = true }
    }
    Button {
      id: saveButton
      text: page.ctx.tr("looks.saveButton")
      bordered: true
      enabled: Model.lookId(name.text) !== ""
      foreground: page.ctx.fg
      fontFamily: page.ctx.fontFamily
      function save() {
        var id = Model.lookId(name.text)
        if (id === "") return
        page.ctx.send("look", ["save", id])
        name.text = ""
        page.ctx.refocus()
      }
      onClicked: save()
    }
  }
}
