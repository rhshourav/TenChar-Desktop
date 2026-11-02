import QtQuick

ScreenFrame {
    id: s
    centered: true
    function submit() { if (!ui.busy && pass.text.length > 0) ui.unlock(pass.text) }
    Component.onCompleted: pass.focusField()
    HeroBlock { title: ui.strings.gate_title; body: ui.strings.gate_body }
    Spacer { size: 28 }
    TcField { id: pass; password: true; name: ui.strings.pass_field; placeholder: ui.strings.pass_field; invalid: ui.unlockFailed; onAccepted: s.submit(); onEdited: ui.activity() }
    Tx {
        visible: ui.unlockFailed
        width: parent.width
        topPadding: 16
        kind: "bodyM"
        color: Tc.peachText
        horizontalAlignment: Text.AlignHCenter
        text: ui.strings.gate_failed
        Accessible.role: Accessible.AlertMessage
    }
    footer: [ TcButton { text: ui.busy ? ui.strings.gate_unlocking : ui.strings.gate_unlock; enabled: !ui.busy; onClicked: s.submit() } ]
}
