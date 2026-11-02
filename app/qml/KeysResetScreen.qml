import QtQuick

ScreenFrame {
    centered: true
    HeroBlock { title: ui.strings.keys_reset_title; body: ui.strings.keys_reset_body; halo: Tc.peach }
    footer: [ TcButton { text: ui.strings.keys_reset_pair; onClicked: ui.pairAgain() } ]
}
