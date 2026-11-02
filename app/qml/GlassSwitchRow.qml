import QtQuick

// One toggle line inside a GlassCard. The whole row is the target.
Item {
    id: r
    property string text
    property string detail
    property bool checked: false
    signal toggled(bool value)
    width: parent ? parent.width : 300
    height: Math.max(56, col.implicitHeight + 16)
    activeFocusOnTab: true
    Keys.onReturnPressed: toggled(!checked)
    Keys.onSpacePressed: toggled(!checked)
    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.onToggleAction: toggled(!checked)
    Column {
        id: col
        anchors { left: parent.left; right: track.left; leftMargin: 20; rightMargin: 12; verticalCenter: parent.verticalCenter }
        Tx { width: parent.width; text: r.text; Accessible.ignored: true }
        Tx { width: parent.width; visible: r.detail !== ""; kind: "labelM"; text: r.detail; color: Tc.inkMuted; Accessible.ignored: true }
    }
    Rectangle {
        id: track
        anchors { right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
        width: 52; height: 30; radius: 15
        color: r.checked ? Tc.mint : Qt.rgba(1, 1, 1, 0.12)
        border.width: r.checked ? 0 : 2
        border.color: Qt.rgba(1, 1, 1, 0.35)
        Behavior on color { ColorAnimation { duration: ui.reducedMotion ? 0 : 120 } }
        Rectangle {
            width: 22; height: 22; radius: 11; y: 4
            x: r.checked ? parent.width - width - 4 : 4
            color: r.checked ? Tc.space : Tc.inkMuted
            Behavior on x { NumberAnimation { duration: ui.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic } }
        }
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: r.toggled(!r.checked) }
    FocusRing { pad: -2 }
}
