import QtQuick

// A fully round button. Primary (brand gradient with a soft lavender glow), glass, or caution (peach tint).
Item {
    id: b
    property string text
    property string kind: "primary"   // primary, glass, caution
    signal clicked()
    width: parent ? parent.width : 240
    height: Math.max(56, label.implicitHeight + 28)
    opacity: enabled ? 1 : 0.4
    activeFocusOnTab: true
    scale: mouse.pressed && enabled && !ui.reducedMotion ? 0.95 : 1
    Behavior on scale { SpringAnimation { spring: 4; damping: 0.28; epsilon: 0.001 } }
    Keys.onReturnPressed: if (enabled) clicked()
    Keys.onEnterPressed: if (enabled) clicked()
    Keys.onSpacePressed: if (enabled) clicked()
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: if (enabled) clicked()

    Repeater {   // the soft lavender glow under the primary button
        model: b.kind === "primary" && !ui.reducedTransparency ? 6 : 0
        Rectangle {
            anchors.fill: parent
            anchors.margins: -(index + 1) * 3
            radius: height / 2
            color: Tc.alpha(Tc.lavender, 0.05)
        }
    }
    GlassPlate {
        anchors.fill: parent
        radius: height / 2
        brand: b.kind === "primary"
        tint: b.kind === "caution" ? Tc.alpha(Tc.peach, 0.14) : "transparent"
    }
    Tx {
        id: label
        anchors.centerIn: parent
        width: parent.width - 48
        text: b.text
        kind: "labelL"
        horizontalAlignment: Text.AlignHCenter
        color: b.kind === "primary" ? Tc.space : b.kind === "caution" ? Tc.peachText : Tc.ink
        Accessible.ignored: true
    }
    MouseArea { id: mouse; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: if (b.enabled) b.clicked() }
    FocusRing {}
}
