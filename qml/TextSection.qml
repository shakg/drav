import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Typography controls for text layers.
Section {
    id: root
    title: qsTr("Text")
    readonly property var p: editor.props
    actions: [
        IconButton {
            iconName: "variable"; iconSize: 13
            checked: !!root.p.bindings && !!root.p.bindings.characters
            tip: qsTr("Bind text content to a string variable")
            onClicked: { textVar.current = root.p.bindings ? (root.p.bindings.characters || "") : ""; textVar.open() }
            VariablePicker { id: textVar; x: -width - 20; varType: "STRING"; onPicked: id => editor.bindVariable("characters", id) }
        },
        IconButton {
            iconName: "style"; iconSize: 14
            tip: qsTr("Text styles")
            onClicked: { textStyles.current = root.p.textStyleId || ""; textStyles.open() }
            StylePicker { id: textStyles; slot: "text"; x: -width - 20 }
        }
    ]
    StyleChip { slot: "text"; styleId: root.p.textStyleId || "" }
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
        NumberField { Layout.preferredWidth: 70; key: "fontSize"; bindKey: "fontSize"; value: root.p.fontSize; minimum: 1; decimals: 1 }
    }
    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 6
        rowSpacing: 4
        NumberField {
            Layout.fillWidth: true
            icon: "autoHeight"
            bindKey: "lineHeight"
            value: root.p.lineHeight === 0 ? undefined : root.p.lineHeight
            emptyText: qsTr("Auto")
            suffix: root.p.lineHeightPercent ? "%" : ""
            minimum: 0
            onCommitted: v => editor.setProp("lineHeight", v)
        }
        NumberField { Layout.fillWidth: true; icon: "autoWidth"; key: "letterSpacing"; bindKey: "letterSpacing"; value: root.p.letterSpacing; suffix: "%"; step: 0.5 }
        NumberField { Layout.fillWidth: true; label: "¶"; key: "paragraphSpacing"; bindKey: "paragraphSpacing"; value: root.p.paragraphSpacing; minimum: 0 }
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
        IconButton { iconName: "truncate"; tip: qsTr("Truncate"); checked: root.p.autoResize === "TRUNCATE"; onClicked: editor.setProp("autoResize", "TRUNCATE") }
        Item { width: 8 }
        IconButton { iconName: "underline"; tip: qsTr("Underline"); checked: root.p.decoration === "UNDERLINE"; onClicked: editor.setProp("decoration", root.p.decoration === "UNDERLINE" ? "NONE" : "UNDERLINE") }
        IconButton { iconName: "strike"; tip: qsTr("Strikethrough"); checked: root.p.decoration === "STRIKETHROUGH"; onClicked: editor.setProp("decoration", root.p.decoration === "STRIKETHROUGH" ? "NONE" : "STRIKETHROUGH") }
        IconButton { iconName: "italic"; tip: qsTr("Italic"); checked: root.p.italic === true; onClicked: editor.setProp("italic", !root.p.italic) }
    }
    RowLayout {
        spacing: 2
        IconButton { iconName: "listBullet"; tip: qsTr("Bulleted list"); checked: root.p.listType === "BULLET"
            onClicked: editor.setProp("listType", root.p.listType === "BULLET" ? "NONE" : "BULLET") }
        IconButton { iconName: "listOrdered"; tip: qsTr("Numbered list"); checked: root.p.listType === "ORDERED"
            onClicked: editor.setProp("listType", root.p.listType === "ORDERED" ? "NONE" : "ORDERED") }
        Item { Layout.fillWidth: true }
        IconButton { iconName: "settings"; tip: qsTr("Type details"); checked: details.visible; onClicked: details.visible = !details.visible }
    }
    // Link on the selected characters (while editing text).
    RowLayout {
        visible: !!root.p.textRange && root.p.textRange[0] !== root.p.textRange[1]
        Layout.fillWidth: true
        Icon { name: "link"; size: 13; color: Theme.textDim }
        TextBox {
            Layout.fillWidth: true
            placeholder: qsTr("Link URL for selected text")
            text: root.p.link || ""
            onAccepted: t => editor.setProp("link", t)
        }
    }
    // OpenType features and variable axes.
    ColumnLayout {
        id: details
        visible: false
        Layout.fillWidth: true
        spacing: 2
        readonly property var feats: root.p.features || ({})
        readonly property var known: [
            { tag: "liga", text: qsTr("Ligatures"), on: 1 }, { tag: "calt", text: qsTr("Contextual alternates"), on: 1 },
            { tag: "tnum", text: qsTr("Tabular numbers"), on: 0 }, { tag: "zero", text: qsTr("Slashed zero"), on: 0 },
            { tag: "frac", text: qsTr("Fractions"), on: 0 }, { tag: "case", text: qsTr("Case-sensitive forms"), on: 0 },
            { tag: "smcp", text: qsTr("Small caps"), on: 0 }, { tag: "ss01", text: qsTr("Stylistic set 1"), on: 0 },
            { tag: "ss02", text: qsTr("Stylistic set 2"), on: 0 }, { tag: "cv11", text: qsTr("Character variant 11"), on: 0 }]
        Repeater {
            model: details.known
            CheckBox {
                required property var modelData
                text: modelData.text
                checked: details.feats[modelData.tag] !== undefined ? details.feats[modelData.tag] !== 0 : modelData.on === 1
                // Store only values that differ from the font default.
                onToggled: editor.setProp("fontFeature", { tag: modelData.tag, value: (checked ? 1 : 0) === modelData.on ? null : (checked ? 1 : 0) })
                contentItem: Text { leftPadding: 22; text: parent.text; color: Theme.textDim; font.pixelSize: Theme.fontSize; verticalAlignment: Text.AlignVCenter }
                implicitHeight: 22
            }
        }
        Repeater {
            model: details.visible && root.p.fontFamily ? editor.fontAxes(root.p.fontFamily, root.p.fontWeight || 400, root.p.italic === true).filter(a => a.tag !== "wght") : []
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Text { text: modelData.tag; color: Theme.textDim; font.pixelSize: Theme.fontSize; Layout.preferredWidth: 40 }
                Slider {
                    Layout.fillWidth: true
                    from: modelData.min
                    to: modelData.max
                    value: root.p.axes && root.p.axes[modelData.tag] !== undefined ? root.p.axes[modelData.tag] : modelData.default
                    onMoved: editor.setPropLive("fontAxis", { tag: modelData.tag, value: value })
                    onPressedChanged: if (!pressed) editor.endLive()
                }
                Text { text: Math.round((root.p.axes && root.p.axes[modelData.tag] !== undefined ? root.p.axes[modelData.tag] : modelData.default) * 10) / 10; color: Theme.textFaint; font.pixelSize: 10 }
            }
        }
    }
}
