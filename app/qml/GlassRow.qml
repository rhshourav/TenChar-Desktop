import QtQuick

// One tappable line inside a GlassCard, with a chevron and an optional smaller line under it.
Item {
    id: r
    property string text
    property string detail
    property bool caution: false
    signal clicked()
    width: parent ? parent.width : 300
    height: Math.max(56, col.implicitHeight + 16)
    activeFocusOnTab: true
    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()
    Accessible.role: Accessible.Button
    Accessible.name: text + (detail !== "" ? ", " + detail : "")
    Accessible.onPressAction: clicked()
    Rectangle { anchors.fill: parent; color: Qt.rgba(1, 1, 1, 0.07); visible: mouse.pressed }
    Column {
        id: col
        anchors { left: parent.left; right: chev.left; leftMargin: 20; rightMargin: 8; verticalCenter: parent.verticalCenter }
        Tx { width: parent.width; text: r.text; color: r.caution ? Tc.peachText : Tc.ink; Accessible.ignored: true }
        Tx { width: parent.width; visible: r.detail !== ""; kind: "labelM"; text: r.detail; color: Tc.inkMuted; elide: Text.ElideRight; maximumLineCount: 1; wrapMode: Text.NoWrap; Accessible.ignored: true }
    }
    TcIcon { id: chev; anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter } icon: "right"; size: 20; color: r.caution ? Tc.peachText : Tc.inkFaint }
    MouseArea { id: mouse; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: r.clicked() }
    FocusRing { pad: -2 }
}
