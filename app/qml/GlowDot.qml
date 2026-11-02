import QtQuick

// A small glowing dot: bullets, status, progress.
Item {
    id: d
    property color color: Tc.mint
    property real diameter: 8
    property real lit: 1
    width: diameter
    height: diameter
    Accessible.ignored: true
    Rectangle { anchors.centerIn: parent; width: d.diameter * 2.8; height: width; radius: width / 2; color: Tc.alpha(d.color, 0.10 * d.lit) }
    Rectangle { anchors.centerIn: parent; width: d.diameter * 1.9; height: width; radius: width / 2; color: Tc.alpha(d.color, 0.20 * d.lit) }
    Rectangle { anchors.fill: parent; radius: width / 2; color: d.color; opacity: 0.25 + 0.75 * d.lit }
}
