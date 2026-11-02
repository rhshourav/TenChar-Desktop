import QtQuick

Tx {
    property bool center: false
    width: parent ? parent.width : implicitWidth
    kind: "headline"
    horizontalAlignment: center ? Text.AlignHCenter : Text.AlignLeft
    Accessible.role: Accessible.Heading
}
