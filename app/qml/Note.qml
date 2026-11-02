import QtQuick

Item {
    id: n
    property string text
    property color color: Tc.inkMuted
    property string kind: "labelM"
    width: parent ? parent.width : 300
    height: label.implicitHeight + 20
    Tx { id: label; x: 8; y: 10; width: parent.width - 16; kind: n.kind; text: n.text; color: n.color }
}
