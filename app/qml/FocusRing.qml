import QtQuick

// The visible mint focus ring every keyboard-reachable control shows.
Rectangle {
    property Item target: parent
    property real pad: 3
    anchors.fill: target
    anchors.margins: -pad
    color: "transparent"
    border.color: Tc.mint
    border.width: 2
    visible: target.activeFocus
    z: 50
}
