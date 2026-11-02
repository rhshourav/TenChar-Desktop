import QtQuick
import QtQuick.Shapes

// A send button wrapped in a ring that fills as the ten characters (or 64 bytes) are used.
Item {
    id: b
    property real fraction: 0
    property bool active: false
    property string name
    signal clicked()
    width: 52
    height: 52
    activeFocusOnTab: true
    opacity: 1
    property real shown: fraction
    Behavior on shown { NumberAnimation { duration: ui.reducedMotion ? 0 : 220; easing.type: Easing.OutCubic } }
    readonly property color ringColor: fraction >= 0.999 ? Tc.peach : fraction >= 0.8 ? Tc.lavender : Tc.mint
    scale: mouse.pressed && active && !ui.reducedMotion ? 0.95 : 1
    Behavior on scale { SpringAnimation { spring: 4; damping: 0.28; epsilon: 0.001 } }
    Keys.onReturnPressed: if (active) clicked()
    Keys.onSpacePressed: if (active) clicked()
    Accessible.role: Accessible.Button
    Accessible.name: name
    Accessible.onPressAction: if (active) clicked()

    Shape {
        anchors.fill: parent
        ShapePath {
            strokeColor: Qt.rgba(1, 1, 1, 0.12); strokeWidth: 3; fillColor: "transparent"
            PathAngleArc { centerX: 26; centerY: 26; radiusX: 24.5; radiusY: 24.5; startAngle: -90; sweepAngle: 359.9 }
        }
        ShapePath {
            strokeColor: b.ringColor; strokeWidth: 3; fillColor: "transparent"; capStyle: ShapePath.RoundCap
            PathAngleArc { centerX: 26; centerY: 26; radiusX: 24.5; radiusY: 24.5; startAngle: -90; sweepAngle: 360 * Math.min(1, b.shown) }
        }
    }
    GlassPlate {
        anchors.centerIn: parent
        width: 38; height: 38; radius: 19
        brand: b.active
        visible: b.active
    }
    Rectangle { anchors.centerIn: parent; width: 38; height: 38; radius: 19; color: Qt.rgba(1, 1, 1, 0.10); visible: !b.active }
    TcIcon { anchors.centerIn: parent; icon: "up"; size: 22; color: b.active ? Tc.space : Tc.inkFaint }
    MouseArea { id: mouse; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: if (b.active) b.clicked() }
    FocusRing { pad: 1 }
}
