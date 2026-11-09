import QtQuick

ScreenFrame {
    id: s
    title: ui.strings.enter_title
    showBack: true
    onBackClicked: ui.back()
    readonly property int symbols: ui.entry.replace(/-/g, "").length
    property bool focusedOnce: false
    readonly property bool valid: symbols === 16
    readonly property bool showError: !field.input.activeFocus && symbols > 0 && symbols < 16
    Component.onCompleted: field.focusField()
    function join() { if (valid) ui.join() }

    Spacer { size: 16 }
    TcField {
        id: field
        entry: true
        name: ui.strings.enter_title
        placeholder: ui.strings.enter_hint
        invalid: s.showError
        // Format as it is typed. Letters I, O, 0 and 1 are dropped.
        onEdited: { var f = ui.formatCode(text); if (f !== text) { text = f; input.cursorPosition = f.length } ui.onEntry(f) }
        onAccepted: s.join()
    }
    Row {
        id: dots
        anchors.horizontalCenter: parent.horizontalCenter
        topPadding: 24
        spacing: 16
        Accessible.ignored: true
        Repeater {
            model: 4
            Row {
                spacing: 6
                readonly property int g: index
                Repeater {
                    model: 4
                    GlowDot { diameter: 10; color: Tc.mint; lit: (parent.g * 4 + index) < s.symbols ? 1 : 0; Behavior on lit { NumberAnimation { duration: ui.reducedMotion ? 0 : 160; easing.type: Easing.OutBack } } }
                }
            }
        }
    }
    Spacer { size: 24 }
    RelayCard { onEditRequested: relayDialog.open("main") }
    Message { text: s.showError ? ui.strings.enter_invalid : "" }
    footer: [ TcButton { text: ui.strings.enter_join; enabled: s.valid; onClicked: s.join() } ]
    overlays: [ RelayDialog { id: relayDialog } ]
}
