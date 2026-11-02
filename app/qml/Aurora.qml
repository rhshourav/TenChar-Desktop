import QtQuick
import QtQuick.Shapes

// The one ambient moment: five soft colour fields drifting very slowly. Drawn once at the window root so
// screen changes never restart it. Still when motion is reduced or the window is not in front.
Item {
    id: a
    property bool active: true
    property real phase: ui.reducedMotion ? 0.2 * 2 * Math.PI : 0
    readonly property real w: width
    readonly property real h: height
    readonly property bool flat: ui.reducedTransparency

    NumberAnimation on phase {
        from: 0; to: 2 * Math.PI; duration: 60000; loops: Animation.Infinite
        running: a.active && !ui.reducedMotion && !a.flat
    }

    Rectangle { anchors.fill: parent; color: Tc.space }

    component Glow : Shape {
        id: g
        property color glowColor
        property real glowAlpha
        property real cx
        property real cy
        property real radius
        anchors.fill: parent
        visible: !a.flat
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: g.cx; centerY: g.cy; centerRadius: g.radius
                focalX: g.cx; focalY: g.cy; focalRadius: 0
                GradientStop { position: 0; color: Qt.rgba(g.glowColor.r, g.glowColor.g, g.glowColor.b, g.glowAlpha) }
                GradientStop { position: 1; color: Qt.rgba(g.glowColor.r, g.glowColor.g, g.glowColor.b, 0) }
            }
            PathSvg { path: "M0 0 L" + a.w + " 0 L" + a.w + " " + a.h + " L0 " + a.h + " Z" }
        }
    }

    Glow { glowColor: Tc.plum; glowAlpha: 1.0; cx: a.w * 0.5; cy: a.h * 0.45; radius: Math.max(a.w, a.h) * 0.95 }
    Glow { glowColor: Tc.indigo; glowAlpha: 0.55; cx: a.w * (0.10 + 0.10 * Math.sin(a.phase)); cy: a.h * (0.10 + 0.04 * Math.cos(a.phase)); radius: a.w * 1.0 }
    Glow { glowColor: Tc.lavender; glowAlpha: 0.22; cx: a.w * (0.95 - 0.10 * Math.cos(a.phase)); cy: a.h * (0.30 + 0.05 * Math.sin(a.phase)); radius: a.w * 0.7 }
    Glow { glowColor: Tc.peach; glowAlpha: 0.14; cx: a.w * (0.90 + 0.06 * Math.sin(a.phase + 2)); cy: a.h * (0.95 - 0.04 * Math.cos(a.phase)); radius: a.w * 0.8 }
    Glow { glowColor: Tc.mint; glowAlpha: 0.12; cx: a.w * (0.05 + 0.08 * Math.cos(a.phase + 1)); cy: a.h * (0.88 + 0.04 * Math.sin(a.phase)); radius: a.w * 0.7 }
}
