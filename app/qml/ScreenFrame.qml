import QtQuick

// The common frame. It draws no background: the aurora lives once at the window root. Primary actions go in
// `bottom`, which stays within reach at the foot of the window.
Item {
    id: f
    property string title: ""
    property bool showBack: false
    property bool centered: false
    property alias actions: bar.actions
    default property alias content: col.data
    property alias footer: bottomCol.data
    signal backClicked()
    readonly property real colW: Math.min(width, Tc.contentMax)

    HeaderBar { id: bar; visible: f.title !== ""; height: visible ? 68 : 0; title: f.title; showBack: f.showBack; onBackClicked: f.backClicked() }
    Flickable {
        id: flick
        anchors { top: bar.bottom; left: parent.left; right: parent.right; bottom: bottomBox.top }
        contentWidth: width
        contentHeight: Math.max(height, col.height + 16)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        Column {
            id: col
            width: f.colW - 48
            x: (flick.width - width) / 2
            y: f.centered ? Math.max(8, (flick.height - height) / 2) : 8
        }
    }
    Item {
        id: bottomBox
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: bottomCol.height > 0 ? bottomCol.height + 24 : 0
        Column {
            id: bottomCol
            width: f.colW - 48
            x: (parent.width - width) / 2
            y: 8
            spacing: 12
        }
    }
}
