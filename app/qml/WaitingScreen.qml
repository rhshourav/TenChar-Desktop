import QtQuick

ScreenFrame {
    id: s
    title: ui.joining ? ui.strings.joining_title : ui.strings.waiting_title
    showBack: true
    onBackClicked: ui.cancelWaiting()
    property bool copied: false
    Timer { id: reset; interval: 2000; onTriggered: s.copied = false }
    readonly property var groups: ui.code === "" ? [] : ui.code.split("-")

    Spacer { size: 8 }
    BodyText { text: ui.joining ? ui.strings.joining_body : ui.strings.waiting_body }
    Spacer { size: 24 }
    CodeBlock {
        visible: ui.code !== ""
        lines: s.groups.length === 4 ? [s.groups[0] + "  " + s.groups[1], s.groups[2] + "  " + s.groups[3]] : []
        spoken: ui.code.replace(/-/g, "").split("").join(" ")
    }
    Spacer { size: 28 }
    WaitingDots {}
    footer: [
        TcButton {
            visible: ui.code !== ""
            kind: "glass"
            text: s.copied ? ui.strings.waiting_copied : ui.strings.waiting_copy
            onClicked: { ui.copyCode(); s.copied = true; reset.restart() }
        },
        TcButton { text: ui.strings.cancel; kind: "glass"; onClicked: ui.cancelWaiting() }
    ]
}
