import QtQuick

Item {
    width: parent ? parent.width : 300
    height: 1
    Accessible.ignored: true
    Rectangle { x: 20; width: parent.width - 40; height: 1; color: Qt.rgba(1, 1, 1, 0.10) }
}
