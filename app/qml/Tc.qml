pragma Singleton
import QtQuick

// The TenChar palette (from the logo) and the shared numbers of the design system. Dark only.
QtObject {
    readonly property color space: "#13141C"
    readonly property color plum: "#1E1B38"
    readonly property color mint: "#84F5B7"
    readonly property color lavender: "#B491FE"
    readonly property color lavenderSoft: "#D2B8FF"
    readonly property color peach: "#FFB385"
    /// Peach for TEXT: a little lighter so it keeps 4.5:1 on glass over the lightest part of the aurora.
    readonly property color peachText: "#FFCAAA"
    readonly property color indigo: "#5A33D3"
    readonly property color ink: "#F4F2FF"
    readonly property color inkMuted: Qt.rgba(0.957, 0.949, 1.0, 0.72)
    readonly property color inkFaint: Qt.rgba(0.957, 0.949, 1.0, 0.56)
    readonly property string family: "Inter"
    readonly property string mono: "monospace"
    readonly property real contentMax: 560

    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    function mix(a, b, t) { return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1) }
    function hex(c) {
        function h(v) { var s = Math.round(v * 255).toString(16); return s.length < 2 ? "0" + s : s }
        return "#" + h(c.r) + h(c.g) + h(c.b)
    }
    /// Styled text with the brand gradient (mint to lavender) spread over the characters.
    function gradientHtml(text) {
        var out = "", n = Math.max(1, text.length - 1)
        for (var i = 0; i < text.length; i++) {
            var ch = text[i] === " " ? "&nbsp;" : text[i]
            out += "<font color='" + hex(mix(mint, lavender, i / n)) + "'>" + ch + "</font>"
        }
        return out
    }
}
