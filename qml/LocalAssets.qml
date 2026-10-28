import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Shown in the inspector when nothing is selected: local variables and styles of the document.
ColumnLayout {
    id: root
    spacing: 0
    Layout.fillWidth: true

    Section {
        id: missing
        readonly property var fonts: { editor.library; editor.undoChanged; return editor.missingFonts() }
        visible: fonts.length > 0
        title: qsTr("Missing fonts")
        Repeater {
            model: missing.fonts
            RowLayout {
                required property string modelData
                Layout.fillWidth: true
                Icon { name: "warning"; size: 13; color: Theme.danger }
                Text { text: modelData; color: Theme.text; font.pixelSize: Theme.fontSize; Layout.preferredWidth: 80; elide: Text.ElideRight }
                Dropdown {
                    Layout.fillWidth: true
                    current: null
                    displayText: qsTr("Replace with…")
                    model: editor.fontFamilies
                    onPicked: v => editor.replaceFont(modelData, v)
                }
            }
        }
    }

    Section {
        title: qsTr("Local variables")
        RowLayout {
            Layout.fillWidth: true
            Icon { name: "variable"; size: 14; color: Theme.textDim }
            Text {
                Layout.fillWidth: true
                readonly property int nv: editor.library.variables.length
                readonly property int nc: editor.library.collections.length
                text: (nv === 1 ? qsTr("1 variable") : qsTr("%1 variables").arg(nv)) + " · " + (nc === 1 ? qsTr("1 collection") : qsTr("%1 collections").arg(nc))
                color: Theme.textDim
                font.pixelSize: Theme.fontSize
            }
        }
        Button {
            Layout.fillWidth: true
            text: qsTr("Open variables")
            onClicked: variablesWindow.show()
        }
    }

    Repeater {
        model: [{ type: "TEXT", title: qsTr("Text styles") }, { type: "FILL", title: qsTr("Color styles") },
            { type: "EFFECT", title: qsTr("Effect styles") }, { type: "GRID", title: qsTr("Grid styles") }]
        Section {
            id: group
            required property var modelData
            readonly property var styles: editor.library.styles.filter(s => s.type === modelData.type)
            visible: styles.length > 0
            title: modelData.title
            Repeater {
                model: group.styles
                Rectangle {
                    id: styleRow
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 28
                    radius: 5
                    color: rowHover.hovered ? Theme.field : "transparent"
                    HoverHandler { id: rowHover }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        spacing: 8
                        StylePreview { style: styleRow.modelData }
                        Text {
                            Layout.fillWidth: true
                            text: styleRow.modelData.name
                            color: Theme.text
                            font.pixelSize: Theme.fontSize
                            elide: Text.ElideRight
                        }
                        Text { text: styleRow.modelData.users; color: Theme.textFaint; font.pixelSize: 10; rightPadding: 8 }
                    }
                    MouseArea { anchors.fill: parent; onClicked: styleEditor.open() }
                    StyleEditor { id: styleEditor; styleId: styleRow.modelData.id; x: -width - 30 }
                }
            }
        }
    }

    VariablesWindow { id: variablesWindow; objectName: "variablesWindow" }
}
