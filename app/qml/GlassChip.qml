import QtQuick

// A header action: fully round, at least 48 tall.
Item {
    id: c
    property string text
    signal clicked()
    height: 48
    width: Math.max(48, label.implicitWidth + 36)
    activeFocusOnTab: true
    scale: mouse.pressed && !ui.reducedMotion ? 0.95 : 1
    Behavior on scale { SpringAnimation { spring: 4; damping: 0.28; epsilon: 0.001 } }
    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: clicked()
    GlassPlate { anchors.fill: parent; radius: 24 }
    Text {
        id: label
        anchors.centerIn: parent
        text: c.text
        color: Tc.ink
        font.family: Tc.family
        font.pixelSize: 15
        font.weight: Font.DemiBold
        Accessible.ignored: true
    }
    MouseArea { id: mouse; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: c.clicked() }
    FocusRing {}
}
