import QtQuick
import QtQuick.Controls
import qs.Commons
import qs.Ui
import "../Model.js" as Model

// One row for any setting, whatever module it belongs to, picked by its type:
// a wallpaper parameter, an engine option, or a bar option. Every module
// describes its settings in the same shape, so this one row serves them all.
Column {
  id: row

  property var ctx: null                  // the panel: setValue, setLater, resetValue, fg, palette, tr
  property var item: ({})
  property string kind: "param"           // "param" | "config" | "bar"
  readonly property string key: (kind === "param" ? item.name : item.key) || ""
  readonly property string type: item.type || "float"
  readonly property bool numeric: type === "float" || type === "int"
  readonly property bool special: Model.isSpecial(item)
  readonly property bool changed: Model.isChanged(item)
  readonly property color fg: ctx ? ctx.fg : "white"
  readonly property string fontFamily: ctx ? ctx.fontFamily : Style.font.family
  // what the slider shows between letting go and the daemon confirming
  property bool held: false
  property real heldValue: 0
  onItemChanged: held = false
  // click a slider's number to type one: Enter sets it, Esc cancels, clicking away sets it
  property bool editing: false

  function startEdit() {
    editField.text = valueText.text
    editing = true
    editField.forceActiveFocus()
    editField.selectAll()
  }

  function finishEdit(apply, refocus) {
    if (!editing) return
    editing = false
    var v = apply ? Model.parseNumberInput(item, editField.text) : null
    if (v !== null) {
      heldValue = v
      held = true
      ctx.setValue(kind, key, Model.formatNumber(item, v))
    }
    if (refocus) ctx.refocus()
  }

  width: parent ? parent.width : 0
  spacing: Style.space(4)

  component PlainText: Text {
    textFormat: Text.PlainText
    color: row.fg
    font.family: row.fontFamily
    font.pixelSize: Style.font.body
    elide: Text.ElideRight
  }

  Item {
    width: parent.width
    implicitHeight: Math.max(label.implicitHeight, toggle.visible ? toggle.implicitHeight : 0)

    PlainText {
      id: label
      anchors.left: parent.left
      anchors.right: row.editing ? editField.left : valueText.left
      anchors.rightMargin: Style.space(8)
      anchors.verticalCenter: parent.verticalCenter
      text: row.item.label || row.key
    }
    PlainText {
      id: valueText
      anchors.right: resetButton.visible ? resetButton.left : (toggle.visible ? toggle.left : parent.right)
      anchors.rightMargin: Style.space(6)
      anchors.verticalCenter: parent.verticalCenter
      visible: row.type !== "bool" && row.type !== "color" && row.type !== "text" && !row.editing
      text: row.special ? String(row.item.value) : (row.numeric ? Model.formatNumber(row.item, slider.liveValue) : Model.formatValue(row.item))
      opacity: valueMouse.containsMouse ? 1 : 0.65
      font.pixelSize: Style.font.bodySmall
      font.underline: valueMouse.containsMouse
      MouseArea {
        id: valueMouse
        anchors.fill: parent
        anchors.margins: -Style.space(4)
        enabled: row.numeric
        hoverEnabled: true
        cursorShape: Qt.IBeamCursor
        onClicked: row.startEdit()
      }
    }
    TextField {
      id: editField
      anchors.right: valueText.right
      anchors.verticalCenter: parent.verticalCenter
      width: Style.space(76)
      visible: row.editing
      foreground: row.fg
      horizontalAlignment: TextInput.AlignRight
      verticalPadding: Style.space(1)
      horizontalPadding: Style.space(5)
      font.pixelSize: Style.font.bodySmall
      inputMethodHints: Qt.ImhFormattedNumbersOnly
      validator: RegularExpressionValidator { regularExpression: /[-+]?[0-9]*[.,]?[0-9]*/ }
      onActiveFocusChanged: {
        if (row.ctx) row.ctx.editingValue = activeFocus
        if (!activeFocus) row.finishEdit(true, false)
      }
      onAccepted: row.finishEdit(true, true)
      Keys.onEscapePressed: function(event) { row.finishEdit(false, true); event.accepted = true }
    }
    PanelActionButton {
      id: resetButton
      anchors.right: toggle.visible ? toggle.left : parent.right
      anchors.verticalCenter: parent.verticalCenter
      visible: row.changed
      iconText: "󰑓"
      fontSize: Style.font.body
      foreground: row.fg
      fontFamily: row.fontFamily
      tooltipText: row.ctx ? row.ctx.tr("reset") : ""
      onClicked: row.ctx.resetValue(row.kind, row.item)
    }
    ToggleSwitch {
      id: toggle
      anchors.right: parent.right
      anchors.verticalCenter: parent.verticalCenter
      visible: row.type === "bool"
      checked: Model.isOn(row.item)
      foreground: row.fg
      onToggled: row.ctx.setValue(row.kind, row.key, !Model.isOn(row.item))
    }
  }

  PlainText {
    width: parent.width
    visible: !!row.item.description
    text: row.item.description || ""
    wrapMode: Text.Wrap
    elide: Text.ElideNone
    opacity: 0.5
    font.pixelSize: Style.font.caption
  }

  Row {
    width: parent.width
    spacing: Style.space(6)
    visible: row.numeric

    // a special word instead of a number: radius "full"
    Button {
      id: specialChip
      visible: row.item.special !== undefined
      text: String(row.item.special || "")
      selected: row.special
      bordered: true
      foreground: row.fg
      fontFamily: row.fontFamily
      fontSize: Style.font.caption
      horizontalPadding: Style.space(6)
      anchors.verticalCenter: parent.verticalCenter
      onClicked: row.ctx.setValue(row.kind, row.key, row.item.special)
    }

    PanelSlider {
      id: slider
      width: parent.width - (specialChip.visible ? specialChip.width + parent.spacing : 0)
      height: Style.spacing.controlHeight * 0.7
      bar: row.ctx ? row.ctx.bar : null
      opacity: row.special ? 0.4 : 1
      minimum: Number(row.item.min || 0)
      maximum: Number(row.item.max === undefined ? 1 : row.item.max)
      step: Model.stepFor(row.item)
      integer: row.type === "int"
      value: row.held ? row.heldValue : (row.special ? maximum : Number(row.item.value || 0))
      onMoved: function(v) { row.heldValue = v; row.held = true; row.ctx.setLater(row.kind, row.key, Model.formatNumber(row.item, v)) }
      onReleased: function(v) { row.heldValue = v; row.held = true; row.ctx.setLater(row.kind, row.key, Model.formatNumber(row.item, v)) }
      onRightClicked: row.ctx.resetValue(row.kind, row.item)

      // Omarchy's slider turns with the mouse wheel and keeps it, so scrolling
      // the panel past a slider changed the setting. Over a slider the wheel
      // scrolls the panel; Ctrl + wheel still steps the value. Clicks and drags
      // pass through to the slider (no buttons accepted here).
      MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        onWheel: function(wheel) {
          wheel.accepted = true
          if (!(wheel.modifiers & Qt.ControlModifier)) {
            if (row.ctx) row.ctx.scrollWheel(wheel)
            return
          }
          if (wheel.angleDelta.y === 0 || !row.ctx) return
          var v = Number(slider.value) + (wheel.angleDelta.y > 0 ? slider.step : -slider.step)
          v = Math.max(slider.minimum, Math.min(slider.maximum, slider.integer ? Math.round(v) : v))
          row.heldValue = v
          row.held = true
          row.ctx.setLater(row.kind, row.key, Model.formatNumber(row.item, v))
        }
      }
    }
  }

  // text: a few words (classes, tags); Enter sets them, an empty field sets "none"
  TextField {
    id: textField
    width: parent.width
    visible: row.type === "text"
    text: row.type === "text" && String(row.item.value) !== "none" ? String(row.item.value || "") : ""
    placeholderText: row.type === "text" ? String(row.item["default"] || "") : ""
    maximumLength: 64
    foreground: row.fg
    font.pixelSize: Style.font.bodySmall
    onActiveFocusChanged: if (row.ctx) row.ctx.editingValue = activeFocus
    onAccepted: {
      var v = text.trim().replace(/\s+/g, " ")
      if (!v.length) v = "none"
      if (Model.safeArg(v)) row.ctx.setValue(row.kind, row.key, v)
      row.ctx.refocus()
    }
  }

  ButtonGroup {
    visible: row.type === "choice" && (row.item.options || []).length <= 5
    options: Model.choiceOptions(row.item)
    value: Model.choiceValue(row.item)
    foreground: row.fg
    fontFamily: row.fontFamily
    fontSize: Style.font.bodySmall
    onChanged: function(v) { row.ctx.setValue(row.kind, row.key, v) }
  }

  Dropdown {
    width: parent.width
    visible: row.type === "choice" && (row.item.options || []).length > 5
    showLabel: false
    options: Model.choiceOptions(row.item)
    value: Model.choiceValue(row.item)
    foreground: row.fg
    fontFamily: row.fontFamily
    onChanged: function(v) { row.ctx.setValue(row.kind, row.key, v) }
  }

  // bitmask: one chip per neighbor count, the shape of a Life rule
  Row {
    visible: row.type === "bits"
    spacing: Style.space(3)
    Repeater {
      model: row.type === "bits" ? (row.item.bits || 9) : 0
      delegate: Button {
        required property int index
        text: String(index)
        selected: Model.hasBit(row.item.value, index)
        bordered: true
        foreground: row.fg
        fontFamily: row.fontFamily
        fontSize: Style.font.bodySmall
        horizontalPadding: Style.space(7)
        onClicked: row.ctx.setValue(row.kind, row.key, String(Model.toggleBit(row.item.value, index)))
      }
    }
  }

  // color: named choices (theme, accent), the theme's palette as swatches, free hex entry
  Column {
    visible: row.type === "color"
    width: parent.width
    spacing: Style.space(4)
    Flow {
      width: parent.width
      spacing: Style.space(3)
      Repeater {
        model: row.type === "color" ? (row.item.options || []) : []
        delegate: Button {
          required property var modelData
          text: Model.titleCase(String(modelData))
          selected: String(row.item.value) === String(modelData)
          bordered: true
          foreground: row.fg
          fontFamily: row.fontFamily
          fontSize: Style.font.caption
          horizontalPadding: Style.space(6)
          onClicked: row.ctx.setValue(row.kind, row.key, modelData)
        }
      }
      Repeater {
        model: row.type === "color" && row.ctx ? row.ctx.palette : []
        delegate: Rectangle {
          required property var modelData
          width: Style.space(18)
          height: Style.space(18)
          radius: Style.cornerRadius > 0 ? width / 2 : 0
          color: modelData
          border.width: String(row.item.value).toLowerCase() === String(modelData).toLowerCase() ? 2 : 1
          border.color: border.width > 1 ? row.fg : Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.25)
          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: row.ctx.setValue(row.kind, row.key, parent.modelData)
          }
        }
      }
    }
    Row {
      spacing: Style.space(6)
      Rectangle {
        width: Style.space(24)
        height: hexField.height
        color: Model.normalizeHex(row.item.value) || "transparent"
        border.width: 1
        border.color: Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.3)
      }
      TextField {
        id: hexField
        width: Style.space(110)
        text: String(row.item.value || "").charAt(0) === "#" ? String(row.item.value) : ""
        placeholderText: row.ctx ? row.ctx.tr("hexPlaceholder") : ""
        maximumLength: 7
        foreground: row.fg
        onAccepted: {
          var hex = Model.normalizeHex(text)
          if (hex) row.ctx.setValue(row.kind, row.key, hex)
        }
      }
    }
  }
}
