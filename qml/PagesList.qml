import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Pages of the document.
ColumnLayout {
    id: root
    spacing: 0
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 8
        Layout.preferredHeight: 36
        Text { text: qsTr("Pages"); color: Theme.text; font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; Layout.fillWidth: true }
        IconButton { iconName: "plus"; tip: qsTr("Add page"); onClicked: editor.addPage() }
    }
    Repeater {
        model: editor.pages
        Rectangle {
            id: pageRow
            required property var modelData
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: hover.hovered ? Theme.hover : "transparent"
            HoverHandler { id: hover }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 8
                Icon { name: "check"; size: 12; opacity: editor.currentPage === pageRow.modelData.id ? 1 : 0 }
                Text {
                    visible: !edit.visible
                    text: pageRow.modelData.name
                    color: Theme.text
                    font.pixelSize: Theme.fontSize
                    font.weight: editor.currentPage === pageRow.modelData.id ? Font.DemiBold : Font.Normal
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                TextInput {
                    id: edit
                    visible: false
                    Layout.fillWidth: true
                    color: Theme.text
                    font.pixelSize: Theme.fontSize
                    onAccepted: { editor.renamePage(pageRow.modelData.id, text); visible = false }
                    onActiveFocusChanged: if (!activeFocus) visible = false
                }
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: m => {
                    if (m.button === Qt.RightButton) pageMenu.popup()
                    else editor.currentPage = pageRow.modelData.id
                }
                onDoubleClicked: { edit.text = pageRow.modelData.name; edit.visible = true; edit.forceActiveFocus(); edit.selectAll() }
            }
            AppMenu {
                id: pageMenu
                AppMenuItem { text: qsTr("Rename"); onTriggered: { edit.text = pageRow.modelData.name; edit.visible = true; edit.forceActiveFocus() } }
                AppMenuItem { text: qsTr("Duplicate"); onTriggered: editor.duplicatePage(pageRow.modelData.id) }
                AppMenuItem { text: qsTr("Delete"); enabled: editor.pages.length > 1; onTriggered: editor.deletePage(pageRow.modelData.id) }
            }
        }
    }
    Item { Layout.preferredHeight: 6 }
}
