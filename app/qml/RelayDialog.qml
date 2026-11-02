import QtQuick

// Change the main or the backup relay. The same rules as the phone: https only, a bare origin, no look-alike hosts.
Dialog {
    id: d
    property string which: "main"   // main or fallback
    title: which === "main" ? ui.strings.relay_title : ui.strings.fallback_address
    function open(kind) {
        which = kind
        field.text = kind === "main" ? ui.relayUrl : ui.fallbackUrl
        invalid.visible = false
        show()
        field.focusField()
    }
    function save() {
        var ok = which === "main" ? ui.saveRelay(field.text) : ui.saveFallback(field.text)
        if (ok) hide(); else invalid.visible = true
    }
    Tx { width: parent.width; kind: "bodyM"; color: Tc.inkMuted; text: ui.strings.relay_body }
    TcField { id: field; name: d.title; placeholder: ui.strings.relay_hint; invalid: invalid.visible; onEdited: invalid.visible = false; onAccepted: d.save() }
    Tx { id: invalid; visible: false; width: parent.width; kind: "bodyM"; color: Tc.peachText; text: ui.strings.relay_invalid; Accessible.role: Accessible.AlertMessage }
    TextButton {
        text: ui.strings.relay_reset
        color: Tc.lavender
        onClicked: { if (d.which === "main") ui.resetRelay(); else ui.resetFallback(); d.hide() }
    }
    buttons: [
        TextButton { text: ui.strings.cancel; color: Tc.ink; onClicked: d.hide() },
        TextButton { text: ui.strings.relay_save; onClicked: d.save() }
    ]
}
