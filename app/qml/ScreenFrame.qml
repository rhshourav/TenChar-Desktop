import QtQuick
import QtQuick.Window

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
    /// Dialogs go here, NOT in the default content: that is a Column, and an item that fills its parent breaks it.
    property alias overlays: overlayLayer.data
    signal backClicked()
    readonly property real colW: Math.min(width, Tc.contentMax)

    // Keep the focused field in view (Tab, or a field below the fold).
    Connections {
        target: f.Window.window
        function onActiveFocusItemChanged() { f.reveal(f.Window.window.activeFocusItem) }
    }
    function reveal(item) {
        if (!item) return
        // wait until the layout has settled, otherwise a field focused at start-up scrolls the page away
        Qt.callLater(function() { f.doReveal(item) })
    }
    function doReveal(item) {
        if (!item || flick.height < 50 || flick.contentHeight <= flick.height + 1) return
        var p = item, inside = false
        while (p) { if (p === col) { inside = true; break } p = p.parent }
        if (!inside) return
        var pt = item.mapToItem(flick.contentItem, 0, 0)
        var top = pt.y - 16, bottom = pt.y + item.height + 16
        if (top < flick.contentY) flick.contentY = Math.max(0, top)
        else if (bottom > flick.contentY + flick.height) flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, bottom - flick.height))
    }

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
    Rectangle {   // scroll indicator: shows that there is more below
        parent: f
        visible: flick.contentHeight > flick.height + 1
        x: f.width - width - 4
        y: flick.y + 4 + (flick.height - 8) * flick.visibleArea.yPosition
        width: 3; radius: 1.5
        height: Math.max(24, (flick.height - 8) * flick.visibleArea.heightRatio)
        color: Qt.rgba(1, 1, 1, 0.35)
        z: 5
        Accessible.ignored: true
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
    Item { id: overlayLayer; anchors.fill: parent; z: 100 }
}
