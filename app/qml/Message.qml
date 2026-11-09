import QtQuick

// A line of status or error text under a field. It is always present and only changes height, so a Column
// that contains it never has to react to something appearing.
Item {
    id: m
    property string text
    property color color: Tc.peachText
    width: parent ? parent.width : 300
    height: text === "" ? 0 : label.implicitHeight + 20
    clip: true
    Tx {
        id: label
        x: 8; y: 10
        width: parent.width - 16
        kind: "bodyM"
        text: m.text
        color: m.color
        Accessible.role: Accessible.AlertMessage
    }
}
