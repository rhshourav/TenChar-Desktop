import QtQuick

// A text field on a glass plate.
Item {
    id: f
    property alias text: input.text
    property alias input: input
    property string placeholder
    property string name
    property bool password: false
    property bool entry: false      // monospace, for pairing codes
    property bool invalid: false
    property int maximumLength: 32767
    signal accepted()
    signal edited()
    width: parent ? parent.width : 300
    height: 56
    function focusField() { input.forceActiveFocus() }
    GlassPlate { anchors.fill: parent; radius: 24; tint: f.invalid ? Tc.alpha(Tc.peach, 0.14) : "transparent" }
    TextInput {
        id: input
        anchors { left: parent.left; right: parent.right; leftMargin: 20; rightMargin: 20; verticalCenter: parent.verticalCenter }
        color: Tc.ink
        font.family: f.entry ? Tc.mono : Tc.family
        font.pixelSize: f.entry ? 20 : 16
        font.letterSpacing: f.entry ? 1 : 0
        selectionColor: Tc.alpha(Tc.mint, 0.30)
        selectedTextColor: Tc.ink
        echoMode: f.password ? TextInput.Password : TextInput.Normal
        passwordCharacter: "•"
        maximumLength: f.maximumLength
        clip: true
        inputMethodHints: f.password ? (Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase)
                                     : f.entry ? (Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhPreferUppercase | Qt.ImhSensitiveData)
                                               : Qt.ImhNone
        cursorDelegate: Rectangle { width: 2; color: Tc.mint }
        onAccepted: f.accepted()
        onTextEdited: { f.edited(); ui.activity() }
        activeFocusOnTab: true
        Accessible.role: Accessible.EditableText
        Accessible.name: f.name !== "" ? f.name : f.placeholder
    }
    Tx {
        anchors { left: input.left; right: input.right; verticalCenter: parent.verticalCenter }
        visible: input.text.length === 0 && input.preeditText.length === 0
        text: f.placeholder
        wrapMode: Text.NoWrap
        elide: Text.ElideRight
        color: Tc.inkMuted
        font.family: f.entry ? Tc.mono : Tc.family
        font.pixelSize: f.entry ? 20 : 16
        Accessible.ignored: true
    }
    Rectangle { anchors.fill: parent; anchors.margins: -3; radius: 27; color: "transparent"; border.color: Tc.mint; border.width: 2; visible: input.activeFocus }
}
