import QtQuick

// First run: the choices that shape how the app talks to the world, then the passphrase that protects the keys.
// After "keys were reset" only the passphrase part is shown.
ScreenFrame {
    id: s
    function submit() { if (!ui.busy) ui.finishSetup(pass.text, confirm.text) }
    HeroBlock {
        title: ui.firstRun ? ui.strings.setup_title : ui.strings.reset_title
        body: ui.firstRun ? ui.strings.setup_body : ui.strings.reset_body
        diameter: 120
    }
    SectionTitle { text: ui.strings.setup_relays; visible: ui.firstRun }
    GlassCard {
        visible: ui.firstRun
        GlassRow { text: ui.strings.settings_relay; detail: ui.relayUrl.replace("https://", ""); onClicked: relayDialog.open("main") }
        RowDivider {}
        GlassSwitchRow { text: ui.strings.fallback_label; detail: ui.strings.fallback_detail; checked: ui.fallbackEnabled; onToggled: (v) => ui.enableFallback(v) }
        RowDivider { visible: ui.fallbackEnabled }
        GlassRow { visible: ui.fallbackEnabled; text: ui.strings.fallback_address; detail: ui.fallbackUrl.replace("https://", ""); onClicked: relayDialog.open("fallback") }
    }
    Note { visible: ui.firstRun; text: ui.strings.setup_relays_note }
    SectionTitle { text: ui.strings.settings_autolock; visible: ui.firstRun }
    AutoLockCard { visible: ui.firstRun }
    SectionTitle { text: ui.strings.setup_pass_title }
    Note { text: ui.strings.setup_pass_hint; color: Tc.inkMuted; kind: "bodyM" }
    TcField { id: pass; password: true; name: ui.strings.pass_field; placeholder: ui.strings.pass_field; invalid: ui.setupError !== ""; onAccepted: confirm.focusField() }
    Spacer { size: 12 }
    TcField { id: confirm; password: true; name: ui.strings.pass_confirm; placeholder: ui.strings.pass_confirm; invalid: ui.setupError === "mismatch"; onAccepted: s.submit() }
    Tx {
        visible: ui.setupError !== ""
        width: parent.width; topPadding: 12
        kind: "bodyM"; color: Tc.peachText
        text: ui.setupError === "short" ? ui.strings.setup_err_short : ui.setupError === "mismatch" ? ui.strings.setup_err_mismatch : ui.strings.setup_err_failed
        Accessible.role: Accessible.AlertMessage
    }
    Note { text: ui.strings.setup_pass_warning; color: Tc.peachText }
    Spacer { size: 8 }
    footer: [ TcButton { text: ui.busy ? ui.strings.setup_creating : ui.strings.setup_create; enabled: !ui.busy; onClicked: s.submit() } ]
    RelayDialog { id: relayDialog }
}
