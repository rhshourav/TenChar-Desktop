import QtQuick

GlassCard {
    RadioRow { text: ui.strings.lock_immediately; selected: ui.autoLock === "immediately"; onChosen: ui.chooseAutoLock("immediately") }
    RowDivider {}
    RadioRow { text: ui.strings.lock_30s; selected: ui.autoLock === "30s"; onChosen: ui.chooseAutoLock("30s") }
    RowDivider {}
    RadioRow { text: ui.strings.lock_5m; selected: ui.autoLock === "5m"; onChosen: ui.chooseAutoLock("5m") }
}
