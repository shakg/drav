import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Rename several layers at once: pattern with $& (current name), $n / $N (numbers), find/replace.
Popup {
    id: root
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 360
    padding: 14
    background: Rectangle { color: "#1e1e1e"; radius: 10; border.color: "#3a3a3a" }
    onOpened: { pattern.text = "$&"; findBox.text = ""; replaceBox.text = ""; pattern.forceActiveFocus() }

    contentItem: ColumnLayout {
        spacing: 8
        Text { text: qsTr("Rename %1 layers").arg(editor.selectionCount); color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
        TextBox { id: pattern; Layout.fillWidth: true; placeholder: qsTr("Name pattern") }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("$& current name · $n number up · $N number down")
            color: Theme.textFaint
            font.pixelSize: 10
        }
        RowLayout {
            Text { text: qsTr("Start at"); color: Theme.textDim; font.pixelSize: Theme.fontSize }
            NumberField { id: start; Layout.preferredWidth: 70; value: 1; decimals: 0; minimum: 0; onCommitted: v => value = v }
        }
        RowLayout {
            Layout.fillWidth: true
            TextBox { id: findBox; Layout.fillWidth: true; placeholder: qsTr("Find") }
            TextBox { id: replaceBox; Layout.fillWidth: true; placeholder: qsTr("Replace") }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Cancel"); onClicked: root.close() }
            Button {
                text: qsTr("Rename")
                onClicked: { editor.renameLayers(pattern.text === "$&" ? "" : pattern.text, findBox.text, replaceBox.text, start.value); root.close() }
            }
        }
    }
}
