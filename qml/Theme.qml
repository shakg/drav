pragma Singleton
import QtQuick

// Dark UI palette modeled on Figma's editor chrome.
QtObject {
    readonly property color bg: "#2c2c2c"
    readonly property color panel: "#2c2c2c"
    readonly property color panelBorder: "#444444"
    readonly property color canvas: "#1e1e1e"
    readonly property color hover: "#383838"
    readonly property color field: "#383838"
    readonly property color fieldHover: "#444444"
    readonly property color text: "#ffffff"
    readonly property color textDim: "#b3b3b3"
    readonly property color textFaint: "#7a7a7a"
    readonly property color accent: "#0d99ff"
    readonly property color accentSoft: "#4c0d99ff"
    readonly property color selectionRow: "#4a5878"
    readonly property color selectionChild: "#363f52"
    readonly property color component: "#9747ff"
    readonly property color componentText: "#d6b6ff"
    readonly property color danger: "#f24822"
    readonly property int fontSize: 11
    readonly property int rowHeight: 32
    readonly property string fontFamily: "Inter"
}
