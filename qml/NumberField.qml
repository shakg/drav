import QtQuick
import QtQuick.Controls.Basic

// Figma-style numeric input: drag the label to scrub, type to set, Up/Down to step.
// When `key` is set, edits go straight to editor.setProp / setPropLive.
Rectangle {
    id: root
    property string key
    property var value
    property string label
    property string icon
    property string suffix
    property int decimals: 2
    property real step: 1
    property real minimum: -1e9
    property real maximum: 1e9
    property real displayScale: 1 // e.g. 100 for opacity shown in percent
    readonly property bool mixed: value === undefined || value === null
    signal committed(real value)
    signal live(real value)
    signal liveEnd()

    implicitWidth: 80
    implicitHeight: 28
    radius: 5
    color: input.activeFocus ? Theme.field : (hover.hovered ? Theme.field : "transparent")
    border.color: input.activeFocus ? Theme.accent : (hover.hovered ? Theme.panelBorder : "transparent")
    border.width: 1

    function fmt(v) {
        if (root.mixed) return "Mixed"
        const s = v * root.displayScale
        const r = Math.round(s * Math.pow(10, decimals)) / Math.pow(10, decimals)
        return String(r) + root.suffix
    }
    function apply(v, isLive) {
        v = Math.max(minimum, Math.min(maximum, v / root.displayScale))
        if (isLive) {
            if (key.length) editor.setPropLive(key, v)
            live(v)
        } else {
            if (key.length) editor.setProp(key, v)
            committed(v)
        }
    }
    function evaluate(text) {
        const t = text.replace(root.suffix, "").replace(/,/g, ".").trim()
        if (!/^[-+*/(). 0-9eE]+$/.test(t)) return NaN
        try { return Number(Function('"use strict";return (' + t + ')')()) } catch (e) { return NaN }
    }

    HoverHandler { id: hover }

    Item {
        id: lead
        width: (root.label.length || root.icon.length) ? 22 : 6
        height: parent.height
        Text {
            anchors.centerIn: parent
            text: root.label
            visible: root.label.length > 0
            color: Theme.textFaint
            font.pixelSize: Theme.fontSize
            font.family: Theme.fontFamily
        }
        Icon {
            anchors.centerIn: parent
            name: root.icon
            visible: root.icon.length > 0
            size: 13
            color: Theme.textFaint
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeHorCursor
            property real startX
            property real startValue
            property bool dragging: false
            onPressed: mouse => {
                startX = mouse.x
                startValue = root.mixed ? 0 : root.value * root.displayScale
                dragging = false
            }
            onPositionChanged: mouse => {
                const dx = mouse.x - startX
                if (Math.abs(dx) > 1) dragging = true
                if (!dragging) return
                const mult = (mouse.modifiers & Qt.ShiftModifier) ? 10 : 1
                root.apply(startValue + Math.round(dx) * root.step * mult, true)
            }
            onReleased: {
                if (dragging) {
                    if (root.key.length) editor.endLive()
                    root.liveEnd()
                }
            }
        }
    }

    TextInput {
        id: input
        anchors.left: lead.right
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        color: root.mixed ? Theme.textDim : Theme.text
        selectionColor: Theme.accent
        font.pixelSize: Theme.fontSize
        font.family: Theme.fontFamily
        clip: true
        selectByMouse: true
        text: root.fmt(root.value)
        onActiveFocusChanged: if (activeFocus) selectAll(); else text = Qt.binding(() => root.fmt(root.value))
        onAccepted: {
            const v = root.evaluate(text)
            if (!isNaN(v)) root.apply(v, false)
            text = Qt.binding(() => root.fmt(root.value))
            focus = false
        }
        Keys.onEscapePressed: { text = Qt.binding(() => root.fmt(root.value)); focus = false }
        Keys.onUpPressed: event => { root.apply((root.mixed ? 0 : root.value * root.displayScale) + root.step * ((event.modifiers & Qt.ShiftModifier) ? 10 : 1), false) }
        Keys.onDownPressed: event => { root.apply((root.mixed ? 0 : root.value * root.displayScale) - root.step * ((event.modifiers & Qt.ShiftModifier) ? 10 : 1), false) }
    }
}
