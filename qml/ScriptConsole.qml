import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window

// Runs JavaScript against the document through the `drav` object (one undo step per run).
Window {
    id: root
    title: qsTr("Script console")
    width: 640
    height: 520
    color: Theme.panel
    flags: Qt.Dialog

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("drav.selection(), drav.get(id), drav.set(id, key, value), drav.create(type, parent, props), drav.remove(id), drav.find(name), drav.select(ids), print(...). Ctrl+Enter runs.")
            color: Theme.textDim
            font.pixelSize: Theme.fontSize
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                id: code
                font.family: "monospace"
                font.pixelSize: 12
                color: "#d4d4d4"
                selectByMouse: true
                text: "for (const id of drav.selection())\n    print(drav.get(id).name)\n"
                background: Rectangle { color: "#1e1e1e"; radius: 6 }
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                        output.text = editor.runScript(code.text)
                        event.accepted = true
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTr("Run"); onClicked: output.text = editor.runScript(code.text) }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Open plugins folder"); onClicked: Qt.openUrlExternally("file://" + editor.pluginsFolder()) }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            TextArea {
                id: output
                readOnly: true
                font.family: "monospace"
                font.pixelSize: 11
                color: "#b3b3b3"
                wrapMode: TextEdit.Wrap
                background: Rectangle { color: "#181818"; radius: 6 }
            }
        }
    }
}
