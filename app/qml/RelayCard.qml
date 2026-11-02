import QtQuick

// Shows which relay will be used and lets the person change it. It sits on the pairing screens because both
// devices must share a relay to find each other.
GlassCard {
    id: c
    radius: 22
    signal editRequested()
    GlassRow { text: ui.strings.settings_relay; detail: ui.relayUrl.replace("https://", ""); onClicked: c.editRequested() }
}
