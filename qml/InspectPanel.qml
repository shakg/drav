import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Read-only code for the selection (CSS), with copy buttons.
ColumnLayout {
    id: root
    spacing: 6
    readonly property string code: editor.selectionCount > 0 ? (editor.props, editor.inspectCode()) : ""

    Text {
        visible: root.code.length === 0
        Layout.margins: 16
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("Select a layer to see its CSS.")
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
    }
    RowLayout {
        visible: root.code.length > 0
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 8
        Layout.topMargin: 8
        Text { text: "CSS"; color: Theme.text; font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; Layout.fillWidth: true }
        IconButton { iconName: "copy"; tip: qsTr("Copy CSS"); onClicked: editor.copyAs("CSS") }
        IconButton { iconName: "code"; tip: qsTr("Copy as SVG"); onClicked: editor.copyAs("SVG") }
        IconButton { iconName: "image"; tip: qsTr("Copy as PNG"); onClicked: editor.copyAs("PNG") }
    }
    ScrollView {
        visible: root.code.length > 0
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 8
        Layout.rightMargin: 4
        clip: true
        TextArea {
            readOnly: true
            selectByMouse: true
            text: root.code
            color: "#d4d4d4"
            font.family: "monospace"
            font.pixelSize: 11
            wrapMode: TextEdit.Wrap
            background: Rectangle { color: "#1e1e1e"; radius: 6 }
        }
    }
}
