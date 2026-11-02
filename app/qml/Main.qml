import QtQuick
import QtQuick.Window

Window {
    id: win
    visible: true
    width: 420
    height: 640
    minimumWidth: 360
    minimumHeight: 520
    color: Tc.space
    title: "TenChar"

    // Closing the window locks the app and hides it to the tray; Quit in the tray menu ends everything.
    onClosing: (close) => { close.accepted = false; ui.hideToTray() }
    Connections { target: ui; function onShowRequested() { win.show(); win.raise(); win.requestActivate() } }

    Aurora { anchors.fill: parent; active: win.active }

    readonly property var sources: ({
        setup: "SetupScreen.qml", unlock: "UnlockScreen.qml", pair: "PairScreen.qml", entercode: "EnterCodeScreen.qml",
        waiting: "WaitingScreen.qml", verify: "VerifyScreen.qml", chat: "ChatScreen.qml", settings: "SettingsScreen.qml",
        about: "AboutScreen.qml", keysreset: "KeysResetScreen.qml"
    })
    property real enter: 1
    Connections {
        target: ui
        function onChanged() {
            if (loader.screenName !== ui.screen) {
                loader.screenName = ui.screen
                // Locking is never animated: nothing may linger on screen.
                if (ui.screen === "unlock" || ui.reducedMotion) win.enter = 1
                else { win.enter = 0; enterAnim.restart() }
            }
        }
    }
    NumberAnimation { id: enterAnim; target: win; property: "enter"; from: 0; to: 1; duration: 320; easing.type: Easing.OutCubic }

    Loader {
        id: loader
        property string screenName: ui.screen
        anchors.fill: parent
        source: win.sources[screenName]
        opacity: win.enter
        scale: 0.96 + 0.04 * win.enter
    }

    // Any pointer movement counts as activity for the inactivity lock; clicks pass through.
    MouseArea { anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton; onPositionChanged: ui.activity() }

    Shortcut { sequences: ["Ctrl+L"]; onActivated: ui.lock() }
    Shortcut { sequences: ["Ctrl+,"]; onActivated: if (ui.screen === "chat") ui.open("settings") }
    Shortcut { sequences: ["Ctrl+Q"]; onActivated: ui.quit() }
}
