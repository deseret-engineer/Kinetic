import QtQuick
import QtQuick.Controls
import Quickshell
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "components"
import "pages"

// The Kinetic app: a window of the Omarchy shell listing every module's pages.
// The list comes from the modules' manifests (module.json `pages`), so a module
// you add shows up with its own pages, loaded from its folder; a module switched
// off leaves the list. A module with several pages is a group: its first page is
// the row, the others nest under it (Wallpaper: Library, Engine), and the group
// folds. Modules, at the bottom, adds, removes and switches them.
// `kinetic app [page]` (or the launcher's Kinetic entry) opens it.
Item {
  id: root
  property var shell: null
  visible: false

  // every page whose module is on, then the modules. Replaced only when it really
  // changes: the module states refresh after every command, and a new list would
  // rebuild every page. A page's `parent` is its group's first page ("" for that
  // one and for single-page modules); `children` counts the pages nested under it.
  property var entries: []
  property string entriesKey: ""
  onComputedEntriesChanged: {
    var k = JSON.stringify(computedEntries)
    if (k !== entriesKey) { entriesKey = k; entries = computedEntries }
  }
  readonly property var computedEntries: {
    var out = [], head = null
    for (var i = 0; i < kinetic.pages.length; i++) {
      var p = kinetic.pages[i]
      if (!kinetic.moduleActive(p.module)) continue
      var e = Object.assign({}, p, { parent: "", parentLabel: "", children: 0, last: false })
      if (head && head.module === p.module) { e.parent = head.id; e.parentLabel = head.label; head.children++ }
      else head = e
      out.push(e)
    }
    for (var j = 0; j < out.length; j++)
      out[j].last = out[j].parent !== "" && (j + 1 === out.length || out[j + 1].parent !== out[j].parent)
    out.push({ id: "modules", module: "", label: kinetic.tr("app.modules"),
               icon: "󰕰", builtin: true, url: Qt.resolvedUrl("pages/ModulesPage.qml"), props: {},
               parent: "", parentLabel: "", children: 0, last: false })
    // a page without a row in the list: the gear beside Reload opens it
    out.push({ id: "settings", module: "", label: kinetic.tr("app.settings"),
               icon: "󰒓", builtin: true, url: Qt.resolvedUrl("pages/SettingsPage.qml"), props: {},
               parent: "", parentLabel: "", children: 0, last: false, hidden: true })
    return out
  }
  readonly property var ids: entries.map(function(p) { return p.id })
  function pageFor(id) {
    for (var i = 0; i < entries.length; i++) if (entries[i].id === id) return entries[i]
    return null
  }
  readonly property var current: pageFor(kinetic.tab) || entries[entries.length - 1]
  // a page whose module was just switched off (or removed): go to the modules
  onIdsChanged: if (kinetic.modules.length > 0 && ids.indexOf(kinetic.tab) < 0) kinetic.tab = "modules"

  // Folded groups, by the id of their first page. Showing a page opens its group;
  // folding the group of the page showing moves to the group's first page.
  property var folded: ({})
  function fold(id, on) {
    var f = Object.assign({}, folded)
    if (on) f[id] = true
    else delete f[id]
    folded = f
    if (on && root.current.parent === id) kinetic.tab = id
  }
  // the rows the sidebar shows, in order: what Ctrl + 1-9 and Ctrl + Tab walk
  readonly property var rowIds: entries.filter(function(p) { return !p.hidden && (p.parent === "" || !folded[p.parent]) })
                                       .map(function(p) { return p.id })
  function step(by) {
    var r = rowIds, i = r.indexOf(kinetic.tab)
    kinetic.tab = r[(i + by + r.length) % r.length]
  }

  KineticCtx {
    id: kinetic
    active: window.visible
    live: window.visible && ["wallpaper", "library", "glass", "light", "engine"].indexOf(kinetic.tab) >= 0
  }

  // Closing the window from Hyprland (Super + W, a close button) destroys it while
  // Quickshell still counts it visible, so setting visible again would do nothing.
  // Opening always maps it afresh, which also brings it to the current workspace.
  function open(page) {
    if (/^[a-z][a-z0-9-]*$/.test(String(page))) kinetic.tab = String(page)
    if (kinetic.modules.length > 0 && ids.indexOf(kinetic.tab) < 0) kinetic.tab = "modules" // off, or no such page
    window.visible = false
    window.visible = true
    kinetic.refresh()
    kinetic.refreshModules()
    kinetic.maybeCheckUpdates() // once a day at most, if the setting is on
  }

  ShellIpc {
    target: "kinetic.app"
    function open(page: string): void { root.open(page) }
    function close(): void { window.visible = false }
    function toggle(): void { if (window.visible) window.visible = false; else root.open(kinetic.tab) }
  }

  Connections {
    target: kinetic
    function onTabChanged() {
      var p = root.pageFor(kinetic.tab)
      if (p && root.folded[p.parent]) root.fold(p.parent, false)
      Qt.callLater(function() {
        scroll.ScrollBar.vertical.position = 0
        if (root.current.search && kinetic.searchField) kinetic.searchField.forceActiveFocus()
        if (kinetic.tab === "presets") kinetic.refreshPresets()
      })
    }
  }

  component PlainText: Text {
    textFormat: Text.PlainText
    color: kinetic.fg
    font.family: kinetic.fontFamily
    font.pixelSize: Style.font.body
    elide: Text.ElideRight
  }

  FloatingWindow {
    id: window
    title: "Kinetic"
    visible: false
    color: Commons.Color.background
    implicitWidth: 1060
    implicitHeight: 740
    minimumSize: Qt.size(760, 520)

    // Keys: Escape closes, Ctrl + 1-9 picks a row, Ctrl + Tab steps through the rows.
    Item {
      id: keys
      anchors.fill: parent
      focus: true
      Component.onCompleted: kinetic.focusItem = keys
      Keys.onPressed: function(event) {
        var rows = root.rowIds
        if (event.key === Qt.Key_Escape && !kinetic.editingValue) { window.visible = false; event.accepted = true }
        else if ((event.modifiers & Qt.ControlModifier) && event.key >= Qt.Key_1 && event.key <= Qt.Key_9 && event.key - Qt.Key_1 < rows.length) { kinetic.tab = rows[event.key - Qt.Key_1]; event.accepted = true }
        else if (event.key === Qt.Key_F5 || ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_R)) { kinetic.reloadAll(); event.accepted = true }
        else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Tab) { root.step(1); event.accepted = true }
        else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Backtab) { root.step(-1); event.accepted = true }
      }

      // ---------- the pages ----------
      Rectangle {
        id: side
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Style.space(260)
        color: Qt.rgba(kinetic.fg.r, kinetic.fg.g, kinetic.fg.b, 0.04)

        Column {
          id: brand
          anchors.left: parent.left
          anchors.right: parent.right
          anchors.top: parent.top
          anchors.margins: Style.space(18)
          spacing: Style.space(2)
          PlainText { text: kinetic.tr("app.title"); font.pixelSize: Style.font.title; font.bold: true }
          PlainText { width: parent.width; text: kinetic.tr("app.subtitle"); opacity: 0.55; font.pixelSize: Style.font.caption }
        }

        Flickable {
          anchors.left: parent.left
          anchors.right: parent.right
          anchors.top: brand.bottom
          anchors.bottom: reloadButton.top
          anchors.topMargin: Style.space(16)
          anchors.bottomMargin: Style.space(10)
          anchors.leftMargin: Style.space(10)
          anchors.rightMargin: Style.space(10)
          contentHeight: list.implicitHeight
          clip: true
          interactive: contentHeight > height

          Column {
            id: list
            width: parent.width
            spacing: Style.space(2)

            // A row per page. A group's first page carries the fold arrow; the
            // pages nested under it sit indented along a guide from its icon.
            Repeater {
              model: root.entries
              delegate: Rectangle {
                id: entry
                required property var modelData
                readonly property bool nested: modelData.parent !== ""
                readonly property bool group: modelData.children > 0
                readonly property bool open: group && !root.folded[modelData.id]
                readonly property bool current: kinetic.tab === modelData.id
                readonly property real indent: nested ? Style.space(30) : 0
                visible: !modelData.hidden && (!nested || !root.folded[modelData.parent])
                x: indent
                width: parent.width - indent
                height: nested ? Style.space(36) : Math.max(Style.space(52), label.implicitHeight + Style.space(16))
                radius: Style.cornerRadius
                color: current ? Qt.rgba(kinetic.accent.r, kinetic.accent.g, kinetic.accent.b, 0.18)
                  : hover.containsMouse ? Qt.rgba(kinetic.fg.r, kinetic.fg.g, kinetic.fg.b, 0.06) : "transparent"

                // the guide: down from the group's icon, past each nested page
                Rectangle {
                  visible: entry.nested
                  x: Style.space(25) - entry.indent
                  y: -list.spacing
                  width: 1
                  height: entry.height + list.spacing
                  color: Qt.rgba(kinetic.fg.r, kinetic.fg.g, kinetic.fg.b, 0.14)
                }
                PlainText {
                  id: glyph
                  anchors.left: parent.left
                  anchors.leftMargin: Style.space(entry.nested ? 8 : 12)
                  anchors.verticalCenter: parent.verticalCenter
                  width: Style.space(22)
                  horizontalAlignment: Text.AlignHCenter
                  elide: Text.ElideNone
                  text: entry.modelData.icon
                  font.pixelSize: entry.nested ? Style.font.body : Style.font.title
                  color: entry.current ? kinetic.accent : kinetic.fg
                  opacity: entry.current || !entry.nested ? 1 : 0.7
                }
                Column {
                  id: label
                  anchors.left: glyph.right
                  anchors.leftMargin: Style.space(entry.nested ? 6 : 10)
                  anchors.right: entry.group ? arrow.left : parent.right
                  anchors.rightMargin: Style.space(entry.group ? 4 : 10)
                  anchors.verticalCenter: parent.verticalCenter
                  spacing: Style.space(1)
                  PlainText { width: parent.width; text: entry.modelData.label; font.bold: entry.current }
                }
                MouseArea {
                  id: hover
                  anchors.fill: parent
                  hoverEnabled: true
                  cursorShape: Qt.PointingHandCursor
                  onClicked: { kinetic.tab = entry.modelData.id; keys.forceActiveFocus() }
                }
                // folds and unfolds the group
                Rectangle {
                  id: arrow
                  visible: entry.group
                  anchors.right: parent.right
                  anchors.rightMargin: Style.space(8)
                  anchors.verticalCenter: parent.verticalCenter
                  width: Style.space(24)
                  height: width
                  radius: width / 2
                  color: arrowHover.containsMouse ? Qt.rgba(kinetic.fg.r, kinetic.fg.g, kinetic.fg.b, 0.1) : "transparent"
                  PlainText {
                    anchors.centerIn: parent
                    text: entry.open ? "󰅀" : "󰅂"
                    elide: Text.ElideNone
                    opacity: arrowHover.containsMouse ? 0.9 : 0.45
                  }
                  MouseArea {
                    id: arrowHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { root.fold(entry.modelData.id, entry.open); keys.forceActiveFocus() }
                  }
                }
              }
            }
          }
        }

        // When something looks wrong: restart every part (engine, glass plugin, bar, this app).
        Button {
          id: reloadButton
          anchors.left: parent.left
          anchors.bottom: keyHint.top
          anchors.leftMargin: Style.space(10)
          anchors.bottomMargin: Style.space(6)
          text: kinetic.tr("app.reload")
          iconText: "󰑐"
          tooltipText: kinetic.tr("app.reload.about")
          foreground: kinetic.fg
          fontFamily: kinetic.fontFamily
          onClicked: kinetic.reloadAll()
        }
        // Settings: the app's own (updates, about, attributions); a dot when an update is waiting
        Button {
          id: settingsButton
          anchors.left: reloadButton.right
          anchors.leftMargin: Style.space(4)
          anchors.verticalCenter: reloadButton.verticalCenter
          iconText: "󰒓"
          tooltipText: kinetic.tr("app.settings")
          selected: kinetic.tab === "settings"
          foreground: kinetic.fg
          fontFamily: kinetic.fontFamily
          onClicked: kinetic.tab = "settings"
          Rectangle {
            visible: kinetic.updateWaiting
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Style.space(3)
            width: Style.space(7)
            height: width
            radius: width / 2
            color: kinetic.accent
          }
        }

        PlainText {
          id: keyHint
          anchors.left: parent.left
          anchors.right: parent.right
          anchors.bottom: parent.bottom
          anchors.margins: Style.space(18)
          text: kinetic.tr("app.keys")
          wrapMode: Text.Wrap
          elide: Text.ElideNone
          opacity: 0.35
          font.pixelSize: Style.font.caption
        }
      }

      // Drop a wallpaper or a module (a folder, an archive, a link) anywhere. A
      // wallpaper (or a collection of them) is added to the library; anything
      // else goes to the Modules page, which asks before installing anything.
      DropArea {
        id: drop
        anchors.fill: parent
        onEntered: function(d) { d.accept(Qt.CopyAction) }
        onDropped: function(d) {
          var src = d.hasUrls && d.urls.length ? String(d.urls[0]) : d.hasText ? String(d.text).trim().split("\n")[0] : ""
          if (src === "") return
          kinetic.tab = "library"
          kinetic.addWallpaper(src, true)
          d.accept(Qt.CopyAction)
        }
      }
      Rectangle {
        anchors.fill: parent
        anchors.margins: Style.space(8)
        z: 10
        visible: drop.containsDrag
        radius: Style.cornerRadius
        color: Qt.rgba(kinetic.accent.r, kinetic.accent.g, kinetic.accent.b, 0.12)
        border.width: 2
        border.color: kinetic.accent
        PlainText { anchors.centerIn: parent; text: kinetic.tr("modules.dropHere"); font.pixelSize: Style.font.title; color: kinetic.accent }
      }

      // ---------- the page showing ----------
      Item {
        id: main
        anchors.left: side.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: Style.space(24)

        Column {
          id: head
          anchors.left: parent.left
          anchors.right: parent.right
          anchors.top: parent.top
          spacing: Style.space(12)

          // the wallpaper's pages lead with what is showing and its switch
          WallHero {
            width: parent.width
            visible: !!root.current.hero
            ctx: kinetic
          }
          Column {
            width: parent.width
            visible: !root.current.hero
            spacing: Style.space(2)
            // a nested page names its group first
            PlainText {
              width: parent.width
              visible: !!root.current.parentLabel
              text: root.current.parentLabel + "  ›"
              opacity: 0.55
              font.pixelSize: Style.font.caption
            }
            PlainText { text: root.current.label; font.pixelSize: Style.font.title; font.bold: true }
          }
          TextField {
            id: search
            width: parent.width
            visible: !!root.current.search && !kinetic.offline
            placeholderText: kinetic.tr("search")
            foreground: kinetic.fg
            text: kinetic.query
            onTextChanged: if (text !== kinetic.query) { kinetic.query = text; scroll.ScrollBar.vertical.position = 0 }
            Component.onCompleted: kinetic.searchField = search
            onAccepted: if (kinetic.shown.length > 0) { kinetic.send("wall", ["set", kinetic.shown[0].id]); kinetic.query = ""; kinetic.tab = "wallpaper" }
            Keys.onEscapePressed: function(event) {
              if (kinetic.query !== "") kinetic.query = ""
              else keys.forceActiveFocus()
              event.accepted = true
            }
          }
          PlainText {
            width: parent.width
            visible: kinetic.offline && root.current.module === "wall"
            text: kinetic.tr("offline")
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            opacity: 0.7
          }
        }

        ScrollView {
          id: scroll
          anchors.left: parent.left
          anchors.right: parent.right
          anchors.top: head.bottom
          anchors.topMargin: Style.space(16)
          anchors.bottom: parent.bottom
          clip: true
          ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
          ScrollBar.vertical.policy: body.implicitHeight > height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
          Binding { target: scroll.contentItem; property: "interactive"; value: body.implicitHeight > scroll.height }
          Component.onCompleted: kinetic.scrollArea = scroll

          Column {
            id: body
            width: Math.min(scroll.availableWidth - Style.space(12), Style.space(720))
            spacing: Style.space(12)

            // A page is loaded when first shown, then kept: Kinetic's own from the
            // app, a module's own from its folder. Each gets `ctx` (KineticCtx).
            Repeater {
              model: root.entries
              delegate: Loader {
                id: pageLoader
                required property var modelData
                readonly property bool here: root.current.id === modelData.id
                property bool wanted: false
                width: parent.width
                visible: here && !(kinetic.offline && modelData.module === "wall")
                onHereChanged: if (here && !wanted) { wanted = true; setSource(modelData.url, Object.assign({ ctx: kinetic }, modelData.props)) }
                Component.onCompleted: if (here) { wanted = true; setSource(modelData.url, Object.assign({ ctx: kinetic }, modelData.props)) }
                onStatusChanged: if (status === Loader.Error) console.warn("Kinetic: page " + modelData.id + " did not load: " + modelData.url)
              }
            }
          }
        }
      }
    }
  }
}
