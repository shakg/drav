import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Component properties: definitions (component / set), values (instance), links (layer in a component).
ColumnLayout {
    id: root
    readonly property var p: editor.props
    readonly property bool has: p.count === 1
    readonly property var defs: has && p.componentProps ? p.componentProps : null
    readonly property var values: has && p.instanceProps ? p.instanceProps : []
    readonly property var linkable: has && p.linkableProps ? p.linkableProps : null
    readonly property var typeIcons: ({ BOOLEAN: "toggle", TEXT: "textProp", INSTANCE_SWAP: "instance" })
    spacing: 0
    Layout.fillWidth: true

    // ---------------- Definitions
    Section {
        visible: root.defs !== null
        title: qsTr("Properties")
        showAdd: true
        onAdd: addMenu.popup()
        Menu {
            id: addMenu
            MenuItem { text: qsTr("Boolean"); onTriggered: editor.addComponentProp("BOOLEAN", "") }
            MenuItem { text: qsTr("Text"); onTriggered: editor.addComponentProp("TEXT", "") }
            MenuItem { text: qsTr("Instance swap"); onTriggered: editor.addComponentProp("INSTANCE_SWAP", "") }
        }
        Repeater {
            model: root.defs || []
            RowLayout {
                id: defRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 4
                Icon { name: root.typeIcons[defRow.modelData.type]; size: 13; color: Theme.textDim }
                TextBox {
                    Layout.preferredWidth: 80
                    text: defRow.modelData.name
                    onAccepted: t => editor.editComponentProp(defRow.modelData.id, { name: t })
                }
                IconButton {
                    visible: defRow.modelData.type === "BOOLEAN"
                    iconName: defRow.modelData.default === "false" ? "eyeOff" : "eye"
                    tip: qsTr("Default value")
                    onClicked: editor.editComponentProp(defRow.modelData.id, { default: defRow.modelData.default === "false" })
                }
                TextBox {
                    visible: defRow.modelData.type === "TEXT"
                    Layout.fillWidth: true
                    text: defRow.modelData.default
                    onAccepted: t => editor.editComponentProp(defRow.modelData.id, { default: t })
                }
                Dropdown {
                    visible: defRow.modelData.type === "INSTANCE_SWAP"
                    Layout.fillWidth: true
                    current: defRow.modelData.default
                    model: editor.components.map(c => ({ value: String(c.id), text: (c.set ? c.set + " / " : "") + c.name }))
                    onPicked: v => editor.editComponentProp(defRow.modelData.id, { default: v })
                }
                Item { Layout.fillWidth: true; visible: defRow.modelData.type === "BOOLEAN" }
                IconButton { iconName: "minus"; tip: qsTr("Delete property"); onClicked: editor.deleteComponentProp(defRow.modelData.id) }
            }
        }
    }

    // ---------------- Instance values
    Section {
        visible: root.values.length > 0
        title: qsTr("Properties")
        Repeater {
            model: root.values
            RowLayout {
                id: valRow
                required property var modelData
                Layout.fillWidth: true
                Text { text: valRow.modelData.name; color: Theme.textDim; font.pixelSize: Theme.fontSize; Layout.preferredWidth: 80; elide: Text.ElideRight }
                Switch {
                    visible: valRow.modelData.type === "BOOLEAN"
                    checked: valRow.modelData.value !== "false"
                    onToggled: editor.setInstanceProp(valRow.modelData.id, checked)
                    implicitHeight: 24
                }
                Item { Layout.fillWidth: true; visible: valRow.modelData.type === "BOOLEAN" }
                TextBox {
                    visible: valRow.modelData.type === "TEXT"
                    Layout.fillWidth: true
                    text: valRow.modelData.value
                    onAccepted: t => editor.setInstanceProp(valRow.modelData.id, t)
                }
                Dropdown {
                    visible: valRow.modelData.type === "INSTANCE_SWAP"
                    Layout.fillWidth: true
                    current: valRow.modelData.value
                    model: valRow.modelData.options || []
                    onPicked: v => editor.setInstanceProp(valRow.modelData.id, v)
                }
            }
        }
    }

    // ---------------- Links from a layer inside a component
    Section {
        visible: root.linkable !== null
        title: qsTr("Component property links")
        Repeater {
            model: [{ field: "visible", type: "BOOLEAN", label: qsTr("Visibility") }]
                .concat(root.p.type === "TEXT" ? [{ field: "characters", type: "TEXT", label: qsTr("Text") }] : [])
                .concat(root.p.type === "INSTANCE" ? [{ field: "mainComponent", type: "INSTANCE_SWAP", label: qsTr("Instance") }] : [])
            RowLayout {
                id: linkRow
                required property var modelData
                Layout.fillWidth: true
                Text { text: linkRow.modelData.label; color: Theme.textDim; font.pixelSize: Theme.fontSize; Layout.preferredWidth: 80 }
                Dropdown {
                    Layout.fillWidth: true
                    current: root.p.propRefs ? (root.p.propRefs[linkRow.modelData.field] || "") : ""
                    model: [{ value: "", text: qsTr("Not linked") }]
                        .concat((root.linkable || []).filter(d => d.type === linkRow.modelData.type).map(d => ({ value: d.id, text: d.name })))
                        .concat([{ value: "__create", text: qsTr("Create property…") }])
                    onPicked: v => v === "__create" ? editor.createLinkedProp(linkRow.modelData.field, "")
                                                    : editor.linkComponentProp(linkRow.modelData.field, v)
                }
            }
        }
    }
}
