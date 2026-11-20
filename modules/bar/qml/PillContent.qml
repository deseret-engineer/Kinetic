import QtQuick

// Where the bar's widgets sit: the whole strip normally, or inside the pill.
// Wraps the bar's widget Loader and draws the single capsule behind it.
Item {
  id: content

  property var bar: null     // the bar root
  property Item strip: null  // the strip along the screen edge
  property var glass: null   // PillGlass for this screen

  readonly property bool pill: !!bar && bar.pill
  readonly property var cfg: bar ? bar.pillConfig : ({})
  readonly property bool vertical: !!bar && bar.vertical
  readonly property real stripLength: !strip ? 0 : (vertical ? strip.height : strip.width)
  readonly property real stripCross: !strip ? 0 : (vertical ? strip.width : strip.height)
  readonly property real avail: Math.max(0, stripLength - 2 * cfg.sideMargin)
  readonly property real pillLength: bar && bar.pillIslands ? avail : Math.round(avail * cfg.length)
  readonly property real pillStart: cfg.sideMargin + (cfg.align === "start" ? 0
    : cfg.align === "end" ? avail - pillLength : Math.round((avail - pillLength) / 2))
  readonly property real endInset: bar && bar.pillIslands ? 0 : (bar ? bar.pillEndInset : 0)
  readonly property bool nearEdgeFirst: !!bar && (bar.position === "top" || bar.position === "left")
  readonly property real crossStart: !bar ? 0 : (nearEdgeFirst ? bar.pillOuterEdge : bar.pillInnerEdge)
  readonly property real crossSize: !bar ? 0 : Math.max(0, stripCross - bar.pillOuterEdge - bar.pillInnerEdge)
  readonly property real alongStart: pillStart + endInset
  readonly property real alongSize: Math.max(0, pillLength - 2 * endInset)

  x: !pill ? 0 : (vertical ? crossStart : alongStart)
  y: !pill ? 0 : (vertical ? alongStart : crossStart)
  width: !pill ? (strip ? strip.width : 0) : (vertical ? crossSize : alongSize)
  height: !pill ? (strip ? strip.height : 0) : (vertical ? alongSize : crossSize)

  PillSurface {
    bar: content.bar
    glass: content.glass
    visible: content.pill && !content.bar.pillIslands
    readonly property real pad: content.cfg.padding || 0
    x: content.vertical ? -pad : -content.endInset
    y: content.vertical ? -content.endInset : -pad
    width: content.width + 2 * (content.vertical ? pad : content.endInset)
    height: content.height + 2 * (content.vertical ? content.endInset : pad)
  }
}
