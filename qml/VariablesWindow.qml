import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window

// Local variables table: collections on the left, variables x modes on the right.
ThemedWindow {
    id: root
    title: qsTr("Local variables")
    width: 900
    height: 560
    flags: Qt.Dialog
    property int collectionIndex: 0
    readonly property var lib: editor.library
    readonly property var collection: lib.collections.length ? lib.collections[Math.min(collectionIndex, lib.collections.length - 1)] : null
    readonly property var variables: collection ? lib.variables.filter(v => v.collection === collection.id) : []
    readonly property int nameWidth: 220
    readonly property int cellWidth: 180
    readonly property var typeIcons: ({ COLOR: "effects", FLOAT: "variable", STRING: "text", BOOLEAN: "toggle" })

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---------------- Collections
        Rectangle {
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: Theme.bg
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    Layout.leftMargin: 12
                    Layout.rightMargin: 6
                    Text { text: qsTr("Collections"); color: Theme.text; font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    IconButton {
                        iconName: "plus"
                        tip: qsTr("New collection")
                        onClicked: { editor.addCollection(""); root.collectionIndex = root.lib.collections.length - 1 }
                    }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.lib.collections
                    delegate: Rectangle {
                        id: collRow
                        required property var modelData
                        required property int index
                        width: ListView.view.width
                        height: 30
                        color: index === root.collectionIndex ? Theme.selectionRow : (hover.hovered ? Theme.hover : "transparent")
                        HoverHandler { id: hover }
                        MouseArea { anchors.fill: parent; onClicked: root.collectionIndex = collRow.index }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 4
                            TextBox {
                                Layout.fillWidth: true
                                text: collRow.modelData.name
                                onAccepted: t => editor.renameCollection(collRow.modelData.id, t)
                            }
                            IconButton {
                                visible: hover.hovered
                                iconName: "trash"
                                iconSize: 13
                                tip: qsTr("Delete collection and its variables")
                                onClicked: editor.deleteCollection(collRow.modelData.id)
                            }
                        }
                    }
                }
            }
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.panelBorder }
        }

        // ---------------- Table
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Text {
                visible: !root.collection
                Layout.fillWidth: true
                Layout.fillHeight: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Create a collection to start adding variables.")
                color: Theme.textDim
                font.pixelSize: Theme.fontSize
            }

            Flickable {
                id: table
                visible: !!root.collection
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: Math.max(width, root.nameWidth + (root.collection ? root.collection.modes.length : 0) * root.cellWidth + 48)
                contentHeight: body.implicitHeight
                ScrollBar.horizontal: ScrollBar {}
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    id: body
                    width: table.contentWidth
                    spacing: 0

                    // Header: name + one column per mode.
                    RowLayout {
                        Layout.preferredHeight: 40
                        spacing: 0
                        Text {
                            Layout.preferredWidth: root.nameWidth
                            leftPadding: 16
                            text: qsTr("Name")
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSize
                        }
                        Repeater {
                            model: root.collection ? root.collection.modes : []
                            RowLayout {
                                id: modeHead
                                required property var modelData
                                Layout.preferredWidth: root.cellWidth
                                Layout.fillWidth: false
                                spacing: 0
                                HoverHandler { id: headHover }
                                TextBox {
                                    Layout.fillWidth: true
                                    text: modeHead.modelData.name
                                    onAccepted: t => editor.renameMode(root.collection.id, modeHead.modelData.id, t)
                                }
                                IconButton {
                                    visible: headHover.hovered && root.collection.modes.length > 1
                                    iconName: "trash"
                                    iconSize: 13
                                    tip: qsTr("Delete mode")
                                    onClicked: editor.deleteMode(root.collection.id, modeHead.modelData.id)
                                }
                            }
                        }
                        IconButton { iconName: "plus"; tip: qsTr("Add mode"); onClicked: editor.addMode(root.collection.id, "") }
                        Item { Layout.fillWidth: true }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.panelBorder }

                    Repeater {
                        model: root.variables
                        Rectangle {
                            id: varRow
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 34
                            color: rowHover.hovered ? "#333333" : "transparent"
                            HoverHandler { id: rowHover }
                            RowLayout {
                                anchors.fill: parent
                                spacing: 0
                                RowLayout {
                                    Layout.preferredWidth: root.nameWidth - 10
                                    Layout.fillWidth: false
                                    Layout.leftMargin: 10
                                    spacing: 4
                                    Icon { name: root.typeIcons[varRow.modelData.type]; size: 13; color: Theme.textDim }
                                    TextBox {
                                        Layout.fillWidth: true
                                        text: varRow.modelData.name
                                        onAccepted: t => editor.renameVariable(varRow.modelData.id, t)
                                    }
                                }
                                Repeater {
                                    model: root.collection ? root.collection.modes : []
                                    VariableCell {
                                        required property var modelData
                                        Layout.preferredWidth: root.cellWidth
                                        variable: varRow.modelData
                                        modeId: modelData.id
                                    }
                                }
                                IconButton {
                                    visible: rowHover.hovered
                                    iconName: "trash"
                                    iconSize: 13
                                    tip: qsTr("Delete variable")
                                    onClicked: editor.deleteVariable(varRow.modelData.id)
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }

                    RowLayout {
                        Layout.margins: 10
                        Button {
                            text: qsTr("+ Create variable")
                            onClicked: createMenu.popup()
                            Menu {
                                id: createMenu
                                MenuItem { text: qsTr("Color"); onTriggered: editor.addVariable(root.collection.id, "COLOR", "") }
                                MenuItem { text: qsTr("Number"); onTriggered: editor.addVariable(root.collection.id, "FLOAT", "") }
                                MenuItem { text: qsTr("String"); onTriggered: editor.addVariable(root.collection.id, "STRING", "") }
                                MenuItem { text: qsTr("Boolean"); onTriggered: editor.addVariable(root.collection.id, "BOOLEAN", "") }
                            }
                        }
                    }
                }
            }
        }
    }
}
