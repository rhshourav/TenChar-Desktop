import QtQuick

ScreenFrame {
    centered: true
    readonly property string noticeText: ui.notice === "mismatch" ? ui.strings.notice_mismatch
        : ui.notice === "forgotten" ? ui.strings.notice_forgotten : ui.notice === "timeout" ? ui.strings.notice_timeout
        : ui.notice === "unreachable" ? ui.strings.notice_unreachable : ui.notice === "tampered" ? ui.strings.notice_tampered
        : ui.notice === "full" ? ui.strings.notice_full : ui.notice === "storage" ? ui.strings.notice_storage : ""
    NoticeCard { visible: ui.notice !== ""; text: noticeText; warning: ui.notice !== "forgotten" }
    Spacer { size: 28; visible: ui.notice !== "" }
    HeroBlock { title: ui.strings.pair_title; body: ui.strings.pair_body; diameter: 132 }
    Spacer { size: 28 }
    RelayCard { onEditRequested: relayDialog.open("main") }
    footer: [
        TcButton { text: ui.strings.pair_make; onClicked: ui.startWaiting() },
        TcButton { text: ui.strings.pair_have; kind: "glass"; onClicked: ui.openEnterCode() }
    ]
    RelayDialog { id: relayDialog }
}
