import QtQuick
import qs.Commons
import qs.Ui
import "../components"

// The app's own settings, the ones no module owns: where updates come from and
// whether to look for them, updating, and what Kinetic is built on.
// From `kinetic settings --json`, `kinetic update status|check|branches|apply`.
Column {
  id: page
  property var ctx: null
  spacing: Style.space(10)

  property bool confirmUpdate: false   // Update was clicked once: it restarts the bar and the app

  Component.onCompleted: { ctx.refreshSettings(); ctx.refreshUpdate(); ctx.refreshBranches() }

  component PlainText: Text {
    textFormat: Text.PlainText
    color: page.ctx.fg
    font.family: page.ctx.fontFamily
    font.pixelSize: Style.font.body
    elide: Text.ElideNone
    wrapMode: Text.Wrap
  }

  readonly property var u: ctx ? ctx.update : ({})
  readonly property var mine: u.installed || ({})
  function short(c) { return c ? String(c).slice(0, 7) : "" }
  function latest() {
    return (u.branch || "") + (u.latest ? " @ " + short(u.latest) : "")
      + (u.latestVersion ? " (Kinetic " + u.latestVersion + ")" : "")
  }
  function status() {
    if (page.ctx.updateChecking) return page.ctx.tr("settings.checking")
    switch (page.ctx.updateStatus) {
    case "current": return page.ctx.tr("settings.current").replace("%1", latest())
    case "available": return page.ctx.tr("settings.available").replace("%1", latest())
      + (u.behind ? " · " + page.ctx.tr("settings.behind").replace("%1", u.behind) : "")
    case "ahead": return page.ctx.tr("settings.ahead").replace("%1", latest())
    case "different": return page.ctx.tr("settings.different").replace("%1", latest())
    case "unknown": return page.ctx.tr("settings.unknown").replace("%1", latest())
    case "updating": return page.ctx.tr("settings.updating")
    case "failed": return page.ctx.tr("settings.failed")
    case "error": return page.ctx.tr("settings.error").replace("%1", String(u.error || ""))
    default: return page.ctx.tr("settings.notChecked")
    }
  }
  // the branch setting as a choice among the repository's branches (and the one set)
  function branchItem() {
    var it = page.ctx.appSetting("updateBranch")
    if (!it) return null
    var opts = page.ctx.branches.slice()
    if (opts.indexOf(it.value) < 0) opts.unshift(it.value)
    return Object.assign({}, it, { type: "choice", options: opts })
  }

  // ---------- updates ----------
  PanelSectionHeader { text: page.ctx.tr("settings.updates"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
  PlainText {
    width: parent.width
    text: page.ctx.tr("settings.installed").replace("%1", page.mine.version || "?")
      + (page.mine.commit ? " · " + (page.mine.branch || "") + " @ " + page.short(page.mine.commit) : "")
      + (page.mine.dirty ? " · " + page.ctx.tr("settings.localChanges") : "")
    opacity: 0.7
    font.pixelSize: Style.font.bodySmall
  }
  SettingRow {
    visible: page.branchItem() !== null
    ctx: page.ctx
    item: page.branchItem() || ({})
    kind: "settings"
  }
  SettingRow {
    visible: page.ctx.appSetting("updateCheck") !== null
    ctx: page.ctx
    item: page.ctx.appSetting("updateCheck") || ({})
    kind: "settings"
  }
  PlainText {
    width: parent.width
    text: page.status()
    color: page.ctx.updateWaiting || page.ctx.updateStatus === "failed" || page.ctx.updateStatus === "error"
      ? page.ctx.accent : page.ctx.fg
  }
  PlainText {
    width: parent.width
    visible: page.ctx.updateStatus === "failed"
    text: page.ctx.tr("settings.log").replace("%1", page.ctx.updateLog)
    opacity: 0.6
    font.pixelSize: Style.font.caption
  }
  Repeater { // what the update brings
    model: page.ctx.updateStatus === "available" ? (page.u.changes || []).slice(0, 12) : []
    delegate: PlainText {
      required property var modelData
      width: page.width
      text: "·  " + String(modelData)
      opacity: 0.7
      font.pixelSize: Style.font.bodySmall
    }
  }
  PlainText {
    width: parent.width
    visible: !!page.u.checked
    text: page.ctx.tr("settings.checkedAt").replace("%1", Qt.formatDateTime(new Date((page.u.checked || 0) * 1000), "yyyy-MM-dd hh:mm"))
    opacity: 0.45
    font.pixelSize: Style.font.caption
  }
  Row {
    spacing: Style.space(8)
    Button {
      visible: !page.confirmUpdate
      text: page.ctx.tr("settings.checkNow")
      bordered: true
      enabled: !page.ctx.updateChecking && page.ctx.updateStatus !== "updating"
      foreground: page.ctx.fg
      fontFamily: page.ctx.fontFamily
      onClicked: { page.ctx.checkUpdates(); page.ctx.refreshBranches() }
    }
    Button {
      visible: page.ctx.updateWaiting && !page.confirmUpdate && !page.ctx.updateChecking
      text: page.ctx.tr("settings.update")
      bordered: true
      foreground: page.ctx.accent
      fontFamily: page.ctx.fontFamily
      onClicked: page.confirmUpdate = true
    }
    PlainText {
      visible: page.confirmUpdate
      anchors.verticalCenter: parent.verticalCenter
      text: page.ctx.tr("settings.updateSure")
    }
    Button {
      visible: page.confirmUpdate
      text: page.ctx.tr("settings.updateConfirm")
      bordered: true
      foreground: page.ctx.accent
      fontFamily: page.ctx.fontFamily
      onClicked: { page.confirmUpdate = false; page.ctx.applyUpdate() }
    }
    Button {
      visible: page.confirmUpdate
      text: page.ctx.tr("settings.cancel")
      bordered: true
      foreground: page.ctx.fg
      fontFamily: page.ctx.fontFamily
      onClicked: page.confirmUpdate = false
    }
  }

  // ---------- about ----------
  PanelSectionHeader { text: page.ctx.tr("settings.about"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
  PlainText { width: parent.width; text: page.ctx.tr("settings.version").replace("%1", page.mine.version || "?") }
  PlainText { width: parent.width; text: page.ctx.tr("settings.license"); opacity: 0.7; font.pixelSize: Style.font.bodySmall }
  PlainText {
    width: parent.width
    visible: !!page.mine.source
    text: page.ctx.tr("settings.source").replace("%1", page.mine.source || "")
    opacity: 0.7
    font.pixelSize: Style.font.bodySmall
  }

  // ---------- attributions ----------
  PanelSectionHeader { text: page.ctx.tr("settings.attributions"); foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
  PlainText {
    width: parent.width
    text: page.ctx.tr("settings.attributionsAbout").replace("%1", "~/.local/share/kinetic/THIRD-PARTY.md")
    opacity: 0.7
    font.pixelSize: Style.font.bodySmall
  }
  Repeater {
    model: ["layerShell", "hyprland", "lowbias", "artOfCode", "omarchy", "builtWith"]
    delegate: Column {
      required property var modelData
      width: page.width
      spacing: Style.space(2)
      PlainText { width: parent.width; text: page.ctx.tr("attr." + modelData + ".name"); font.bold: true }
      PlainText {
        width: parent.width
        text: page.ctx.tr("attr." + modelData + ".what")
        opacity: 0.7
        font.pixelSize: Style.font.bodySmall
      }
    }
  }
}
