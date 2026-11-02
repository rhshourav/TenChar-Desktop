import QtQuick

// Three accent dots rising in turn. Still when motion is reduced.
Row {
    spacing: 14
    height: 24
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    Accessible.ignored: true
    Repeater {
        model: [Tc.mint, Tc.peach, Tc.lavenderSoft]
        GlowDot {
            id: dot
            color: modelData
            diameter: 10
            y: 12
            opacity: 0.55 + 0.45 * lift
            property real lift: 0
            transform: Translate { y: -dot.lift * 10 }
            SequentialAnimation {
                running: !ui.reducedMotion
                PauseAnimation { duration: index * 160 }
                SequentialAnimation {
                    loops: Animation.Infinite
                    NumberAnimation { target: dot; property: "lift"; from: 0; to: 1; duration: 700; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: dot; property: "lift"; from: 1; to: 0; duration: 700; easing.type: Easing.InOutQuad }
                }
            }
        }
    }
}
