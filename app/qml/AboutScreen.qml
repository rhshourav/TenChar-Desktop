import QtQuick

ScreenFrame {
    title: ui.strings.about_title
    showBack: true
    onBackClicked: ui.back()
    Spacer { size: 8 }
    GlassCard {
        Column {
            x: 20; width: parent.width - 40
            topPadding: 20; bottomPadding: 20; spacing: 12
            Tx { width: parent.width; kind: "titleM"; text: ui.strings.about_intro }
            BodyText { text: ui.strings.about_status }
            BodyText { text: ui.strings.about_review; color: Tc.peachText }
        }
    }
    SectionTitle { text: ui.strings.about_not_title }
    GlassCard {
        Column {
            x: 20; width: parent.width - 40
            topPadding: 12; bottomPadding: 12
            Repeater {
                model: [ui.strings.about_not_1, ui.strings.about_not_2, ui.strings.about_not_3, ui.strings.about_not_4,
                        ui.strings.about_not_5, ui.strings.about_not_6, ui.strings.about_not_7]
                Item {
                    width: parent.width
                    height: t.implicitHeight + 16
                    GlowDot { x: 0; y: 17; color: Tc.peach; diameter: 7 }
                    Tx { id: t; x: 24; y: 8; width: parent.width - 24; kind: "bodyM"; text: modelData }
                }
            }
        }
    }
    Spacer { size: 24 }
}
