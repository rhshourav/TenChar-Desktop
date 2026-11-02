import QtQuick

Item {
    id: c
    property real radius: 28
    property color tint: "transparent"
    default property alias content: inner.data
    width: parent ? parent.width : 300
    height: inner.height
    GlassPlate { anchors.fill: parent; radius: c.radius; tint: c.tint }
    Column { id: inner; width: parent.width }
}
