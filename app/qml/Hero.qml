import QtQuick
import QtQuick.Window

// The mark on a frosted disc, with three accent dots orbiting it slowly (one turn in 40 seconds).
// The mark is drawn as two letters; the phone's logo image is not part of this repository.
Item {
    id: h
    property real diameter: 176
    property color halo: Tc.lavender
    width: diameter
    height: diameter
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    readonly property bool windowActive: Window.active
    Accessible.ignored: true

    Repeater {
        model: 16
        Rectangle { anchors.centerIn: parent; width: h.diameter * (0.85 + index * 0.065); height: width; radius: width / 2; color: Tc.alpha(h.halo, 0.022) }
    }
    Rectangle { anchors.centerIn: parent; width: h.diameter - 8; height: width; radius: width / 2; color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08) }
    Item {
        id: orbit
        anchors.fill: parent
        NumberAnimation on rotation { from: 0; to: 360; duration: 40000; loops: Animation.Infinite; running: !ui.reducedMotion && h.windowActive }
        Repeater {
            model: [{ a: 200, c: Tc.mint, r: 4 }, { a: 320, c: Tc.peach, r: 3 }, { a: 85, c: Tc.lavenderSoft, r: 5 }]
            GlowDot {
                readonly property real rad: (h.diameter - 8) / 2
                x: h.diameter / 2 + rad * Math.cos(modelData.a * Math.PI / 180) - width / 2
                y: h.diameter / 2 + rad * Math.sin(modelData.a * Math.PI / 180) - height / 2
                color: modelData.c
                diameter: modelData.r * 2
            }
        }
    }
    GlassPlate { anchors.centerIn: parent; width: h.diameter * 0.66; height: width; radius: width / 2 }
    Row {
        anchors.centerIn: parent
        Text { text: "T"; color: Tc.mint; font.family: Tc.family; font.weight: Font.Bold; font.pixelSize: h.diameter * 0.25 }
        Text { text: "C"; color: Tc.peach; font.family: Tc.family; font.weight: Font.Bold; font.pixelSize: h.diameter * 0.25 }
    }
}
