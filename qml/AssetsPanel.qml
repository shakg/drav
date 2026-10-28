import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Local components; click to insert an instance.
ColumnLayout {
    spacing: 4
    TextBox { id: search; Layout.fillWidth: true; Layout.margins: 8; placeholder: qsTr("Search assets") }
    Text {
        visible: editor.components.length === 0
        Layout.margins: 16
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("No components yet. Select layers and press Ctrl+Alt+K to create one.")
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
    }
    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: editor.components.filter(c => (c.set + " " + c.name).toLowerCase().indexOf(search.text.toLowerCase()) >= 0)
        delegate: Rectangle {
            required property var modelData
            width: ListView.view.width
            height: 32
            color: hover.hovered ? Theme.hover : "transparent"
            HoverHandler { id: hover }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 8
                Icon { name: "component"; size: 14; color: Theme.component }
                Text {
                    Layout.fillWidth: true
                    text: (modelData.set ? modelData.set + " / " : "") + modelData.name
                    color: Theme.text
                    font.pixelSize: Theme.fontSize
                    elide: Text.ElideRight
                }
                Text { text: modelData.page; color: Theme.textFaint; font.pixelSize: 10 }
            }
            MouseArea { anchors.fill: parent; onClicked: editor.placeComponent(modelData.id) }
            ToolTip.visible: hover.hovered
            ToolTip.delay: 800
            ToolTip.text: qsTr("Click to insert an instance")
        }
    }
}
