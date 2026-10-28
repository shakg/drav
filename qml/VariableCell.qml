import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One variable value in one mode: literal editor or alias pill, plus an alias picker.
Item {
    id: root
    property var variable
    property string modeId
    readonly property var entry: variable && variable.values ? variable.values[modeId] : null
    readonly property string alias: entry ? entry.alias : ""
    readonly property var aliasInfo: alias.length ? (editor.library.variables.find(v => v.id === alias) || null) : null
    readonly property string type: variable ? variable.type : ""
    implicitHeight: 30

    function hex(c) {
        const h = v => ("0" + Math.round(v * 255).toString(16)).slice(-2)
        return (h(c.r) + h(c.g) + h(c.b)).toUpperCase()
    }

    RowLayout {
        anchors.fill: parent
        anchors.rightMargin: 8
        spacing: 4

        // Alias
        Rectangle {
            visible: root.alias.length > 0
            Layout.fillWidth: true
            implicitHeight: 22
            radius: 4
            color: Theme.hover
            border.color: Theme.panelBorder
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                spacing: 4
                Swatch { visible: root.type === "COLOR"; paint: ({ type: "SOLID", color: root.entry && root.entry.resolved ? root.entry.resolved : "transparent", opacity: 1 }) }
                Text {
                    Layout.fillWidth: true
                    text: root.aliasInfo ? root.aliasInfo.name : qsTr("Missing")
                    color: Theme.text
                    font.pixelSize: Theme.fontSize
                    elide: Text.ElideRight
                }
            }
        }

        // Literals
        Swatch {
            visible: !root.alias.length && root.type === "COLOR"
            paint: ({ type: "SOLID", color: root.entry ? root.entry.value : "black", opacity: 1 })
            MouseArea {
                anchors.fill: parent
                onClicked: { colorPopup.paint = { type: "SOLID", color: root.entry.value, opacity: 1, blend: "NORMAL" }; colorPopup.open() }
            }
            PaintPopup {
                id: colorPopup
                y: 20
                allowImage: false
                onEdited: (p, live) => {
                    const c = p.type === "SOLID" ? p.color : p.stops[0].color
                    colorPopup.paint = { type: "SOLID", color: c, opacity: 1, blend: "NORMAL" }
                    editor.setVariableValue(root.variable.id, root.modeId, c, live)
                }
                onClosed: editor.endLive()
            }
        }
        TextBox {
            visible: !root.alias.length && root.type === "COLOR"
            Layout.fillWidth: true
            text: root.entry ? root.hex(root.entry.value) : ""
            onAccepted: t => {
                const c = Qt.color("#" + t.replace("#", ""))
                if (c.valid) editor.setVariableValue(root.variable.id, root.modeId, Qt.rgba(c.r, c.g, c.b, root.entry.value.a))
            }
        }
        NumberField {
            visible: !root.alias.length && root.type === "FLOAT"
            Layout.fillWidth: true
            value: root.entry ? root.entry.value : 0
            onCommitted: v => editor.setVariableValue(root.variable.id, root.modeId, v)
            onLive: v => editor.setVariableValue(root.variable.id, root.modeId, v, true)
            onLiveEnd: editor.endLive()
        }
        TextBox {
            visible: !root.alias.length && root.type === "STRING"
            Layout.fillWidth: true
            text: root.entry ? root.entry.value : ""
            onAccepted: t => editor.setVariableValue(root.variable.id, root.modeId, t)
        }
        CheckBox {
            visible: !root.alias.length && root.type === "BOOLEAN"
            Layout.fillWidth: true
            checked: !!root.entry && root.entry.value === true
            onToggled: editor.setVariableValue(root.variable.id, root.modeId, checked)
            contentItem: Text { leftPadding: 22; text: parent.checked ? "true" : "false"; color: Theme.text; font.pixelSize: Theme.fontSize; verticalAlignment: Text.AlignVCenter }
        }

        IconButton {
            iconName: "variable"
            iconSize: 12
            checked: root.alias.length > 0
            tip: qsTr("Use another variable (alias)")
            onClicked: { aliasPicker.current = root.alias; aliasPicker.open() }
            VariablePicker {
                id: aliasPicker
                y: 24
                varType: root.type
                exclude: root.variable ? root.variable.id : ""
                onPicked: id => {
                    if (id.length) editor.setVariableAlias(root.variable.id, root.modeId, id)
                    else editor.setVariableValue(root.variable.id, root.modeId, root.entry.resolved)
                }
            }
        }
    }
}
