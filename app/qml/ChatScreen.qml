import QtQuick
import QtQuick.Window

Item {
    id: s
    readonly property real colW: Math.min(width, Tc.contentMax) - 48

    HeaderBar {
        id: bar
        title: ui.strings.chat_title
        actions: [
            GlassChip { text: ui.strings.chat_lock; onClicked: ui.lock() },
            GlassChip { text: ui.strings.chat_settings; onClicked: ui.open("settings") }
        ]
    }

    Item {
        id: body
        anchors { top: bar.bottom; bottom: parent.bottom; horizontalCenter: parent.horizontalCenter }
        width: s.colW

        // "Offline" or "the relays no longer have this conversation": shown only when needed.
        GlassCard {
            id: banner
            visible: ui.connection !== "online"
            height: visible ? bannerRow.height : 0
            radius: 18
            tint: Tc.alpha(Tc.peach, 0.14)
            Accessible.role: Accessible.AlertMessage
            Accessible.name: bannerText.text
            Item {
                id: bannerRow
                width: parent.width; height: bannerText.implicitHeight + 24
                GlowDot { x: 16; anchors.verticalCenter: parent.verticalCenter; color: Tc.peach; diameter: 7 }
                Tx { id: bannerText; x: 36; y: 12; width: parent.width - 52; kind: "labelM"
                     text: ui.connection === "gone" ? ui.strings.chat_gone : ui.strings.chat_offline; Accessible.ignored: true }
            }
        }

        Item {   // empty state
            visible: rep.count === 0
            anchors { top: banner.bottom; bottom: composer.top; left: parent.left; right: parent.right }
            Item {
                width: parent.width; height: parent.height
                Column {
                    anchors.centerIn: parent
                    width: parent.width
                    spacing: 20
                    Hero { diameter: 112 }
                    BodyText { text: ui.strings.chat_empty; center: true }
                }
            }
        }

        // A Flickable with a Column and Repeater rather than a ListView: items are created strictly in conversation
        // order, which is the order a screen reader walks them in (a ListView creates delegates out of order).
        Flickable {
            id: list
            anchors { top: banner.bottom; bottom: composer.top; left: parent.left; right: parent.right }
            clip: true
            contentWidth: width
            contentHeight: msgs.height + 24
            boundsBehavior: Flickable.StopAtBounds
            property bool ready: false
            property bool stick: true
            function toEnd() { contentY = Math.max(0, contentHeight - height) }
            Timer { running: true; interval: 150; onTriggered: list.ready = true }
            onContentHeightChanged: if (stick) Qt.callLater(toEnd)
            onHeightChanged: if (stick) Qt.callLater(toEnd)
            onMovementEnded: stick = contentY >= contentHeight - height - 2
            onFlickEnded: stick = contentY >= contentHeight - height - 2
            Accessible.role: Accessible.List
            Accessible.name: ui.strings.chat_title
            Column {
                id: msgs
                y: 12
                width: list.width
                spacing: 8
                Repeater {
                    id: rep
                    model: ui.messages
                    onItemAdded: (index, item) => { if (list.ready && !ui.reducedMotion) item.play() }
                    delegate: Item {
                        id: row
                        width: msgs.width
                        height: bubble.height + (status.visible ? status.height + 4 : 0)
                        readonly property bool mine: model.mine
                        property real appear: 1
                        function play() { appear = 0; grow.start() }
                        transformOrigin: mine ? Item.BottomRight : Item.BottomLeft
                        opacity: Math.min(1, appear * 1.8)
                        scale: 0.86 + 0.14 * appear
                        transform: Translate { y: (1 - row.appear) * 14 }
                        NumberAnimation { id: grow; target: row; property: "appear"; from: 0; to: 1; duration: 320; easing.type: Easing.OutBack; easing.overshoot: 1.2 }
                        readonly property string deliveryText: model.delivery === "queued" ? ui.strings.delivery_queued : model.delivery === "sent" ? ui.strings.delivery_sent : ""
                        Accessible.role: Accessible.StaticText
                        Accessible.name: (mine ? ui.strings.msg_from_you : ui.strings.msg_from_them).replace("%1", model.text) + (deliveryText !== "" ? ", " + deliveryText : "")

                        Text { id: probe; visible: false; wrapMode: Text.NoWrap; font.family: Tc.family; font.pixelSize: 22; font.weight: Font.Medium; text: model.text }
                        Item {
                            id: bubble
                            width: Math.min(300, Math.ceil(probe.implicitWidth) + 40)
                            height: msg.implicitHeight + 24
                            x: row.mine ? row.width - width : 0
                            GlassPlate {
                                anchors.fill: parent
                                rTL: 22; rTR: 22; rBL: row.mine ? 22 : 6; rBR: row.mine ? 6 : 22
                                tint: row.mine ? Tc.alpha(Tc.indigo, 0.70) : Tc.alpha(Tc.peach, 0.07)
                                tintB: row.mine ? Tc.alpha(Tc.lavender, 0.34) : "transparent"
                            }
                            Tx { id: msg; x: 18; y: 12; width: parent.width - 36; kind: "titleL"; font.weight: Font.Medium; text: model.text; Accessible.ignored: true }
                        }
                        Item {
                            id: status
                            visible: row.deliveryText !== ""
                            anchors { top: bubble.bottom; topMargin: 4 }
                            x: row.mine ? row.width - width - 6 : 6
                            height: 17
                            width: 14 + statusText.implicitWidth
                            GlowDot { anchors.verticalCenter: parent.verticalCenter; diameter: 6; color: model.delivery === "sent" ? Tc.mint : Tc.peach }
                            Tx { id: statusText; x: 14; kind: "labelS"; text: row.deliveryText; color: Tc.inkMuted; wrapMode: Text.NoWrap; Accessible.ignored: true }
                        }
                    }
                }
            }
        }

        // The composer: glass, with a live counter and the ring button.
        Item {
            id: composer
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: 12 }
            height: 64
            property bool byteHit: false
            transform: Translate { id: shake; x: 0 }
            SequentialAnimation {
                id: shakeAnim
                NumberAnimation { target: shake; property: "x"; to: -9; duration: 50 }
                NumberAnimation { target: shake; property: "x"; to: 7; duration: 60 }
                NumberAnimation { target: shake; property: "x"; to: -4; duration: 70 }
                NumberAnimation { target: shake; property: "x"; to: 2; duration: 60 }
                NumberAnimation { target: shake; property: "x"; to: 0; duration: 60 }
            }
            readonly property int count: ui.graphemeCount(input.text)
            readonly property int bytes: ui.utf8Length(input.text)
            readonly property bool atLimit: count >= ui.maxChars || byteHit
            function send() {
                if (input.text.trim() === "") return
                ui.send(input.text)
                input.text = ""
                byteHit = false
            }
            GlassPlate { anchors.fill: parent; radius: 34 }
            TextInput {
                id: input
                anchors { left: parent.left; leftMargin: 22; right: counter.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
                color: Tc.ink
                font.family: Tc.family
                font.pixelSize: 22
                clip: true
                selectionColor: Tc.alpha(Tc.mint, 0.30)
                selectedTextColor: Tc.ink
                cursorDelegate: Rectangle { width: 2; color: Tc.mint }
                inputMethodHints: Qt.ImhNoPredictiveText
                activeFocusOnTab: true
                Component.onCompleted: forceActiveFocus()
                Keys.onEscapePressed: { text = ""; composer.byteHit = false }
                onAccepted: composer.send()
                onTextEdited: {
                    ui.activity()
                    var clamped = ui.clamp(text)
                    if (clamped !== text) {
                        // Over the limit: the text is cut, so say so with a nudge. If the byte limit was the cause, say that.
                        composer.byteHit = ui.graphemeCount(text) <= ui.maxChars
                        text = clamped
                        cursorPosition = text.length
                        if (!ui.reducedMotion) shakeAnim.restart()
                    } else {
                        composer.byteHit = false
                    }
                }
                Accessible.role: Accessible.EditableText
                Accessible.name: ui.strings.chat_input_hint
            }
            Tx {
                anchors { left: input.left; right: input.right; verticalCenter: parent.verticalCenter }
                visible: input.text.length === 0 && input.preeditText.length === 0
                text: ui.strings.chat_input_hint
                kind: "bodyL"
                wrapMode: Text.NoWrap
                color: Tc.inkMuted
                Accessible.ignored: true
            }
            Tx {
                id: counter
                anchors { right: send.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
                kind: "labelM"
                wrapMode: Text.NoWrap
                text: ui.strings.chat_counter.replace("%1", composer.count).replace("%2", ui.maxChars)
                color: composer.atLimit ? Tc.peachText : Tc.inkMuted
            }
            SendButton {
                id: send
                anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                name: ui.strings.chat_send
                active: input.text.trim() !== ""
                fraction: Math.max(composer.count / ui.maxChars, composer.bytes / ui.maxBytes)
                onClicked: composer.send()
            }
            Tx {   // the byte limit bound before the character limit (Bangla letters take more bytes)
                visible: composer.byteHit
                anchors { top: parent.top; topMargin: -22; left: parent.left; leftMargin: 12 }
                kind: "labelM"; color: Tc.peachText; wrapMode: Text.NoWrap
                text: ui.strings.chat_limit_bytes
                Accessible.role: Accessible.AlertMessage
            }
        }
    }
}
