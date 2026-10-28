import QtQuick
import QtQuick.Controls.Basic

// Top-level window with the dark theme. Separate windows don't inherit a palette, so every window uses this.
ApplicationWindow {
    color: Theme.bg
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    // Basic-style controls without custom delegates (buttons, check boxes, tooltips…) draw from this.
    palette {
        window: Theme.panel
        windowText: Theme.text
        base: Theme.canvas
        alternateBase: Theme.field
        text: Theme.text
        button: Theme.field
        buttonText: Theme.text
        brightText: Theme.text
        light: Theme.fieldHover
        midlight: Theme.hover
        mid: Theme.panelBorder
        dark: Theme.accent
        shadow: "#000000"
        highlight: Theme.accent
        highlightedText: "#ffffff"
        placeholderText: Theme.textFaint
        toolTipBase: Theme.canvas
        toolTipText: Theme.text
        link: Theme.accent
        disabled.windowText: Theme.textFaint
        disabled.text: Theme.textFaint
        disabled.buttonText: Theme.textFaint
    }
}
