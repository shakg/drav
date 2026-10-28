import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Edits one local style: name, description and its values.
Popup {
    id: root
    property string styleId
    readonly property var style: editor.library.styles.find(s => s.id === styleId) || null
    padding: 12
    width: 260
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: "#1e1e1e"; radius: 10; border.color: "#3a3a3a" }
    onClosed: editor.endLive()

    function paints() { return (style.paints || []).map(p => Object.assign({}, p, { stops: (p.stops || []).map(s => ({ pos: s.pos, color: s.color })) })) }

    contentItem: ColumnLayout {
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            StylePreview { style: root.style }
            TextBox {
                Layout.fillWidth: true
                text: root.style ? root.style.name : ""
                onAccepted: t => editor.editStyle(root.styleId, { name: t })
            }
        }
        TextBox {
            Layout.fillWidth: true
            placeholder: qsTr("Description")
            text: root.style ? root.style.description : ""
            onAccepted: t => editor.editStyle(root.styleId, { description: t })
        }

        // Fill style: one row per paint.
        Repeater {
            model: root.style && root.style.type === "FILL" ? root.style.paints.slice().reverse() : []
            RowLayout {
                id: paintRow
                required property var modelData
                required property int index
                readonly property int realIndex: root.style.paints.length - 1 - index
                Layout.fillWidth: true
                Swatch {
                    paint: paintRow.modelData
                    MouseArea {
                        anchors.fill: parent
                        onClicked: { paintPopup.paintIndex = paintRow.realIndex; paintPopup.paint = paintRow.modelData; paintPopup.open() }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: paintRow.modelData.type === "SOLID" ? paintPopup.hex(paintRow.modelData.color) : paintRow.modelData.type.toLowerCase().replace("gradient_", "")
                    color: Theme.text
                    font.pixelSize: Theme.fontSize
                }
                IconButton {
                    iconName: "minus"
                    enabled: root.style.paints.length > 1
                    onClicked: { const l = root.paints(); l.splice(paintRow.realIndex, 1); editor.editStyle(root.styleId, { paints: l }) }
                }
            }
        }
        PaintPopup {
            id: paintPopup
            property int paintIndex: -1
            x: -width - 20
            allowImage: false
            onEdited: (p, live) => {
                const l = root.paints()
                l[paintIndex] = p
                paintPopup.paint = p
                editor.editStyle(root.styleId, { paints: l }, live)
            }
        }

        // Text style: typography.
        GridLayout {
            visible: !!root.style && root.style.type === "TEXT"
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 6
            rowSpacing: 4
            readonly property var t: root.style && root.style.text ? root.style.text : ({})
            Dropdown {
                Layout.fillWidth: true
                Layout.columnSpan: 2
                current: parent.t.fontFamily
                model: editor.fontFamilies
                onPicked: v => editor.editStyle(root.styleId, { text: { fontFamily: v } })
            }
            Dropdown {
                Layout.fillWidth: true
                current: parent.t.fontWeight
                model: [100, 200, 300, 400, 500, 600, 700, 800, 900].map(w => ({ value: w, text: String(w) }))
                onPicked: v => editor.editStyle(root.styleId, { text: { fontWeight: v } })
            }
            NumberField { Layout.fillWidth: true; label: "S"; value: parent.t.fontSize; minimum: 1; decimals: 1
                onCommitted: v => editor.editStyle(root.styleId, { text: { fontSize: v } }) }
            NumberField { Layout.fillWidth: true; icon: "autoHeight"; value: parent.t.lineHeight === 0 ? undefined : parent.t.lineHeight
                emptyText: qsTr("Auto"); minimum: 0
                onCommitted: v => editor.editStyle(root.styleId, { text: { lineHeight: v } }) }
            NumberField { Layout.fillWidth: true; icon: "autoWidth"; value: parent.t.letterSpacing; suffix: "%"; step: 0.5
                onCommitted: v => editor.editStyle(root.styleId, { text: { letterSpacing: v } }) }
        }

        Text {
            visible: !!root.style && (root.style.type === "EFFECT" || root.style.type === "GRID")
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: !root.style ? "" : root.style.type === "EFFECT" ? qsTr("%n effect(s)", "", root.style.effects.length)
                                                                  : qsTr("%n grid(s)", "", root.style.grids.length)
            color: Theme.textDim
            font.pixelSize: Theme.fontSize
        }

        Text {
            Layout.fillWidth: true
            text: root.style ? qsTr("Used by %n layer(s)", "", root.style.users) : ""
            color: Theme.textFaint
            font.pixelSize: 10
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 4
            rowSpacing: 4
            Button { Layout.fillWidth: true; text: qsTr("Update from selection"); enabled: editor.selectionCount > 0; onClicked: editor.updateStyleFromSelection(root.styleId) }
            Button { Layout.fillWidth: true; text: qsTr("Select layers"); onClicked: editor.selectStyleUsers(root.styleId) }
            Button { Layout.fillWidth: true; text: qsTr("Duplicate"); onClicked: editor.duplicateStyle(root.styleId) }
            Button { Layout.fillWidth: true; text: qsTr("Delete"); onClicked: { root.close(); editor.deleteStyle(root.styleId) } }
        }
    }
}
