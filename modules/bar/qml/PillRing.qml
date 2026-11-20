import QtQuick
import QtQuick.Shapes

// A rounded ring lit by a vertical gradient: the rim light of the glass.
Shape {
  id: ring
  property real ringWidth: 1
  property real radius: 0
  property real topAlpha: 0.5
  property real midAlpha: 0.1
  property real bottomAlpha: 0.3
  property color lightColor: "white"

  anchors.fill: parent
  preferredRendererType: Shape.CurveRenderer

  ShapePath {
    fillRule: ShapePath.OddEvenFill
    strokeColor: "transparent"
    strokeWidth: 0
    fillGradient: LinearGradient {
      x1: 0; y1: 0; x2: 0; y2: ring.height
      GradientStop { position: 0.0; color: Qt.rgba(ring.lightColor.r, ring.lightColor.g, ring.lightColor.b, ring.topAlpha) }
      GradientStop { position: 0.5; color: Qt.rgba(ring.lightColor.r, ring.lightColor.g, ring.lightColor.b, ring.midAlpha) }
      GradientStop { position: 1.0; color: Qt.rgba(ring.lightColor.r, ring.lightColor.g, ring.lightColor.b, ring.bottomAlpha) }
    }
    PathRectangle {
      x: 0; y: 0
      width: ring.width; height: ring.height
      radius: ring.radius
    }
    PathRectangle {
      x: ring.ringWidth; y: ring.ringWidth
      width: Math.max(0, ring.width - 2 * ring.ringWidth)
      height: Math.max(0, ring.height - 2 * ring.ringWidth)
      radius: Math.max(0, ring.radius - ring.ringWidth)
    }
  }
}
