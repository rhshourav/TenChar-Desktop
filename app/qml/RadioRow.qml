import QtQuick

Item {
    id: r
    property string text
    property bool selected: false
    signal chosen()
    width: parent ? parent.width : 300
    height: 56
    activeFocusOnTab: true
    Keys.onReturnPressed: chosen()
    Keys.onSpacePressed: chosen()
    Accessible.role: Accessible.RadioButton
    Accessible.name: text
    Accessible.checkable: true
    Accessible.checked: selected
    Accessible.onPressAction: chosen()
    Tx { anchors { left: parent.left; right: dot.left; leftMargin: 20; rightMargin: 12; verticalCenter: parent.verticalCenter } text: r.text; Accessible.ignored: true }
    Item {
        id: dot
        anchors { right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
        width: 24; height: 24
        Rectangle { anchors.fill: parent; radius: 12; color: "transparent"; border.width: 2; border.color: r.selected ? Tc.mint : Qt.rgba(1, 1, 1, 0.35) }
        Rectangle {
            anchors.centerIn: parent
            width: r.selected ? 12 : 0; height: width; radius: width / 2; color: Tc.mint
            Behavior on width { NumberAnimation { duration: ui.reducedMotion ? 0 : 140; easing.type: Easing.OutBack } }
        }
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: r.chosen() }
    FocusRing { pad: -2 }
}
