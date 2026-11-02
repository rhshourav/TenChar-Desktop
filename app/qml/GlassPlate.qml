import QtQuick
import QtQuick.Shapes

// A frosted plate: a translucent gradient fill, an optional tint, a lit hairline edge and a thin specular
// line along the top. There is deliberately no backdrop blur; the aurora behind supplies the "refraction".
// Reduced transparency turns it into an opaque plum surface with an outline. High contrast makes edges solid.
Item {
    id: p
    property real radius: 28
    property real rTL: radius
    property real rTR: radius
    property real rBR: radius
    property real rBL: radius
    property color tint: "transparent"
    property color tintB: "transparent"   // when its alpha is above 0 the tint is a diagonal gradient from tint to tintB
    property bool brand: false             // solid mint-to-lavender fill (primary actions), no edge
    readonly property bool opaque: ui.reducedTransparency
    readonly property bool contrast: ui.highContrast
    /// A dark base under the glass fill. Without it, text over the lightest part of the aurora falls below 4.5:1
    /// (measured by tests/contrast.py). Keep this value in step with that test.
    readonly property real scrim: 0.45

    function arc(r, ex, ey) { return r > 0.01 ? " A" + r + " " + r + " 0 0 1 " + ex + " " + ey : " L" + ex + " " + ey }
    function outline(i) {
        var w = width, h = height, m = Math.min(w, h) / 2
        function c(r) { return Math.max(0, Math.min(r, m) - i) }
        var x0 = i, y0 = i, x1 = w - i, y1 = h - i
        var a = c(rTL), b = c(rTR), d = c(rBR), e = c(rBL)
        return "M" + (x0 + a) + " " + y0 + " L" + (x1 - b) + " " + y0 + arc(b, x1, y0 + b) +
               " L" + x1 + " " + (y1 - d) + arc(d, x1 - d, y1) +
               " L" + (x0 + e) + " " + y1 + arc(e, x0, y1 - e) +
               " L" + x0 + " " + (y0 + a) + arc(a, x0 + a, y0) + " Z"
    }

    Shape {
        anchors.fill: parent
        visible: p.width > 1 && p.height > 1
        ShapePath {   // scrim
            strokeColor: "transparent"
            fillColor: p.brand || p.opaque ? "transparent" : Qt.rgba(Tc.space.r, Tc.space.g, Tc.space.b, p.scrim)
            PathSvg { path: p.outline(0) }
        }
        ShapePath {   // fill
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0; y1: 0; x2: p.width; y2: p.height
                GradientStop { position: 0; color: p.brand ? Tc.mint : p.opaque ? Tc.plum : Qt.rgba(1, 1, 1, p.contrast ? 0.20 : 0.13) }
                GradientStop { position: 1; color: p.brand ? Tc.lavender : p.opaque ? Tc.plum : Qt.rgba(1, 1, 1, p.contrast ? 0.12 : 0.05) }
            }
            PathSvg { path: p.outline(0) }
        }
        ShapePath {   // tint
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0; y1: 0; x2: p.width; y2: p.height
                GradientStop { position: 0; color: p.opaque ? "transparent" : p.tint }
                GradientStop { position: 1; color: p.opaque ? "transparent" : (p.tintB.a > 0 ? p.tintB : p.tint) }
            }
            PathSvg { path: p.outline(0) }
        }
        ShapePath {   // edge: bright on the lit corner, almost gone in the middle, a little back at the far edge
            strokeColor: "transparent"
            fillRule: ShapePath.OddEvenFill
            fillGradient: LinearGradient {
                x1: 0; y1: 0; x2: p.width; y2: p.height
                GradientStop { position: 0; color: p.brand ? "transparent" : p.contrast ? "white" : p.opaque ? Qt.rgba(1, 1, 1, 0.32) : Qt.rgba(1, 1, 1, 0.34) }
                GradientStop { position: 0.5; color: p.brand ? "transparent" : p.contrast ? "white" : p.opaque ? Qt.rgba(1, 1, 1, 0.32) : Qt.rgba(1, 1, 1, 0.05) }
                GradientStop { position: 1; color: p.brand ? "transparent" : p.contrast ? "white" : p.opaque ? Qt.rgba(1, 1, 1, 0.32) : Qt.rgba(1, 1, 1, 0.16) }
            }
            PathSvg { path: p.outline(0) + " " + p.outline(1) }
        }
    }
    Rectangle {   // specular line
        readonly property real inset: Math.min(p.height / 2, 20)
        visible: !p.brand && !p.opaque
        x: inset; y: 0; height: 1
        width: Math.max(0, p.width - 2 * inset)
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0) }
            GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.40) }
            GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0) }
        }
    }
}
