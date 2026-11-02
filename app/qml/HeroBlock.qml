import QtQuick

// Logo, headline and one paragraph, centred. Used by the full-screen states.
Column {
    id: b
    property string title
    property string body
    property real diameter: 176
    property color halo: Tc.lavender
    width: parent ? parent.width : 300
    spacing: 0
    Hero { diameter: b.diameter; halo: b.halo }
    Spacer { size: 28 }
    Headline { text: b.title; center: true }
    Spacer { size: 12 }
    BodyText { text: b.body; center: true; visible: b.body !== "" }
}
