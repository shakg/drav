import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Editor for one paint: solid color, gradients (with stops) or image.
Popup {
    id: root
    property var paint: ({})
    property bool allowImage: true
    property int stopIndex: 0
    signal edited(var paint, bool live)
    property bool waitingPick: false
    Connections { target: root; function onClosed() { root.waitingPick = false } }
    Connections {
        target: editor
        function onColorPicked(c) {
            if (!root.waitingPick) return
            root.waitingPick = false
            root.setColor(Qt.rgba(c.r, c.g, c.b, root.activeColor.a), false)
        }
    }
    padding: 12
    width: 248
    modal: false
    focus: true
    closePolicy: root.waitingPick ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: "#1e1e1e"; radius: 10; border.color: "#3a3a3a" }

    readonly property bool gradient: !!paint.type && paint.type.startsWith("GRADIENT")
    readonly property color activeColor: gradient ? (paint.stops && paint.stops[stopIndex] ? paint.stops[stopIndex].color : "black") : (paint.color || "black")

    function copy() {
        const p = Object.assign({}, paint)
        p.stops = (paint.stops || []).map(s => ({ pos: s.pos, color: s.color }))
        return p
    }
    function hex(c) {
        const h = v => ("0" + Math.round(v * 255).toString(16)).slice(-2)
        return (h(c.r) + h(c.g) + h(c.b)).toUpperCase()
    }
    function setColor(c, live) {
        const p = copy()
        if (gradient) {
            p.stops[stopIndex] = { pos: p.stops[stopIndex].pos, color: c }
        } else {
            p.color = c
        }
        edited(p, live)
    }
    function setType(t) {
        const p = copy()
        if (t.startsWith("GRADIENT") && !gradient) {
            const base = paint.color || Qt.rgba(0, 0, 0, 1)
            p.stops = [{ pos: 0, color: Qt.rgba(base.r, base.g, base.b, 1) }, { pos: 1, color: Qt.rgba(base.r, base.g, base.b, 0) }]
            p.start = t === "GRADIENT_LINEAR" ? [0, 0.5] : [0.5, 0.5]
            p.end = t === "GRADIENT_LINEAR" ? [1, 0.5] : [1, 0.5]
        } else if (t.startsWith("GRADIENT")) {
            if (t === "GRADIENT_LINEAR") { p.start = [0, 0.5]; p.end = [1, 0.5] }
            else { p.start = [0.5, 0.5]; p.end = [1, 0.5] }
        } else if (t === "SOLID" && gradient) {
            p.color = paint.stops[0].color
        }
        p.type = t
        stopIndex = 0
        edited(p, false)
    }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Dropdown {
                Layout.fillWidth: true
                current: root.paint.type
                model: [
                    { value: "SOLID", text: qsTr("Solid") },
                    { value: "GRADIENT_LINEAR", text: qsTr("Linear") },
                    { value: "GRADIENT_RADIAL", text: qsTr("Radial") },
                    { value: "GRADIENT_ANGULAR", text: qsTr("Angular") },
                    { value: "GRADIENT_DIAMOND", text: qsTr("Diamond") }
                ].concat(root.allowImage ? [{ value: "IMAGE", text: qsTr("Image") }] : [])
                onPicked: v => root.setType(v)
            }
            Dropdown {
                Layout.preferredWidth: 96
                visible: root.paint.type !== undefined
                current: root.paint.blend
                model: ["NORMAL", "DARKEN", "MULTIPLY", "COLOR_BURN", "LIGHTEN", "SCREEN", "COLOR_DODGE", "OVERLAY",
                    "SOFT_LIGHT", "HARD_LIGHT", "DIFFERENCE", "EXCLUSION", "HUE", "SATURATION", "COLOR", "LUMINOSITY"]
                    .map(v => ({ value: v, text: v.charAt(0) + v.slice(1).toLowerCase().replace("_", " ") }))
                onPicked: v => { const p = root.copy(); p.blend = v; root.edited(p, false) }
            }
        }

        // Gradient stops bar: click to add, drag to move, select to edit color.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            visible: root.gradient
            Checker { anchors.fill: bar; cell: 4 }
            Canvas {
                id: bar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 16
                property var stops: root.gradient ? root.paint.stops : []
                onStopsChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    if (!stops.length) return
                    const g = ctx.createLinearGradient(0, 0, width, 0)
                    for (const s of stops) g.addColorStop(Math.max(0, Math.min(1, s.pos)), s.color)
                    ctx.fillStyle = g
                    ctx.fillRect(0, 0, width, height)
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: m => {
                        const p = root.copy()
                        const pos = m.x / width
                        p.stops.push({ pos: pos, color: root.activeColor })
                        p.stops.sort((a, b) => a.pos - b.pos)
                        root.stopIndex = p.stops.findIndex(s => s.pos === pos)
                        root.edited(p, false)
                    }
                }
            }
            Repeater {
                model: root.gradient ? root.paint.stops : []
                Rectangle {
                    required property var modelData
                    required property int index
                    x: modelData.pos * bar.width - 6
                    y: 10
                    width: 12; height: 16; radius: 3
                    color: modelData.color
                    border.color: index === root.stopIndex ? Theme.accent : "white"
                    border.width: 2
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -3
                        property real startX
                        onPressed: m => { root.stopIndex = index; startX = m.x }
                        onPositionChanged: m => {
                            const p = root.copy()
                            const nx = (parent.x + 6 + m.x - startX) / bar.width
                            p.stops[index] = { pos: Math.max(0, Math.min(1, nx)), color: p.stops[index].color }
                            root.edited(p, true)
                        }
                        onReleased: root.edited(root.copy(), false)
                        onDoubleClicked: {
                            if (root.paint.stops.length <= 2) return
                            const p = root.copy()
                            p.stops.splice(index, 1)
                            root.stopIndex = 0
                            root.edited(p, false)
                        }
                    }
                }
            }
        }

        // Image options
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.paint.type === "IMAGE"
            spacing: 6
            Button {
                Layout.fillWidth: true
                text: root.paint.image ? qsTr("Replace image…") : qsTr("Choose image…")
                onClicked: imageDialog.open()
            }
            Dropdown {
                Layout.fillWidth: true
                current: root.paint.scaleMode
                model: [{ value: "FILL", text: qsTr("Fill") }, { value: "FIT", text: qsTr("Fit") },
                    { value: "CROP", text: qsTr("Crop") }, { value: "TILE", text: qsTr("Tile") }]
                onPicked: v => { const p = root.copy(); p.scaleMode = v; root.edited(p, false) }
            }
        }

        ColorArea {
            visible: root.paint.type !== "IMAGE"
            Layout.fillWidth: true
            color: root.activeColor
            onEdited: (c, live) => root.setColor(c, live)
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.paint.type !== "IMAGE"
            IconButton {
                iconName: "pipette"
                iconSize: 14
                tip: qsTr("Pick a color from the canvas (I)")
                onClicked: { root.waitingPick = true; editor.pickColor() }
            }
            Text { text: "#"; color: Theme.textFaint; font.pixelSize: Theme.fontSize }
            TextBox {
                Layout.fillWidth: true
                text: root.hex(root.activeColor)
                onAccepted: t => {
                    const c = Qt.color("#" + t.replace("#", ""))
                    if (c.valid) root.setColor(Qt.rgba(c.r, c.g, c.b, root.activeColor.a), false)
                }
            }
            NumberField {
                Layout.preferredWidth: 64
                value: root.activeColor.a
                displayScale: 100
                suffix: "%"
                decimals: 0
                minimum: 0
                maximum: 1
                onCommitted: v => { const c = root.activeColor; root.setColor(Qt.rgba(c.r, c.g, c.b, v), false) }
                onLive: v => { const c = root.activeColor; root.setColor(Qt.rgba(c.r, c.g, c.b, v), true) }
                onLiveEnd: root.setColor(root.activeColor, false)
            }
        }
    }

    FileDialog {
        id: imageDialog
        title: qsTr("Choose image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp)")]
        onAccepted: {
            const hash = editor.addImageFile(selectedFile)
            if (hash.length) { const p = root.copy(); p.image = hash; root.edited(p, false) }
        }
    }
}
