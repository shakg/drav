import QtQuick
import QtQuick.Layouts

// Fill or stroke list for the selection; `key` is "fills" or "strokes".
Section {
    id: root
    property string key: "fills"
    property var paints: editor.props[key]
    readonly property bool mixed: paints === undefined || paints === null
    showAdd: true
    signal popupRequested(var item, int index)

    function copyList() {
        return (paints || []).map(p => {
            const c = Object.assign({}, p)
            c.stops = (p.stops || []).map(s => ({ pos: s.pos, color: s.color }))
            return c
        })
    }
    function update(list, live) {
        if (live) editor.setPropLive(key, list)
        else { editor.setProp(key, list); editor.endLive() }
    }
    onAdd: {
        const list = mixed ? [] : copyList()
        const black = key === "strokes" || editor.props.type === "TEXT"
        list.push({ type: "SOLID", color: black ? Qt.rgba(0, 0, 0, 1) : Qt.rgba(0.85, 0.85, 0.85, 1), opacity: 1, visible: true, blend: "NORMAL" })
        update(list, false)
    }

    Text {
        visible: root.mixed
        text: qsTr("Click + to replace mixed content")
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
    }

    Repeater {
        model: root.mixed ? [] : root.paints.slice().reverse()
        delegate: RowLayout {
            id: row
            required property var modelData
            required property int index
            readonly property int realIndex: root.paints.length - 1 - index
            Layout.fillWidth: true
            spacing: 4
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                radius: 5
                color: rowHover.hovered ? Theme.field : "transparent"
                HoverHandler { id: rowHover }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    spacing: 6
                    Swatch {
                        paint: row.modelData
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                popup.paintIndex = row.realIndex
                                popup.paint = row.modelData
                                popup.parent = row
                                popup.x = -popup.width - 24
                                popup.y = 0
                                popup.open()
                            }
                        }
                    }
                    TextBox {
                        Layout.fillWidth: true
                        visible: row.modelData.type === "SOLID"
                        text: popup.hex(row.modelData.color)
                        onAccepted: t => {
                            const c = Qt.color("#" + t.replace("#", ""))
                            if (!c.valid) return
                            const list = root.copyList()
                            list[row.realIndex].color = Qt.rgba(c.r, c.g, c.b, row.modelData.color.a)
                            root.update(list, false)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: row.modelData.type !== "SOLID"
                        text: ({ GRADIENT_LINEAR: qsTr("Linear"), GRADIENT_RADIAL: qsTr("Radial"), GRADIENT_ANGULAR: qsTr("Angular"),
                                 GRADIENT_DIAMOND: qsTr("Diamond"), IMAGE: qsTr("Image") })[row.modelData.type] || ""
                        color: Theme.text
                        font.pixelSize: Theme.fontSize
                    }
                    NumberField {
                        Layout.preferredWidth: 56
                        value: row.modelData.opacity
                        displayScale: 100
                        suffix: "%"
                        decimals: 0
                        minimum: 0
                        maximum: 1
                        onCommitted: v => { const l = root.copyList(); l[row.realIndex].opacity = v; root.update(l, false) }
                        onLive: v => { const l = root.copyList(); l[row.realIndex].opacity = v; root.update(l, true) }
                        onLiveEnd: editor.endLive()
                    }
                }
            }
            IconButton {
                iconName: row.modelData.visible ? "eye" : "eyeOff"
                onClicked: { const l = root.copyList(); l[row.realIndex].visible = !row.modelData.visible; root.update(l, false) }
            }
            IconButton {
                iconName: "minus"
                onClicked: { const l = root.copyList(); l.splice(row.realIndex, 1); root.update(l, false) }
            }
        }
    }

    PaintPopup {
        id: popup
        property int paintIndex: -1
        allowImage: root.key === "fills"
        onEdited: (p, live) => {
            const l = root.copyList()
            if (paintIndex < 0 || paintIndex >= l.length) return
            l[paintIndex] = p
            popup.paint = p
            root.update(l, live)
        }
        onClosed: editor.endLive()
    }
}
