import QtQuick

// One capsule behind each group of widgets: start, center, end.
Item {
  id: islands

  property var bar: null
  property var glass: null
  property Item startList: null
  property Item endList: null
  property var centerGroup: null  // CenterModules / CenterSections: groupStart, groupEnd, hasContent

  visible: !!bar && bar.pillIslands
  readonly property bool vertical: !!bar && bar.vertical
  readonly property real inset: bar ? bar.pillIslandInset : 0
  readonly property real pad: bar ? bar.pillConfig.padding : 0

  function along(item) { return vertical ? item.y : item.x }
  function extent(item) { return vertical ? item.height : item.width }

  component Island: PillSurface {
    property bool shown: false
    property real a: 0
    property real b: 0
    readonly property real lo: Math.max(0, Math.round(a))
    readonly property real hi: Math.min(islands.vertical ? islands.height : islands.width, Math.round(b))
    bar: islands.bar
    glass: islands.glass
    visible: islands.visible && shown && hi > lo
    x: islands.vertical ? -islands.pad : lo
    y: islands.vertical ? lo : -islands.pad
    width: islands.vertical ? islands.width + 2 * islands.pad : hi - lo
    height: islands.vertical ? hi - lo : islands.height + 2 * islands.pad
  }

  Island {
    shown: !!islands.startList && islands.startList.visible && islands.extent(islands.startList) > 0
    a: shown ? islands.along(islands.startList) - islands.inset : 0
    b: shown ? islands.along(islands.startList) + islands.extent(islands.startList) + islands.inset : 0
  }
  Island {
    shown: !!islands.centerGroup && islands.centerGroup.hasContent
    a: shown ? islands.centerGroup.groupStart - islands.inset : 0
    b: shown ? islands.centerGroup.groupEnd + islands.inset : 0
  }
  Island {
    shown: !!islands.endList && islands.endList.visible && islands.extent(islands.endList) > 0
    a: shown ? islands.along(islands.endList) - islands.inset : 0
    b: shown ? islands.along(islands.endList) + islands.extent(islands.endList) + islands.inset : 0
  }
}
