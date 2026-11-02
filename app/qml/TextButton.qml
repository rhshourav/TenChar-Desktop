import QtQuick

Item {
    id: t
    property string text
    property color color: Tc.mint
    signal clicked()
    height: 48
    width: label.implicitWidth + 32
    activeFocusOnTab: true
    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: clicked()
    Text { id: label; anchors.centerIn: parent; text: t.text; color: t.color; font.family: Tc.family; font.pixelSize: 16; font.weight: Font.DemiBold; Accessible.ignored: true }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: t.clicked() }
    FocusRing {}
}
