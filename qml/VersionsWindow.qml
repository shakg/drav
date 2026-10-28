import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window

// Version history: save named versions, restore older ones.
Window {
    id: root
    title: qsTr("Version history")
    width: 380
    height: 480
    color: Theme.panel
    flags: Qt.Dialog
    property var list: []
    function refresh() { list = editor.versions() }
    onVisibleChanged: if (visible) refresh()
    Connections { target: editor; function onVersionsChanged() { root.refresh() } function onFileChanged() { root.refresh() } }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            TextBox { id: nameBox; Layout.fillWidth: true; placeholder: qsTr("Version name") }
            Button { text: qsTr("Save version"); onClicked: { editor.saveVersion(nameBox.text); nameBox.text = "" } }
        }
        Text {
            visible: root.list.length === 0
            text: qsTr("No versions yet. Versions are also saved automatically when you save (at most every 30 minutes).")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            color: Theme.textDim
            font.pixelSize: Theme.fontSize
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.list
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: 44
                color: hover.hovered ? Theme.hover : "transparent"
                radius: 5
                HoverHandler { id: hover }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 4
                    Icon { name: "history"; size: 14; color: Theme.textDim }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { text: modelData.name; color: Theme.text; font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: Qt.formatDateTime(modelData.time, "yyyy-MM-dd hh:mm"); color: Theme.textFaint; font.pixelSize: 10 }
                    }
                    Button { text: qsTr("Restore"); onClicked: editor.restoreVersion(modelData.path) }
                }
            }
        }
    }
}
