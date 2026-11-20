import QtQuick
import qs.Commons
import qs.Ui

// Every module: Kinetic's own and the ones you added, with a switch, its version,
// where it came from and what it needs. Add a module from a GitHub link, a
// folder or an archive (or drop one anywhere on the app): Kinetic fetches it and
// what it needs, shows all of it, and installs nothing until you say so.
// From `kinetic modules --json` and `kinetic module ...`.
Column {
  id: page
  property var ctx: null
  spacing: Style.space(10)

  property string confirmRemove: ""   // a module waiting for a second click to be removed

  component PlainText: Text {
    textFormat: Text.PlainText
    color: page.ctx.fg
    font.family: page.ctx.fontFamily
    font.pixelSize: Style.font.body
    elide: Text.ElideNone
    wrapMode: Text.Wrap
  }
  component Panel: Rectangle {
    width: page.width
    height: inner.implicitHeight + Style.space(24)
    radius: Style.cornerRadius
    color: Qt.rgba(page.ctx.accent.r, page.ctx.accent.g, page.ctx.accent.b, 0.08)
    border.width: 1
    border.color: Qt.rgba(page.ctx.accent.r, page.ctx.accent.g, page.ctx.accent.b, 0.35)
    default property alias content: inner.data
    Column {
      id: inner
      x: Style.space(14)
      y: Style.space(12)
      width: parent.width - Style.space(28)
      spacing: Style.space(8)
    }
  }

  function moduleName(id) { var m = page.ctx.moduleInfo(id); return m ? m.name : id }
  function note(m) {
    var key = "modules." + m.id + (m.active === false ? ".off" : ".on"), t = page.ctx.tr(key)
    return t === key ? "" : t
  }

  PlainText { width: parent.width; text: page.ctx.tr("modules.about"); opacity: 0.7; font.pixelSize: Style.font.bodySmall }

  // ---------- add a module ----------
  Item {
    width: parent.width
    implicitHeight: source.implicitHeight
    TextField {
      id: source
      anchors.left: parent.left
      anchors.right: add.left
      anchors.rightMargin: Style.space(8)
      placeholderText: page.ctx.tr("modules.addPlaceholder")
      foreground: page.ctx.fg
      onAccepted: if (text.trim() !== "") page.ctx.inspectModule(text)
      onActiveFocusChanged: page.ctx.editingValue = activeFocus
    }
    Button {
      id: add
      anchors.right: parent.right
      anchors.verticalCenter: source.verticalCenter
      text: page.ctx.moduleBusy ? page.ctx.tr("modules.fetching") : page.ctx.tr("modules.add")
      bordered: true
      foreground: page.ctx.fg
      fontFamily: page.ctx.fontFamily
      onClicked: if (!page.ctx.moduleBusy && source.text.trim() !== "") page.ctx.inspectModule(source.text)
    }
  }
  PlainText { width: parent.width; text: page.ctx.tr("modules.dropHint"); opacity: 0.45; font.pixelSize: Style.font.caption }
  PlainText {
    width: parent.width
    visible: page.ctx.moduleError !== ""
    text: page.ctx.moduleError
    color: page.ctx.accent
    font.pixelSize: Style.font.bodySmall
  }
  PlainText {
    width: parent.width
    visible: page.ctx.moduleNote !== "" && page.ctx.moduleError === ""
    text: page.ctx.moduleNote
    opacity: 0.7
    font.pixelSize: Style.font.bodySmall
  }

  // ---------- what an install would do: nothing happens until Install ----------
  Panel {
    visible: !!page.ctx.modulePlan
    PlainText { width: parent.width; text: page.ctx.tr("modules.confirmTitle"); font.bold: true }
    Repeater {
      model: page.ctx.modulePlan ? page.ctx.modulePlan.modules : []
      delegate: Column {
        required property var modelData
        width: parent.width
        spacing: Style.space(1)
        PlainText {
          width: parent.width
          text: modelData.icon + "  " + modelData.name + "  " + modelData.id + " "
            + (modelData.replaces ? modelData.replaces + " → " : "") + modelData.version
            + (modelData.reason === "requested" ? "" : "   (" + modelData.reason + ")")
          font.bold: modelData.reason === "requested"
        }
        PlainText { width: parent.width; text: modelData.summary; opacity: 0.7; font.pixelSize: Style.font.caption; visible: text !== "" }
        PlainText {
          width: parent.width
          text: page.ctx.tr("modules.from") + " " + modelData.source + (modelData.commit ? " @ " + String(modelData.commit).slice(0, 10) : "")
            + (modelData.author ? " · " + modelData.author : "")
          opacity: 0.6
          font.pixelSize: Style.font.caption
        }
        PlainText {
          width: parent.width
          text: modelData.contains.files + " " + page.ctx.tr("modules.files")
            + (modelData.contains.pages.length ? " · " + page.ctx.tr("modules.pages") + " " + modelData.contains.pages.join(", ") : "")
            + (modelData.contains.hooks.length ? " · " + page.ctx.tr("modules.runsOn") + " " + modelData.contains.hooks.join(", ") : "")
          opacity: 0.6
          font.pixelSize: Style.font.caption
        }
      }
    }
    Repeater {
      model: page.ctx.modulePlan ? page.ctx.modulePlan.optional : []
      delegate: PlainText {
        required property var modelData
        width: parent.width
        text: page.ctx.tr("modules.optional") + " " + modelData.id + " (" + modelData["for"] + (modelData.why ? ": " + modelData.why : "") + ")"
        opacity: 0.6
        font.pixelSize: Style.font.caption
      }
    }
    PlainText { width: parent.width; text: page.ctx.modulePlan ? page.ctx.modulePlan.warning : ""; color: page.ctx.accent; font.pixelSize: Style.font.caption }
    Row {
      spacing: Style.space(8)
      Button { text: page.ctx.tr("modules.install"); bordered: true; foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily; onClicked: page.ctx.installPlan() }
      Button { text: page.ctx.tr("modules.cancel"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily; onClicked: page.ctx.cancelPlan() }
    }
  }

  // ---------- switching off what others need ----------
  Panel {
    visible: !!page.ctx.askDisable
    PlainText {
      width: parent.width
      text: page.ctx.askDisable
        ? page.ctx.tr("modules.stopsToo").replace("%1", page.ctx.askDisable.users.map(page.moduleName).join(", ")).replace("%2", page.moduleName(page.ctx.askDisable.id))
        : ""
    }
    Row {
      spacing: Style.space(8)
      Button {
        text: page.ctx.tr("modules.switchAllOff")
        bordered: true
        foreground: page.ctx.fg
        fontFamily: page.ctx.fontFamily
        onClicked: { var id = page.ctx.askDisable.id; page.ctx.askDisable = null; page.ctx.setModule(id, false, true) }
      }
      Button { text: page.ctx.tr("modules.cancel"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily; onClicked: page.ctx.askDisable = null }
    }
  }

  // ---------- the modules ----------
  Repeater {
    model: page.ctx ? page.ctx.modules : []
    delegate: Rectangle {
      id: row
      required property var modelData
      readonly property bool on: modelData.active !== false
      readonly property var needs: (modelData.requires || []).filter(function(r) { return !r.optional })
      readonly property bool blocked: on && needs.some(function(r) { return !r.installed || r.active === false })
      width: page.width
      height: body.implicitHeight + Style.space(20)
      radius: Style.cornerRadius
      color: Qt.rgba(page.ctx.fg.r, page.ctx.fg.g, page.ctx.fg.b, on ? 0.05 : 0.02)

      PlainText {
        id: glyph
        anchors.left: parent.left
        anchors.leftMargin: Style.space(14)
        anchors.top: parent.top
        anchors.topMargin: Style.space(12)
        width: Style.space(24)
        text: row.modelData.icon
        font.pixelSize: Style.font.title
        opacity: row.on ? 1 : 0.4
      }
      Column {
        id: body
        anchors.left: glyph.right
        anchors.leftMargin: Style.space(10)
        anchors.right: controls.left
        anchors.rightMargin: Style.space(14)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Style.space(2)
        PlainText {
          width: parent.width
          text: row.modelData.name + "   " + row.modelData.id + "  " + row.modelData.version
          font.bold: true
          opacity: row.on ? 1 : 0.55
        }
        PlainText { width: parent.width; text: row.modelData.summary; opacity: 0.6; font.pixelSize: Style.font.caption }
        PlainText {
          width: parent.width
          text: row.modelData.builtin ? page.ctx.tr("modules.builtin")
            : page.ctx.tr("modules.from") + " " + (row.modelData.source || "?") + (row.modelData.commit ? " @ " + String(row.modelData.commit).slice(0, 10) : "")
          opacity: 0.45
          font.pixelSize: Style.font.caption
        }
        PlainText {
          width: parent.width
          visible: row.needs.length > 0
          text: page.ctx.tr("modules.needs") + " " + row.needs.map(function(r) {
            return page.moduleName(r.id) + (!r.installed ? " (" + page.ctx.tr("modules.missing") + ")" : r.active === false ? " (" + page.ctx.tr("modules.isOff") + ")" : "")
          }).join(", ")
          color: row.blocked ? page.ctx.accent : page.ctx.fg
          opacity: row.blocked ? 0.9 : 0.45
          font.pixelSize: Style.font.caption
        }
        PlainText {
          width: parent.width
          visible: (row.modelData.requiredBy || []).length > 0
          text: page.ctx.tr("modules.usedBy") + " " + (row.modelData.requiredBy || []).map(page.moduleName).join(", ")
          opacity: 0.45
          font.pixelSize: Style.font.caption
        }
        PlainText { width: parent.width; visible: text !== ""; text: page.note(row.modelData); opacity: 0.45; font.pixelSize: Style.font.caption }
        PlainText { width: parent.width; visible: !!row.modelData.broken; text: String(row.modelData.broken || ""); color: page.ctx.accent; font.pixelSize: Style.font.caption }
      }
      Row {
        id: controls
        anchors.right: parent.right
        anchors.rightMargin: Style.space(14)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Style.space(8)
        Button {
          visible: !row.modelData.builtin
          anchors.verticalCenter: parent.verticalCenter
          text: page.confirmRemove === row.modelData.id ? page.ctx.tr("modules.removeSure") : ""
          iconText: page.confirmRemove === row.modelData.id ? "" : "󰆴"
          tooltipText: page.ctx.tr("modules.remove")
          bordered: true
          foreground: page.ctx.fg
          fontFamily: page.ctx.fontFamily
          onClicked: {
            if (page.confirmRemove === row.modelData.id) { page.confirmRemove = ""; page.ctx.removeModule(row.modelData.id) }
            else page.confirmRemove = row.modelData.id
          }
        }
        ToggleSwitch {
          anchors.verticalCenter: parent.verticalCenter
          visible: row.modelData.active !== null && !row.modelData.broken
          checked: row.on
          foreground: page.ctx.fg
          onToggled: if (checked) page.ctx.requestDisable(row.modelData.id); else page.ctx.setModule(row.modelData.id, true, false)
        }
      }
    }
  }
}
