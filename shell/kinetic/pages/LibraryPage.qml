import QtQuick
import qs.Commons
import qs.Ui

// Every installed wallpaper with its rendered preview. Click to switch.
Column {
  id: page
  property var ctx: null
  spacing: Style.space(8)

  component PlainText: Text {
    textFormat: Text.PlainText
    color: page.ctx.fg
    font.family: page.ctx.fontFamily
    font.pixelSize: Style.font.body
    elide: Text.ElideRight
  }

  PlainText {
    width: parent.width
    visible: page.ctx.query !== "" && page.ctx.shown.length === 0
    text: page.ctx.tr("noMatches")
    opacity: 0.5
  }

  Flow {
    width: parent.width
    spacing: Style.space(8)
    Repeater {
      model: page.ctx.shown
      delegate: Item {
        id: tile
        required property var modelData
        readonly property bool current: !!page.ctx.info && modelData.id === page.ctx.info.current
        width: (page.width - Style.space(8)) / 2
        height: thumb.height + name.implicitHeight + Style.space(10)

        Rectangle {
          anchors.fill: parent
          radius: Style.cornerRadius
          color: tile.current ? Style.selectionFillFor(page.ctx.fg, page.ctx.accent)
               : (tileMouse.containsMouse ? Qt.rgba(page.ctx.fg.r, page.ctx.fg.g, page.ctx.fg.b, 0.06) : "transparent")
        }
        Image {
          id: thumb
          x: Style.space(4)
          y: Style.space(4)
          width: parent.width - Style.space(8)
          height: Math.round(width * 0.625)
          source: page.ctx.info ? "file://" + page.ctx.info.thumbs + "/" + tile.modelData.id + ".png" : ""
          sourceSize.width: 360
          asynchronous: true
          fillMode: Image.PreserveAspectCrop
          cache: false
        }
        PlainText {
          id: name
          anchors.top: thumb.bottom
          anchors.topMargin: Style.space(3)
          x: Style.space(6)
          width: parent.width - Style.space(12)
          text: tile.modelData.name + (tile.modelData.audio ? "  ♪" : "")
          font.pixelSize: Style.font.bodySmall
          font.bold: tile.current
        }
        MouseArea {
          id: tileMouse
          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
          // clearing the search rebuilds the tiles (this one too), so it goes last, after what it needs
          onClicked: {
            var ctx = page.ctx, id = tile.modelData.id
            ctx.send("wall", ["set", id])
            ctx.tab = "wallpaper"
            ctx.query = ""
          }
        }
        PanelToolTip {
          visible: tileMouse.containsMouse && tile.modelData.description !== ""
          text: tile.modelData.description
          fontFamily: page.ctx.fontFamily
        }
      }
    }
  }
}
