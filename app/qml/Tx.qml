import QtQuick

// Text in one of the design system's styles. Tracking is zero on purpose: Bangla conjuncts collide if it goes negative.
Text {
    id: t
    property string kind: "bodyL"
    readonly property var spec: ({
        headline: [32, 42, Font.Bold], titleL: [22, 30, Font.DemiBold], titleM: [17, 24, Font.DemiBold],
        bodyL: [16, 26, Font.Normal], bodyM: [15, 24, Font.Normal], labelL: [17, 24, Font.DemiBold],
        labelM: [13, 19, Font.Medium], labelS: [12, 17, Font.Medium]
    })
    color: Tc.ink
    wrapMode: Text.Wrap
    font.family: Tc.family
    font.pixelSize: spec[kind][0]
    font.weight: spec[kind][2]
    lineHeightMode: Text.FixedHeight
    lineHeight: spec[kind][1]
    Accessible.role: Accessible.StaticText
    Accessible.name: text
}
