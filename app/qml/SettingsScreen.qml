import QtQuick

ScreenFrame {
    id: s
    title: ui.strings.settings_title
    showBack: true
    onBackClicked: ui.back()

    SectionTitle { text: ui.strings.settings_autolock }
    AutoLockCard {}
    Spacer { size: 24 }
    GlassCard {
        GlassRow { text: ui.strings.settings_show_number; onClicked: numberDialog.show() }
        RowDivider {}
        GlassRow { text: ui.strings.settings_relay; detail: ui.relayUrl.replace("https://", ""); onClicked: relayDialog.open("main") }
        RowDivider {}
        GlassSwitchRow { text: ui.strings.fallback_label; detail: ui.strings.fallback_detail; checked: ui.fallbackEnabled; onToggled: (v) => ui.enableFallback(v) }
        RowDivider { visible: ui.fallbackEnabled }
        GlassRow { visible: ui.fallbackEnabled; text: ui.strings.fallback_address; detail: ui.fallbackUrl.replace("https://", ""); onClicked: relayDialog.open("fallback") }
    }
    Note { text: ui.strings.relay_change_note }
    SectionTitle { text: ui.strings.settings_appearance }
    GlassCard {
        RadioRow { text: ui.strings.lang_system; selected: ui.language === "system"; onChosen: ui.setLanguage("system") }
        RowDivider {}
        RadioRow { text: ui.strings.lang_en; selected: ui.language === "en"; onChosen: ui.setLanguage("en") }
        RowDivider {}
        RadioRow { text: ui.strings.lang_bn; selected: ui.language === "bn"; onChosen: ui.setLanguage("bn") }
    }
    Spacer { size: 16 }
    GlassCard {
        GlassSwitchRow { text: ui.strings.settings_reduce_transparency; checked: ui.reducedTransparency; onToggled: (v) => ui.setReducedTransparency(v) }
        RowDivider {}
        GlassSwitchRow { text: ui.strings.settings_reduce_motion; checked: ui.reducedMotion; onToggled: (v) => ui.setReducedMotion(v) }
        RowDivider {}
        GlassSwitchRow { text: ui.strings.settings_high_contrast; checked: ui.highContrast; onToggled: (v) => ui.setHighContrast(v) }
        RowDivider {}
        GlassSwitchRow { text: ui.strings.settings_notifications; detail: ui.strings.settings_notifications_detail; checked: ui.notifications; onToggled: (v) => ui.setNotifications(v) }
    }
    Spacer { size: 16 }
    GlassCard {
        GlassRow { text: ui.strings.settings_about; onClicked: ui.open("about") }
        RowDivider {}
        GlassRow { text: ui.strings.settings_forget; caution: true; onClicked: forgetDialog.show() }
    }
    Spacer { size: 24 }

    overlays: [
    RelayDialog { id: relayDialog },
    Dialog {
        id: numberDialog
        title: ui.strings.settings_show_number
        Text {
            width: parent.width
            textFormat: Text.StyledText
            text: Tc.gradientHtml(ui.verification)
            font.family: Tc.mono; font.pixelSize: 30; font.weight: Font.Medium; font.letterSpacing: 2
            Accessible.name: ui.verification.replace(/ /g, "").split("").join(" ")
        }
        buttons: [ TextButton { text: ui.strings.dialog_close; onClicked: numberDialog.hide() } ]
    },
    Dialog {
        id: forgetDialog
        title: ui.strings.forget_confirm_title
        Tx { width: parent.width; kind: "bodyM"; color: Tc.inkMuted; text: ui.strings.forget_confirm_body }
        buttons: [
            TextButton { text: ui.strings.cancel; color: Tc.ink; onClicked: forgetDialog.hide() },
            TextButton { text: ui.strings.forget_confirm_yes; color: Tc.peachText; onClicked: { forgetDialog.hide(); ui.forgetConversation() } }
        ]
    }
    ]
}
