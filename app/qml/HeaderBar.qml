import QtQuick

// The glass-free screen header: optional back button, a title, and actions on the right.
Item {
    id: h
    property string title
    property bool showBack: false
    property alias actions: actionRow.data
    signal backClicked()
    readonly property real colW: Math.min(parent ? parent.width : 560, Tc.contentMax)
    width: colW
    height: 68
    x: ((parent ? parent.width : colW) - colW) / 2
    IconButton {
        id: back
        visible: h.showBack
        anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
        icon: "left"
        name: ui.strings.back
        onClicked: h.backClicked()
    }
    Tx {
        anchors {
            left: back.visible ? back.right : parent.left; leftMargin: back.visible ? 12 : 24
            right: actionRow.left; rightMargin: 8; verticalCenter: parent.verticalCenter
        }
        text: h.title
        kind: "titleL"
        wrapMode: Text.NoWrap
        elide: Text.ElideRight
        Accessible.role: Accessible.Heading
    }
    Row { id: actionRow; anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter } spacing: 8 }
}
