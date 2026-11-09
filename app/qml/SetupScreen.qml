import QtQuick

// First run: choose a passphrase (the first thing on the screen, so it never needs scrolling), then the relay and
// auto-lock choices below it. After "keys were reset" only the passphrase part is shown.
ScreenFrame {
    id: s
    function submit() { if (!ui.busy) ui.finishSetup(pass.text, confirm.text) }
    Component.onCompleted: pass.focusField()

    HeroBlock {
        title: ui.firstRun ? ui.strings.setup_title : ui.strings.reset_title
        body: ui.firstRun ? ui.strings.setup_body : ui.strings.reset_body
        diameter: 96
    }
    SectionTitle { text: ui.strings.setup_pass_title }
    Note { text: ui.strings.setup_pass_hint; color: Tc.inkMuted; kind: "bodyM" }
    TcField { id: pass; password: true; name: ui.strings.pass_field; placeholder: ui.strings.pass_field; invalid: ui.setupError === "short" || ui.setupError === "failed"; onAccepted: confirm.focusField() }
    Spacer { size: 12 }
    TcField { id: confirm; password: true; name: ui.strings.pass_confirm; placeholder: ui.strings.pass_confirm; invalid: ui.setupError === "mismatch"; onAccepted: s.submit() }
    Message { text: ui.setupError === "short" ? ui.strings.setup_err_short : ui.setupError === "mismatch" ? ui.strings.setup_err_mismatch : ui.setupError === "failed" ? ui.strings.setup_err_failed : "" }
    Note { text: ui.strings.setup_pass_warning; color: Tc.peachText }

    SectionTitle { visible: ui.firstRun; text: ui.strings.setup_relays }
    GlassCard {
        visible: ui.firstRun
        GlassRow { text: ui.strings.settings_relay; detail: ui.relayUrl.replace("https://", ""); onClicked: relayDialog.open("main") }
        RowDivider {}
        GlassSwitchRow { text: ui.strings.fallback_label; detail: ui.strings.fallback_detail; checked: ui.fallbackEnabled; onToggled: (v) => ui.enableFallback(v) }
        RowDivider { visible: ui.fallbackEnabled }
        GlassRow { visible: ui.fallbackEnabled; text: ui.strings.fallback_address; detail: ui.fallbackUrl.replace("https://", ""); onClicked: relayDialog.open("fallback") }
    }
    Note { visible: ui.firstRun; text: ui.strings.setup_relays_note }
    SectionTitle { visible: ui.firstRun; text: ui.strings.settings_autolock }
    AutoLockCard { visible: ui.firstRun }
    Spacer { size: 16 }

    footer: [ TcButton { text: ui.busy ? ui.strings.setup_creating : ui.strings.setup_create; enabled: !ui.busy; onClicked: s.submit() } ]
    overlays: [ RelayDialog { id: relayDialog } ]
}
