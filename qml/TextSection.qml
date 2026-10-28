import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Typography controls for text layers.
Section {
    id: root
    title: qsTr("Text")
    readonly property var p: editor.props
    readonly property var weights: [
        { value: 100, text: "Thin" }, { value: 200, text: "Extra Light" }, { value: 300, text: "Light" },
        { value: 400, text: "Regular" }, { value: 500, text: "Medium" }, { value: 600, text: "Semi Bold" },
        { value: 700, text: "Bold" }, { value: 800, text: "Extra Bold" }, { value: 900, text: "Black" }]

    // Font family with type-to-filter list.
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 28
        TextBox {
            id: family
            anchors.fill: parent
            text: root.p.fontFamily || ""
            onAccepted: t => { editor.setProp("fontFamily", t); familyPopup.close() }
            MouseArea { anchors.fill: parent; onClicked: { familyPopup.filter = ""; familyPopup.open() } }
        }
        Popup {
            id: familyPopup
            property string filter
            y: 30
            width: parent.width
            height: 300
            padding: 4
            background: Rectangle { color: "#1e1e1e"; radius: 8; border.color: "#3a3a3a" }
            contentItem: ColumnLayout {
                TextBox {
                    Layout.fillWidth: true
                    placeholder: qsTr("Search fonts")
                    onTextChanged: familyPopup.filter = text.toLowerCase()
                    Component.onCompleted: forceActiveFocus()
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: editor.fontFamilies.filter(f => f.toLowerCase().indexOf(familyPopup.filter) >= 0)
                    delegate: ItemDelegate {
                        required property string modelData
                        width: ListView.view.width
                        height: 26
                        contentItem: Text { text: modelData; color: "white"; font.pixelSize: 12; font.family: modelData; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { color: hovered ? Theme.accent : "transparent"; radius: 4 }
                        onClicked: { editor.setProp("fontFamily", modelData); familyPopup.close() }
                    }
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Dropdown {
            Layout.fillWidth: true
            current: root.p.fontWeight
            model: root.weights
            onPicked: v => editor.setProp("fontWeight", v)
        }
        NumberField { Layout.preferredWidth: 70; key: "fontSize"; value: root.p.fontSize; minimum: 1; decimals: 1 }
    }
    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 6
        rowSpacing: 4
        NumberField {
            Layout.fillWidth: true
            icon: "autoHeight"
            value: root.p.lineHeight === 0 ? undefined : root.p.lineHeight
            suffix: root.p.lineHeightPercent ? "%" : ""
            minimum: 0
            onCommitted: v => editor.setProp("lineHeight", v)
            Text { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: root.p.lineHeight === 0 ? qsTr("Auto") : ""; color: Theme.textDim; font.pixelSize: Theme.fontSize }
        }
        NumberField { Layout.fillWidth: true; icon: "autoWidth"; key: "letterSpacing"; value: root.p.letterSpacing; suffix: "%"; step: 0.5 }
        NumberField { Layout.fillWidth: true; label: "¶"; key: "paragraphSpacing"; value: root.p.paragraphSpacing; minimum: 0 }
        Dropdown {
            Layout.fillWidth: true
            current: root.p.textCase
            model: [{ value: "ORIGINAL", text: qsTr("As typed") }, { value: "UPPER", text: qsTr("UPPERCASE") },
                { value: "LOWER", text: qsTr("lowercase") }, { value: "TITLE", text: qsTr("Title Case") }]
            onPicked: v => editor.setProp("textCase", v)
        }
    }
    RowLayout {
        spacing: 2
        IconButton { iconName: "textLeft"; checked: root.p.textAlignH === "LEFT"; onClicked: editor.setProp("textAlignH", "LEFT") }
        IconButton { iconName: "textCenter"; checked: root.p.textAlignH === "CENTER"; onClicked: editor.setProp("textAlignH", "CENTER") }
        IconButton { iconName: "textRight"; checked: root.p.textAlignH === "RIGHT"; onClicked: editor.setProp("textAlignH", "RIGHT") }
        IconButton { iconName: "textJustify"; checked: root.p.textAlignH === "JUSTIFIED"; onClicked: editor.setProp("textAlignH", "JUSTIFIED") }
        Item { width: 8 }
        IconButton { iconName: "textTop"; checked: root.p.textAlignV === "TOP"; onClicked: editor.setProp("textAlignV", "TOP") }
        IconButton { iconName: "textMiddle"; checked: root.p.textAlignV === "CENTER"; onClicked: editor.setProp("textAlignV", "CENTER") }
        IconButton { iconName: "textBottom"; checked: root.p.textAlignV === "BOTTOM"; onClicked: editor.setProp("textAlignV", "BOTTOM") }
    }
    RowLayout {
        spacing: 2
        IconButton { iconName: "autoWidth"; tip: qsTr("Auto width"); checked: root.p.autoResize === "WIDTH_AND_HEIGHT"; onClicked: editor.setProp("autoResize", "WIDTH_AND_HEIGHT") }
        IconButton { iconName: "autoHeight"; tip: qsTr("Auto height"); checked: root.p.autoResize === "HEIGHT"; onClicked: editor.setProp("autoResize", "HEIGHT") }
        IconButton { iconName: "fixedSize"; tip: qsTr("Fixed size"); checked: root.p.autoResize === "NONE"; onClicked: editor.setProp("autoResize", "NONE") }
        IconButton { iconName: "minus"; tip: qsTr("Truncate"); checked: root.p.autoResize === "TRUNCATE"; onClicked: editor.setProp("autoResize", "TRUNCATE") }
        Item { width: 8 }
        IconButton { iconName: "text"; tip: qsTr("Underline"); checked: root.p.decoration === "UNDERLINE"; onClicked: editor.setProp("decoration", root.p.decoration === "UNDERLINE" ? "NONE" : "UNDERLINE") }
        IconButton { iconName: "minus"; tip: qsTr("Strikethrough"); checked: root.p.decoration === "STRIKETHROUGH"; onClicked: editor.setProp("decoration", root.p.decoration === "STRIKETHROUGH" ? "NONE" : "STRIKETHROUGH") }
        IconButton { iconName: "pencil"; tip: qsTr("Italic"); checked: root.p.italic === true; onClicked: editor.setProp("italic", !root.p.italic) }
    }
}
