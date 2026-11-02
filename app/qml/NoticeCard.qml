import QtQuick

// A peach (warning) or lavender (information) glass card with a glowing dot and a sentence.
GlassCard {
    id: n
    property string text
    property bool warning: true
    radius: 22
    tint: Tc.alpha(warning ? Tc.peach : Tc.lavender, 0.14)
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text
    Item {
        width: parent.width
        height: msg.implicitHeight + 36
        GlowDot { x: 18; y: 21; color: n.warning ? Tc.peach : Tc.lavender; diameter: 8 }
        Tx { id: msg; x: 40; y: 18; width: parent.width - 58; kind: "bodyM"; text: n.text; Accessible.ignored: true }
    }
}
