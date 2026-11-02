import QtQuick

Item {
    id: s
    property string text
    width: parent ? parent.width : 300
    height: label.implicitHeight + 34
    Tx {
        id: label
        x: 8; y: 24
        width: parent.width - 16
        kind: "titleM"
        text: s.text
        color: Tc.inkMuted
        Accessible.role: Accessible.Heading
    }
}
