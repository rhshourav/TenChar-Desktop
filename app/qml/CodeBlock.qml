import QtQuick

// A code or number in large gradient monospace, read out character by character.
GlassCard {
    id: c
    property var lines: []
    property string spoken
    radius: 28
    Accessible.role: Accessible.StaticText
    Accessible.name: spoken
    Column {
        width: parent.width
        topPadding: 28
        bottomPadding: 28
        spacing: 6
        Repeater {
            model: c.lines
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                textFormat: Text.StyledText
                text: Tc.gradientHtml(modelData)
                font.family: Tc.mono
                font.pixelSize: 30
                font.weight: Font.Medium
                font.letterSpacing: 2
                Accessible.ignored: true
            }
        }
    }
}
