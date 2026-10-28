import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Pick a local style to apply to the selection, or create one from it.
// slot: "fills", "strokes", "text", "effects" or "grids".
Popup {
    id: root
    property string slot: "fills"
    property string current
    property string filter
    readonly property string styleType: ({ fills: "FILL", strokes: "FILL", text: "TEXT", effects: "EFFECT", grids: "GRID" })[slot]
    readonly property var styles: editor.library.styles.filter(s => s.type === styleType && s.name.toLowerCase().indexOf(filter) >= 0)
    padding: 6
    width: 240
    height: Math.min(400, list.contentHeight + 110)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: "#1e1e1e"; radius: 10; border.color: "#3a3a3a" }
    onOpened: { filter = ""; search.text = ""; nameBox.text = ""; creating = false }
    property bool creating: false

    contentItem: ColumnLayout {
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Styles"); color: Theme.text; font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; Layout.fillWidth: true; leftPadding: 4 }
            IconButton { iconName: "plus"; tip: qsTr("Create style from selection"); onClicked: root.creating = !root.creating }
        }
        RowLayout {
            visible: root.creating
            Layout.fillWidth: true
            TextBox {
                id: nameBox
                Layout.fillWidth: true
                placeholder: qsTr("Style name (Group/Name)")
                onAccepted: t => { editor.createStyle(root.slot, t); root.close() }
            }
            IconButton { iconName: "check"; onClicked: { editor.createStyle(root.slot, nameBox.text); root.close() } }
        }
        TextBox {
            id: search
            Layout.fillWidth: true
            placeholder: qsTr("Search styles")
            onTextChanged: root.filter = text.toLowerCase()
        }
        Text {
            visible: root.styles.length === 0
            Layout.fillWidth: true
            Layout.margins: 6
            wrapMode: Text.WordWrap
            text: qsTr("No styles yet. Press + to save the current value as a style.")
            color: Theme.textDim
            font.pixelSize: Theme.fontSize
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.styles
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: 28
                radius: 4
                color: hover.hovered ? Theme.accent : "transparent"
                HoverHandler { id: hover }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8
                    StylePreview { style: modelData }
                    Text { Layout.fillWidth: true; text: modelData.name; color: "white"; font.pixelSize: Theme.fontSize; elide: Text.ElideRight }
                    Icon { visible: modelData.id === root.current; name: "check"; size: 12 }
                }
                MouseArea { anchors.fill: parent; onClicked: { editor.applyStyle(root.slot, modelData.id); root.close() } }
            }
        }
    }
}
