import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Lists variables of one type, grouped by collection. Emits picked(id); "" means detach.
Popup {
    id: root
    property string varType: "COLOR" // COLOR, FLOAT, STRING, BOOLEAN
    property string current          // bound variable id, if any
    property string exclude          // variable id to hide (aliasing a variable to itself)
    property string filter
    signal picked(string id)
    padding: 6
    width: 240
    height: Math.min(380, list.contentHeight + 90)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: "#1e1e1e"; radius: 10; border.color: "#3a3a3a" }
    onOpened: { filter = ""; search.text = "" }

    readonly property var rows: {
        const lib = editor.library
        const out = []
        for (const c of lib.collections) {
            const vs = lib.variables.filter(v => v.collection === c.id && v.type === root.varType && v.id !== root.exclude
                                             && v.name.toLowerCase().indexOf(root.filter) >= 0)
            if (!vs.length) continue
            out.push({ header: true, name: c.name })
            for (const v of vs) out.push({ header: false, id: v.id, name: v.name, preview: editor.resolveVariable(v.id) })
        }
        return out
    }

    contentItem: ColumnLayout {
        spacing: 4
        TextBox {
            id: search
            Layout.fillWidth: true
            placeholder: qsTr("Search variables")
            onTextChanged: root.filter = text.toLowerCase()
        }
        Text {
            visible: root.rows.length === 0
            Layout.fillWidth: true
            Layout.margins: 6
            wrapMode: Text.WordWrap
            text: qsTr("No matching variables. Create them in Local variables.")
            color: Theme.textDim
            font.pixelSize: Theme.fontSize
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.rows
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: modelData.header ? 24 : 28
                radius: 4
                color: !modelData.header && hover.hovered ? Theme.accent : "transparent"
                HoverHandler { id: hover }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 6
                    Swatch {
                        visible: !modelData.header && root.varType === "COLOR"
                        paint: ({ type: "SOLID", color: modelData.preview || "transparent", opacity: 1 })
                    }
                    Icon { visible: !modelData.header && root.varType !== "COLOR"; name: "variable"; size: 12; color: Theme.textDim }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.name
                        color: modelData.header ? Theme.textFaint : "white"
                        font.pixelSize: Theme.fontSize
                        font.weight: modelData.header ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: !modelData.header && root.varType !== "COLOR"
                        text: String(modelData.preview)
                        color: Theme.textDim
                        font.pixelSize: 10
                    }
                    Icon { visible: !modelData.header && modelData.id === root.current; name: "check"; size: 12 }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !modelData.header
                    onClicked: { root.picked(modelData.id); root.close() }
                }
            }
        }
        Button {
            visible: root.current.length > 0
            Layout.fillWidth: true
            text: qsTr("Detach variable")
            onClicked: { root.picked(""); root.close() }
        }
    }
}
