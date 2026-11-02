import QtQuick

// A plum sheet over a dimmed window. Escape closes it. Dialogs hold no secrets other than what they show.
Item {
    id: d
    property string title
    default property alias body: bodyCol.data
    property alias buttons: btnRow.data
    anchors.fill: parent
    visible: false
    z: 100
    focus: visible
    function show() { visible = true; forceActiveFocus() }
    function hide() { visible = false }
    Keys.onEscapePressed: hide()
    Accessible.role: Accessible.Dialog
    Accessible.name: title

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.55); MouseArea { anchors.fill: parent; onClicked: d.hide() } }
    Item {
        id: card
        width: Math.min(parent.width - 48, 400)
        height: col.height + 48
        anchors.centerIn: parent
        Rectangle { anchors.fill: parent; radius: 28; color: Tc.plum; border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.22) }
        MouseArea { anchors.fill: parent }
        Column {
            id: col
            x: 24; y: 24
            width: parent.width - 48
            spacing: 12
            Tx { width: parent.width; kind: "titleL"; text: d.title; Accessible.role: Accessible.Heading }
            Column { id: bodyCol; width: parent.width; spacing: 12 }
            Row { id: btnRow; anchors.right: parent.right; spacing: 4 }
        }
    }
}
