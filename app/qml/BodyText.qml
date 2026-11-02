import QtQuick

Tx {
    property bool center: false
    width: parent ? parent.width : implicitWidth
    kind: "bodyL"
    color: Tc.inkMuted
    horizontalAlignment: center ? Text.AlignHCenter : Text.AlignLeft
}
