import QtQuick

// HSV picker: saturation/value square, hue strip and alpha strip.
Column {
    id: root
    property color color: "white"
    signal edited(color c, bool live)
    property real h: 0
    property real s: 0
    property real v: 1
    property real a: 1
    property bool internal: false
    spacing: 8
    width: 224

    function load() {
        if (internal) return
        const hh = color.hsvHue
        if (hh >= 0) h = hh
        s = color.hsvSaturation
        v = color.hsvValue
        a = color.a
    }
    onColorChanged: load()
    Component.onCompleted: load()
    function emitColor(live) {
        internal = true
        edited(Qt.hsva(h, s, v, a), live)
        internal = false
    }

    Rectangle {
        id: sv
        width: parent.width
        height: 170
        radius: 4
        color: Qt.hsva(root.h, 1, 1, 1)
        Rectangle {
            anchors.fill: parent
            radius: 4
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "white" }
                GradientStop { position: 1; color: "transparent" }
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: 4
            gradient: Gradient {
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 1; color: "black" }
            }
        }
        Rectangle {
            x: root.s * sv.width - 6
            y: (1 - root.v) * sv.height - 6
            width: 12; height: 12; radius: 6
            color: "transparent"
            border.color: "white"
            border.width: 2
        }
        MouseArea {
            anchors.fill: parent
            function pick(m, live) {
                root.s = Math.max(0, Math.min(1, m.x / width))
                root.v = 1 - Math.max(0, Math.min(1, m.y / height))
                root.emitColor(live)
            }
            onPressed: m => pick(m, true)
            onPositionChanged: m => pick(m, true)
            onReleased: m => pick(m, false)
        }
    }

    component Strip: Item {
        id: strip
        property real value
        signal moved(real v, bool live)
        default property alias fill: track.data
        width: root.width
        height: 12
        Rectangle { id: track; anchors.fill: parent; radius: 6; clip: true; color: "transparent" }
        Rectangle {
            x: strip.value * (strip.width - 12)
            width: 12; height: 12; radius: 6
            color: "transparent"; border.color: "white"; border.width: 2
        }
        MouseArea {
            anchors.fill: parent
            function pick(m, live) { strip.moved(Math.max(0, Math.min(1, (m.x - 6) / (width - 12))), live) }
            onPressed: m => pick(m, true)
            onPositionChanged: m => pick(m, true)
            onReleased: m => pick(m, false)
        }
    }

    Strip {
        value: root.h
        onMoved: (val, live) => { root.h = val; root.emitColor(live) }
        Rectangle {
            anchors.fill: parent
            radius: 6
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#ff0000" }
                GradientStop { position: 0.17; color: "#ffff00" }
                GradientStop { position: 0.33; color: "#00ff00" }
                GradientStop { position: 0.5; color: "#00ffff" }
                GradientStop { position: 0.67; color: "#0000ff" }
                GradientStop { position: 0.83; color: "#ff00ff" }
                GradientStop { position: 1.0; color: "#ff0000" }
            }
        }
    }
    Strip {
        value: root.a
        onMoved: (val, live) => { root.a = val; root.emitColor(live) }
        Checker { anchors.fill: parent }
        Rectangle {
            anchors.fill: parent
            radius: 6
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 1; color: Qt.hsva(root.h, root.s, root.v, 1) }
            }
        }
    }
}
