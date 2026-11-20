import QtQuick
import qs.Commons
import qs.Ui
import "../components"
import "../Model.js" as Model

// The wallpaper engine's global options, grouped.
Column {
  id: page
  property var ctx: null
  // groups other modules own and show on their own pages (Glass, Light ...)
  property var exclude: ctx ? ctx.ownedEngineGroups : []
  spacing: Style.space(10)

  Repeater {
    model: page.ctx.info ? Model.groupConfig(page.ctx.info.config).filter(function(g) { return page.exclude.indexOf(g.group) < 0 }) : []
    delegate: Column {
      required property var modelData
      width: page.width
      spacing: Style.space(10)
      PanelSectionHeader { text: modelData.group; foreground: page.ctx.fg; fontFamily: page.ctx.fontFamily }
      Repeater {
        model: modelData.items
        delegate: SettingRow {
          required property var modelData
          ctx: page.ctx
          item: modelData
          kind: "config"
        }
      }
    }
  }
}
