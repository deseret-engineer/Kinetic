import QtQuick
import QtQuick.Controls
import Quickshell
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "components"
import "pages"

// The Kinetic bar widget: the wallpaper, at hand. What is showing, its presets
// and parameters, and the library. Everything else (the bar, glass, motion,
// presets, the engine) is in the Kinetic app, which the gear button opens.
Panel {
  id: root
  moduleName: "kinetic"
  ipcTarget: "kinetic"

  KineticCtx {
    id: kinetic
    bar: root.bar
    active: root.opened
    live: root.opened
  }

  readonly property var tabs: ["wallpaper", "library"]
  readonly property var tabOptions: tabs.map(function(t) { return { value: t, label: kinetic.tr("tab." + t) } })

  Connections {
    target: kinetic
    function onTabChanged() {
      Qt.callLater(function() {
        if (panel.loadedContent && panel.loadedContent.scrollToTop) panel.loadedContent.scrollToTop()
        if (kinetic.tab === "library" && kinetic.searchField) kinetic.searchField.forceActiveFocus()
      })
    }
  }

  ShellIpc {
    target: "kinetic.data"
    function refresh(): void { kinetic.refresh(); kinetic.refreshPresets() }
    // the widget's own tabs here; anything else is in the app
    function showTab(name: string): void {
      if (root.tabs.indexOf(name) >= 0) kinetic.tab = name
      else { root.close(); kinetic.openApp(name) }
      kinetic.refresh()
    }
  }

  component PlainText: Text {
    textFormat: Text.PlainText
    color: kinetic.fg
    font.family: kinetic.fontFamily
    font.pixelSize: Style.font.body
    elide: Text.ElideRight
  }

  // ---------- bar icon ----------
  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: "󰸉"
    tooltipText: ""
    onPressed: function(b) { root.toggle() }
  }
  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: panel.loadedContent
    contentWidth: panel.fittedContentWidth(Style.space(420))
    contentHeight: panel.fittedContentHeight(panel.loadedContentHeight, Style.space(700))

    content: PanelKeyCatcher {
      id: catcher
      anchors.fill: parent
      implicitHeight: top.implicitHeight + Style.space(12) + body.implicitHeight
      blocked: (!!kinetic.searchField && kinetic.searchField.activeFocus) || kinetic.editingValue
      function scrollToTop() { scroll.ScrollBar.vertical.position = 0 }
      Component.onCompleted: kinetic.focusItem = catcher
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }
      onActivateRequested: if (kinetic.tab === "wallpaper") kinetic.send("wall", ["toggle-pause"])
      onTextKey: function(t) {
        var k = String(t).toLowerCase()
        var n = "12".indexOf(k)
        if (n >= 0) kinetic.tab = root.tabs[n]
        else if (k === "n") kinetic.send("wall", ["next"])
        else if (k === "p") kinetic.send("wall", ["prev"])
        else if (k === "r") kinetic.send("wall", ["random"])
        else if (k === "o") { root.close(); kinetic.openApp("wallpaper") }
        else if (k === "/") { kinetic.tab = "library"; if (kinetic.searchField) kinetic.searchField.forceActiveFocus() }
      }

      Column {
        id: top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: Style.space(10)

        WallHero { width: parent.width; ctx: kinetic; openApp: true }

        ButtonGroup {
          options: root.tabOptions
          value: kinetic.tab
          foreground: kinetic.fg
          fontFamily: kinetic.fontFamily
          onChanged: function(v) { kinetic.tab = v }
        }

        // ===== Library search: stays put while the grid scrolls =====
        TextField {
          id: search
          width: parent.width
          visible: kinetic.tab === "library" && !kinetic.offline
          placeholderText: kinetic.tr("search")
          foreground: kinetic.fg
          text: kinetic.query
          onTextChanged: if (text !== kinetic.query) { kinetic.query = text; scroll.ScrollBar.vertical.position = 0 }
          Component.onCompleted: kinetic.searchField = search
          // Enter picks the first match; Escape clears, then hands the keys back to the panel
          onAccepted: if (kinetic.shown.length > 0) { kinetic.send("wall", ["set", kinetic.shown[0].id]); kinetic.query = ""; kinetic.tab = "wallpaper" }
          Keys.onEscapePressed: function(event) {
            if (kinetic.query !== "") kinetic.query = ""
            else catcher.forceActiveFocus()
            event.accepted = true
          }
          Keys.onTabPressed: function(event) { catcher.forceActiveFocus(); event.accepted = true }
        }
      }

      ScrollView {
        id: scroll
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: top.bottom
        anchors.topMargin: Style.space(12)
        anchors.bottom: parent.bottom
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: body.implicitHeight > height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        Binding { target: scroll.contentItem; property: "interactive"; value: body.implicitHeight > scroll.height }
        Component.onCompleted: kinetic.scrollArea = scroll
        Component.onDestruction: if (kinetic.scrollArea === scroll) kinetic.scrollArea = null

        Column {
          id: body
          width: scroll.availableWidth - Style.space(8)
          spacing: Style.space(12)

          // Pages are loaded when first shown, then kept.
          Loader { width: parent.width; active: kinetic.tab === "wallpaper" || item !== null; visible: kinetic.tab === "wallpaper" && !kinetic.offline
                   sourceComponent: Component { WallPage { ctx: kinetic } } }
          Loader { width: parent.width; active: kinetic.tab === "library" || item !== null; visible: kinetic.tab === "library" && !kinetic.offline
                   sourceComponent: Component { LibraryPage { ctx: kinetic } } }

          PlainText {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: kinetic.tr("hint")
            opacity: 0.4
            font.pixelSize: Style.font.caption
          }
        }
      }
    }
  }
}
