import QtQuick

ScreenFrame {
    title: ui.strings.verify_title
    Spacer { size: 8 }
    BodyText { text: ui.strings.verify_body }
    Spacer { size: 24 }
    CodeBlock { lines: [ui.verification]; spoken: ui.verification.replace(/ /g, "").split("").join(" ") }
    footer: [
        TcButton { text: ui.strings.verify_match; onClicked: ui.verifyMatches() },
        TcButton { text: ui.strings.verify_mismatch; kind: "caution"; onClicked: ui.verifyMismatch() }
    ]
}
