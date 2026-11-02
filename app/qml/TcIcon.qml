import QtQuick
import QtQuick.Shapes

// Icons are drawn, so no icon font or library is needed: a chevron and an arrow are all the design uses.
Item {
    id: i
    property string icon: "right"   // left, right, up
    property color color: Tc.ink
    property real size: 22
    width: size
    height: size
    Accessible.ignored: true
    function d() {
        var s = size
        if (icon === "left") return "M" + s*0.62 + " " + s*0.20 + " L" + s*0.34 + " " + s*0.50 + " L" + s*0.62 + " " + s*0.80
        if (icon === "right") return "M" + s*0.38 + " " + s*0.20 + " L" + s*0.66 + " " + s*0.50 + " L" + s*0.38 + " " + s*0.80
        return "M" + s*0.50 + " " + s*0.82 + " L" + s*0.50 + " " + s*0.20 + " M" + s*0.24 + " " + s*0.46 + " L" + s*0.50 + " " + s*0.20 + " L" + s*0.76 + " " + s*0.46
    }
    Shape {
        anchors.fill: parent
        ShapePath {
            strokeColor: i.color
            strokeWidth: i.size * 0.09
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: i.d() }
        }
    }
}
