import QtQuick
import QtQuick.Effects
import qs.Commons
import qs.Commons as Commons

// One capsule. Styles: liquid (nearly clear, lit rim, sheen, glint), glass
// (tinted), frosted (milky), solid, outline. What is behind it comes from the
// wallpaper engine's lens (bent, real pixels) or Hyprland's blur; this item
// only draws the tint and the light on top.
Item {
  id: surface

  property var bar: null     // the bar root: pillConfig, colors
  property var glass: null   // PillGlass: where this capsule reports its rectangle

  readonly property var cfg: bar ? bar.pillConfig : ({})
  readonly property real r: Math.max(0, Math.min(cfg.radius === "full" ? Math.min(width, height) / 2 : cfg.radius,
                                                 Math.min(width, height) / 2))
  readonly property bool liquid: cfg.style === "liquid" || cfg.style === "frosted"
  readonly property real hl: cfg.highlight || 0
  readonly property color tintBase: colorFor(cfg.tint, bar ? bar.background : "black")
  readonly property color fill: Qt.rgba(tintBase.r, tintBase.g, tintBase.b, cfg.opacity)
  readonly property color edgeBase: colorFor(cfg.borderColor, bar ? bar.barForeground : "white")
  readonly property color edge: Qt.rgba(edgeBase.r, edgeBase.g, edgeBase.b, cfg.borderOpacity)
  readonly property real bw: cfg.border || 0

  function colorFor(value, fallback) {
    var s = String(value === undefined || value === null ? "" : value)
    if (s === "accent") return Commons.Color.accent
    if (s.charAt(0) === "#") return s
    return fallback
  }

  // The lens needs to know where this capsule is on screen.
  onXChanged: if (glass) glass.schedule()
  onYChanged: if (glass) glass.schedule()
  onWidthChanged: if (glass) glass.schedule()
  onHeightChanged: if (glass) glass.schedule()
  onVisibleChanged: if (glass) glass.schedule()
  onRChanged: if (glass) glass.schedule()
  Component.onCompleted: if (glass) glass.add(surface)
  Component.onDestruction: if (glass) glass.remove(surface)

  // A drop shadow drawn only outside the capsule, so a clear pill is not
  // darkened from within.
  Item {
    id: shadowBox
    visible: !!surface.cfg.shadow && surface.cfg.shadowOpacity > 0 && surface.width > 0
    readonly property real reach: surface.cfg.shadowSize * 2 + 4
    x: -reach; y: -reach
    width: surface.width + 2 * reach
    height: surface.height + 2 * reach
    layer.enabled: visible
    layer.effect: MultiEffect {
      maskEnabled: true
      maskInverted: true
      maskThresholdMin: 0.5
      maskSpreadAtMin: 0.5
      maskSource: shadowMask
    }

    RectangularShadow {
      x: shadowBox.reach; y: shadowBox.reach + Math.round(surface.cfg.shadowSize / 6)
      width: surface.width; height: surface.height
      radius: surface.r
      blur: surface.cfg.shadowSize
      spread: 0
      color: Qt.rgba(0, 0, 0, surface.cfg.shadowOpacity)
    }
  }

  Item {
    id: shadowMask
    visible: false
    layer.enabled: shadowBox.visible
    width: shadowBox.width; height: shadowBox.height
    Rectangle {
      x: shadowBox.reach; y: shadowBox.reach
      width: surface.width; height: surface.height
      radius: surface.r
      color: "black"
    }
  }

  // The tint.
  Rectangle {
    anchors.fill: parent
    radius: surface.r
    color: surface.fill
    visible: surface.cfg.opacity > 0
  }

  // Light from above: a bright top falling off, a faint bounce at the bottom.
  Rectangle {
    anchors.fill: parent
    radius: surface.r
    visible: surface.liquid && surface.hl > 0
    gradient: Gradient {
      GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.16 * surface.hl) }
      GradientStop { position: 0.42; color: Qt.rgba(1, 1, 1, 0.03 * surface.hl) }
      GradientStop { position: 0.7; color: Qt.rgba(0, 0, 0, 0.03 * surface.hl) }
      GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.07 * surface.hl) }
    }
  }

  // The lens edge: a soft band just inside the rim, where glass thickens.
  PillRing {
    visible: surface.liquid && surface.hl > 0
    ringWidth: Math.max(2, Math.round(surface.r * 0.3))
    radius: surface.r
    topAlpha: 0.10 * surface.hl
    midAlpha: 0.0
    bottomAlpha: 0.12 * surface.hl
  }

  // The specular rim: brightest along the top, glinting again at the bottom.
  PillRing {
    visible: surface.liquid && surface.bw > 0
    ringWidth: surface.bw
    radius: surface.r
    topAlpha: Math.min(1, 0.55 * Math.max(surface.hl, 0.4))
    midAlpha: 0.10 * Math.max(surface.hl, 0.4)
    bottomAlpha: Math.min(1, 0.32 * Math.max(surface.hl, 0.4))
  }

  // A dark hairline outside the rim keeps a clear pill readable on a bright wallpaper.
  Rectangle {
    visible: surface.liquid && surface.bw > 0
    anchors.fill: parent
    anchors.margins: -1
    radius: surface.r + 1
    color: "transparent"
    border.width: 1
    border.color: Qt.rgba(0, 0, 0, 0.18)
  }

  // A glint across the upper left.
  Rectangle {
    visible: surface.liquid && surface.hl > 0 && surface.width > surface.height
    x: Math.round(surface.r * 0.7)
    y: Math.max(1, Math.round(surface.bw + 1))
    width: Math.max(0, surface.width * 0.4 - x)
    height: Math.max(1, Math.round(surface.height * 0.06))
    radius: height / 2
    gradient: Gradient {
      orientation: Gradient.Horizontal
      GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0) }
      GradientStop { position: 0.3; color: Qt.rgba(1, 1, 1, Math.min(1, 0.45 * surface.hl)) }
      GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0) }
    }
  }

  // A plain border for the flat styles.
  Rectangle {
    visible: !surface.liquid && surface.bw > 0
    anchors.fill: parent
    radius: surface.r
    color: "transparent"
    border.width: surface.bw
    border.color: surface.edge
  }
}
