import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import "Model.js" as Model
import "schema.js" as Schema
import "KineticEnv.js" as KineticEnv

// What every Kinetic page works with (`ctx`): the data, read from the CLIs and
// the config files as they change, and the commands, which all go through the
// CLIs, so the pages, the command line and the files never disagree. The bar
// widget (Panel.qml) and the app window (App.qml) each hold one.
Item {
  id: root
  visible: false

  property QtObject bar: null        // the bar the widget sits on; null in the app window
  property bool active: false        // the host is showing: refresh now and keep live pages fresh
  property bool live: false          // the host shows a page with live engine numbers (fps, GPU)
  property string tab: "wallpaper"   // the page showing; pages may move it (the library, after a pick)
  property string query: ""          // the library's search text
  property Item searchField: null
  property Item scrollArea: null     // the host's ScrollView, which the wheel over a slider scrolls
  property Item focusItem: null      // what takes the keys back after a dialog or field
  property bool editingValue: false  // a value box or text field has the keys

  Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }
  function tr(key) { return strings.t(key) }

  // ---------- data ----------
  property var info: null          // parsed `kinetic-wall describe`
  property bool offline: false
  readonly property var wp: info ? info.wallpaper : null
  readonly property var shown: info ? Model.filterLibrary(info.library, query) : []
  readonly property var palette: info ? info.palette : []
  // The engine is on screen: a live wallpaper, or Omarchy's background held for the glass.
  readonly property bool engineShowing: !!info && (info.enabled || !!info.still)

  property var shellCfg: Model.parseShell("")  // bar id, background, raw pill options
  readonly property var barRows: shellCfg ? Schema.items(shellCfg.pill, KineticEnv.SCHEMA) : []
  readonly property var barGroups: Schema.groups(barRows, KineticEnv.SCHEMA)
  readonly property var barPresets: Object.keys(KineticEnv.SCHEMA.presets)
  readonly property var pillNow: shellCfg ? Schema.normalize(shellCfg.pill, KineticEnv.SCHEMA) : ({})
  readonly property bool kineticBar: !!shellCfg && shellCfg.id === "kinetic.bar"
  readonly property bool pillOn: kineticBar && shellCfg.background === "pill"
  // Who draws the glass behind the pill right now; mirrors the bar's own rule.
  readonly property string glassNow: !pillOn || pillNow.opacity >= 1 || pillNow.glass === "none" ? "none"
    : pillNow.glass === "wallpaper" || (pillNow.glass === "auto" && engineShowing) ? "wallpaper"
    : "blur"

  // Motion: ~/.config/kinetic/motion.json ({enabled, options}), listed against its schema.
  property var motionCfg: ({ enabled: false, options: {} })
  readonly property var motionGroups: Schema.groups(Schema.items(motionCfg.options, KineticEnv.MOTION_SCHEMA), KineticEnv.MOTION_SCHEMA)
  readonly property var motionPresets: Object.keys(KineticEnv.MOTION_SCHEMA.presets)

  property var presets: []
  property string currentPreset: ""

  // ---------- modules ----------
  // `kinetic modules --json`: every module (Kinetic's and the ones you added), its
  // manifest, whether it is on, what it needs. Until it has answered, everything counts as on.
  property var modules: []
  function refreshModules() { if (!modulesProc.running) modulesProc.running = true }
  function moduleInfo(id) {
    for (var i = 0; i < modules.length; i++) if (modules[i].id === id) return modules[i]
    return null
  }
  function moduleActive(id) { var m = moduleInfo(id); return !m || m.active !== false }
  // The engine option groups modules own (shown on their pages, not the Engine page).
  readonly property var ownedEngineGroups: {
    var g = []
    for (var i = 0; i < modules.length; i++) if (modules[i].id !== "wall") g = g.concat(modules[i].engineGroups || [])
    return g
  }
  // Every page the modules declare, with what loads it: module by module (in the
  // modules' order), and a module's pages in their own `order`. The app shows a
  // module's first page as its row and nests the others under it.
  readonly property var pages: {
    var out = []
    for (var i = 0; i < modules.length; i++) {
      var m = modules[i], own = []
      for (var j = 0; j < (m.pages || []).length; j++) {
        var p = m.pages[j], q = String(p.qml)
        own.push({ id: p.id, module: m.id, label: p.label, about: p.about, icon: p.icon, order: p.order, at: j,
                   hero: !!p.hero, search: !!p.search, props: p.props || {}, builtin: m.builtin,
                   url: q.indexOf("builtin:") === 0 ? Qt.resolvedUrl("pages/" + q.slice(8) + ".qml") : "file://" + q })
      }
      own.sort(function(a, b) { return (a.order - b.order) || (a.at - b.at) })
      out = out.concat(own)
    }
    return out
  }

  // Through `kinetic <module> enable|disable` (the wallpaper's switch is its service).
  // `yes`: switch off what needs it too, which the page has already asked about.
  function setModule(id, on, yes) {
    if (!Model.safeArg(id)) return
    var q = queue.slice()
    q.push({ module: "modules", command: [KineticEnv.KINETIC, id, on ? "enable" : "disable"].concat(yes ? ["--yes"] : []) })
    queue = q
    pump()
  }

  // Adding and removing modules: inspect first (fetches, runs nothing), then
  // install exactly what was inspected, after the page's yes.
  property bool moduleBusy: moduleProc.running
  property var modulePlan: null      // what an inspect staged: {staging, modules, optional, warning}
  property string moduleError: ""
  property string moduleNote: ""
  property var askDisable: null      // {id, users}: switching id off would stop these too
  function safeSource(s) { s = String(s).trim(); return s.length > 0 && s.length < 600 && s.charAt(0) !== "-" && !/[\n\r\0]/.test(s) }
  function moduleRun(kind, args) {
    if (moduleProc.running) return
    moduleError = ""
    moduleNote = ""
    moduleProc.kind = kind
    moduleProc.command = [KineticEnv.KINETIC, "module"].concat(args)
    moduleProc.running = true
  }
  function inspectModule(source) {
    var s = String(source).trim()
    if (s.indexOf("file://") === 0) s = decodeURIComponent(s.slice(7))
    if (!safeSource(s)) { moduleError = tr("modules.badSource"); return }
    modulePlan = null
    moduleRun("inspect", ["inspect", "--json", s])
  }
  function installPlan() { if (modulePlan) moduleRun("install", ["install", "--staged", modulePlan.staging, "--yes"]) }
  function cancelPlan() { modulePlan = null }
  function removeModule(id) { if (Model.safeArg(id)) moduleRun("remove", ["remove", id, "--yes"]) }
  // Before switching a module off: which modules it would stop as well.
  function requestDisable(id) {
    if (!Model.safeArg(id) || planDisableProc.running) return
    planDisableProc.target = id
    planDisableProc.command = [KineticEnv.KINETIC, "module", "plan-disable", id]
    planDisableProc.running = true
  }

  // Adding wallpapers: fetched the way modules are, each compiled first, then
  // kept in ~/.config/kinetic/wallpapers (`kinetic wall add`). A wallpaper runs
  // no code, so what you paste or drop is added without a second question.
  // fromDrop: something dropped on the window that holds no wallpaper is
  // handed to the module installer instead.
  property bool wallBusy: wallAddProc.running
  property string wallError: ""
  property string wallNote: ""
  function addWallpaper(source, fromDrop) {
    var s = String(source).trim()
    if (s.indexOf("file://") === 0) s = decodeURIComponent(s.slice(7))
    if (wallAddProc.running) return
    if (!safeSource(s)) { wallError = tr("wall.badSource"); return }
    wallError = ""
    wallNote = ""
    wallAddProc.fromDrop = !!fromDrop
    wallAddProc.source = s
    wallAddProc.command = [KineticEnv.KINETIC, "wall", "add", "--json", "--yes", s]
    wallAddProc.running = true
  }

  // ---------- glass ----------
  // One look for the bar's glass and the windows': the engine stores the windows'
  // (wall.toml), the bar its own (shell.json). Kind "glass" sets both at once.
  readonly property var glassLook: ({
    window_refract: "refraction", window_frost: "frost", window_dispersion: "dispersion",
    window_curve: "curvature", window_saturation: "saturation", window_brightness: "brightness",
    window_specular: "specular", window_light: "lightAngle"
  })
  function engineOption(key) {
    var c = info ? info.config : []
    for (var i = 0; i < c.length; i++) if (c[i].key === key) return c[i]
    return null
  }
  readonly property string windowGlass: { var o = engineOption("window_glass"); return o ? String(o.value) : "off" }
  readonly property bool barGlassOn: pillOn && (pillNow.glass === "auto" || pillNow.glass === "wallpaper")
  readonly property bool glassOn: barGlassOn || windowGlass !== "off"

  // The master switch. Off: the bar goes back to Hyprland's blur, windows lose the
  // glass, and turning the wallpaper off hands the desktop back to Omarchy.
  function setGlass(on) {
    if (on) {
      if (pillOn && !barGlassOn) send("bar", ["set", "glass", "auto"])
      if (windowGlass === "off") send("wall", ["config", "window_glass", "see-through"])
      send("wall", ["config", "glass_still", "1"])
    } else {
      if (barGlassOn) send("bar", ["set", "glass", "blur"])
      if (windowGlass !== "off") send("wall", ["config", "window_glass", "off"])
      send("wall", ["config", "glass_still", "0"])
    }
  }

  readonly property color fg: bar ? bar.foreground : Commons.Color.foreground
  readonly property color accent: Commons.Color.accent
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family

  // ---------- talking to the modules ----------
  property var queue: []
  property var pending: ({})       // slider drags: latest value per key, sent on a short timer

  function refresh() { if (!describeProc.running) describeProc.running = true }
  function refreshPresets() { if (!presetsProc.running) presetsProc.running = true }
  // Starts every part over (`kinetic reload`: the engine, the glass plugin, the shell with
  // the bar and this app). It outlives the shell, which it restarts, so it runs as a user unit.
  function reloadAll() {
    Quickshell.execDetached(["systemd-run", "--user", "--quiet", "--collect", "--unit=kinetic-reload", KineticEnv.KINETIC, "reload"])
  }
  function refocus() { if (focusItem) focusItem.forceActiveFocus() }

  // Scrolls the host by a wheel event that a control under the pointer caught
  // (SettingRow's sliders do, so the wheel scrolls instead of changing them).
  function scrollWheel(wheel) {
    var f = scrollArea ? scrollArea.contentItem : null
    if (!f || !f.interactive) return
    var dy = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.angleDelta.y / 120 * Style.space(60)
    f.contentY = Math.max(0, Math.min(Math.max(0, f.contentHeight - f.height), f.contentY - dy))
  }

  // The Kinetic app, on one of its modules.
  function openApp(module) {
    if (!Model.safeArg(module)) return
    Quickshell.execDetached([KineticEnv.KINETIC, "app", module])
  }

  // module: "wall" talks to the engine directly; the others go through kinetic.
  function send(module, args) {
    for (var i = 0; i < args.length; i++) if (!Model.safeArg(args[i])) return
    var cmd = module === "wall" ? [KineticEnv.WALL] : [KineticEnv.KINETIC, module]
    var q = queue.slice()
    q.push({ module: module, command: cmd.concat(args) })
    queue = q
    pump()
  }

  function pump() {
    if (cmdProc.running || queue.length === 0) return
    var q = queue.slice()
    var next = q.shift()
    queue = q
    cmdProc.module = next.module
    cmdProc.command = next.command
    cmdProc.running = true
  }

  // Each change is one or more commands: [module, args].
  function commandsFor(kind, key, value) {
    if (kind === "param") return [["wall", ["param", key, String(value)]]]
    if (kind === "config") return [["wall", ["config", key, String(value)]]]
    if (kind === "motion") return [["motion", ["set", key, String(value)]]]
    if (kind === "glass") { // the windows' glass, and the bar's matching option
      var c = [["wall", ["config", key, String(value)]]]
      if (glassLook[key]) c.push(["bar", ["set", glassLook[key], String(value)]])
      return c
    }
    return [["bar", ["set", key, String(value)]]]
  }

  function sendAll(cmds) { for (var i = 0; i < cmds.length; i++) send(cmds[i][0], cmds[i][1]) }

  function setValue(kind, key, value) {
    if (typeof value === "boolean") value = kind === "bar" || kind === "motion" ? (value ? "true" : "false") : (value ? "1" : "0")
    sendAll(commandsFor(kind, key, value))
  }

  function setLater(kind, key, value) {
    var p = Object.assign({}, pending)
    p[kind + "\u0000" + key] = commandsFor(kind, key, value)
    pending = p
    flushTimer.restart()
  }

  function flush() {
    var p = pending
    pending = ({})
    for (var k in p) sendAll(p[k])
  }

  function resetValue(kind, item) {
    if (kind === "param") send("wall", ["param", item.name, String(item["default"])])
    else if (kind === "config") send("wall", ["config", item.key, "default"])
    else if (kind === "glass") {
      send("wall", ["config", item.key, "default"])
      if (glassLook[item.key]) send("bar", ["unset", glassLook[item.key]])
    } else send(kind, ["unset", item.key])
  }

  Process {
    id: describeProc
    command: [KineticEnv.WALL, "describe"]
    stdout: StdioCollector { id: describeOut; waitForEnd: true }
    onExited: function(exitCode) {
      var d = exitCode === 0 ? Model.parseDescribe(describeOut.text) : null
      root.offline = d === null
      if (d) root.info = d
    }
  }

  Process {
    id: presetsProc
    command: [KineticEnv.KINETIC, "preset", "list", "--json"]
    stdout: StdioCollector { id: presetsOut; waitForEnd: true }
    onExited: function(exitCode) { if (exitCode === 0) root.presets = Model.parsePresets(presetsOut.text) }
  }

  Process {
    id: moduleProc
    property string kind: ""
    stdout: StdioCollector { id: moduleOut; waitForEnd: true }
    stderr: StdioCollector { id: moduleErr; waitForEnd: true }
    onExited: function(exitCode) {
      var err = String(moduleErr.text).trim().split("\n").pop().replace(/^kinetic module: /, "")
      if (exitCode !== 0) { root.moduleError = err || root.tr("modules.failed"); if (kind === "install") root.modulePlan = null }
      else if (kind === "inspect") {
        try { root.modulePlan = JSON.parse(moduleOut.text) } catch (e) { root.moduleError = root.tr("modules.failed") }
      } else {
        root.moduleNote = String(moduleOut.text).trim()
        if (kind === "install") root.modulePlan = null
      }
      root.refreshModules()
    }
  }

  Process {
    id: wallAddProc
    property bool fromDrop: false
    property string source: ""
    stdout: StdioCollector { id: wallAddOut; waitForEnd: true }
    onExited: function(exitCode) {
      var r = null
      try { r = JSON.parse(wallAddOut.text) } catch (e) {}
      if (r && r.notWallpaper && fromDrop) { root.tab = "modules"; root.inspectModule(source); return }
      if (!r || r.error) { root.wallError = r && r.error ? r.error : root.tr("wall.addFailed"); return }
      var added = r.added || [], skipped = r.skipped || []
      if (added.length) root.wallNote = root.tr("wall.added").replace("%1", added.map(function(a) { return a.name }).join(", "))
      if (skipped.length) root.wallError = skipped.map(function(k) { return k.id + ": " + String(k.why).split("\n")[0] }).join("\n")
      root.refresh()
    }
  }

  Process {
    id: planDisableProc
    property string target: ""
    stdout: StdioCollector { id: planDisableOut; waitForEnd: true }
    onExited: function(exitCode) {
      var users = String(planDisableOut.text).trim()
      if (exitCode !== 0) return
      if (users === "") root.setModule(target, false, false)
      else root.askDisable = { id: target, users: users.split("\n") }
    }
  }

  Process {
    id: modulesProc
    command: [KineticEnv.KINETIC, "modules", "--json"]
    stdout: StdioCollector { id: modulesOut; waitForEnd: true }
    onExited: function(exitCode) {
      if (exitCode !== 0) return
      try { var m = JSON.parse(modulesOut.text); if (Array.isArray(m)) root.modules = m } catch (e) {}
    }
  }

  Process {
    id: cmdProc
    property string module: ""
    onExited: function(exitCode) {
      if (module === "preset") root.refreshPresets()
      if (root.queue.length > 0) root.pump()
      else {
        if (module !== "bar" && module !== "motion") root.refresh()
        root.refreshModules() // any change may switch a module on or off (the glass, the light...)
      }
    }
  }

  // The bar's options: shell.json, read as it changes (any writer: panel, CLI, editor).
  FileView {
    path: Quickshell.env("HOME") + "/.config/omarchy/shell.json"
    watchChanges: true
    printErrors: false
    onFileChanged: reload()
    onLoaded: { var c = Model.parseShell(text()); if (c) root.shellCfg = c }
  }

  FileView {
    path: KineticEnv.MOTION_CONFIG
    watchChanges: true
    printErrors: false
    onFileChanged: reload()
    onLoaded: root.motionCfg = Model.parseMotion(text())
    onLoadFailed: root.motionCfg = Model.parseMotion("")
  }

  FileView {
    path: KineticEnv.PRESET_STATE
    watchChanges: true
    printErrors: false
    onFileChanged: reload()
    onLoaded: root.currentPreset = text().trim()
    onLoadFailed: root.currentPreset = ""
  }

  // The engine changed (on, off, restarted): its signal file says so.
  FileView {
    path: KineticEnv.WALL_SIGNAL
    watchChanges: true
    printErrors: false
    onFileChanged: { reload(); if (root.active) root.refresh() }
  }

  Timer { id: flushTimer; interval: 60; onTriggered: root.flush() }
  // fps and GPU time move on their own: read them every two seconds, only while shown
  Timer { interval: 2000; repeat: true; running: root.active && root.live; onTriggered: root.refresh() }

  onActiveChanged: if (active) {
    refresh()
    refreshPresets()
    refreshModules()
    Qt.callLater(root.refocus)
  }
  Component.onCompleted: refresh()
}
