import QtQuick

Item {
    id: b
    property string icon: "left"
    property string name
    signal clicked()
    width: 48
    height: 48
    activeFocusOnTab: true
    scale: mouse.pressed && !ui.reducedMotion ? 0.95 : 1
    Behavior on scale { SpringAnimation { spring: 4; damping: 0.28; epsilon: 0.001 } }
    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()
    Accessible.role: Accessible.Button
    Accessible.name: name
    Accessible.onPressAction: clicked()
    GlassPlate { anchors.fill: parent; radius: 24 }
    TcIcon { anchors.centerIn: parent; icon: b.icon; size: 22 }
    MouseArea { id: mouse; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: b.clicked() }
    FocusRing {}
}
