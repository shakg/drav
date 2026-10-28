import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Drop/inner shadows and blurs.
Section {
    id: root
    title: qsTr("Effects")
    showAdd: true
    actions: [
        IconButton { iconName: "style"; iconSize: 14; tip: qsTr("Effect styles"); onClicked: { fxStyles.current = editor.props.effectStyle || ""; fxStyles.open() }
            StylePicker { id: fxStyles; slot: "effects"; x: -width - 20 } }
    ]
    property var effects: editor.props.effects
    readonly property bool mixed: effects === undefined || effects === null
    function copyList() { return (effects || []).map(e => Object.assign({}, e)) }
    function update(l, live) { live ? editor.setPropLive("effects", l) : editor.setProp("effects", l) }
    onAdd: {
        const l = mixed ? [] : copyList()
        l.push({ type: "DROP_SHADOW", visible: true, color: Qt.rgba(0, 0, 0, 0.25), x: 0, y: 4, radius: 4, spread: 0, behind: false })
        update(l, false)
    }
    StyleChip { slot: "effects"; styleId: editor.props.effectStyle || "" }
    Text {
        visible: root.mixed
        text: qsTr("Click + to replace mixed content")
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
    }
    Repeater {
        model: root.mixed ? [] : root.effects
        delegate: ColumnLayout {
            id: fx
            required property var modelData
            required property int index
            Layout.fillWidth: true
            readonly property bool shadow: modelData.type === "DROP_SHADOW" || modelData.type === "INNER_SHADOW"
            function set(k, v, live) { const l = root.copyList(); l[index][k] = v; root.update(l, live) }
            RowLayout {
                Layout.fillWidth: true
                IconButton { iconName: "settings"; checked: details.visible; onClicked: details.visible = !details.visible; tip: qsTr("Effect settings") }
                Dropdown {
                    Layout.fillWidth: true
                    current: fx.modelData.type
                    model: [{ value: "DROP_SHADOW", text: qsTr("Drop shadow") }, { value: "INNER_SHADOW", text: qsTr("Inner shadow") },
                        { value: "LAYER_BLUR", text: qsTr("Layer blur") }, { value: "BACKGROUND_BLUR", text: qsTr("Background blur") }]
                    onPicked: v => fx.set("type", v, false)
                }
                IconButton { iconName: fx.modelData.visible ? "eye" : "eyeOff"; onClicked: fx.set("visible", !fx.modelData.visible, false) }
                IconButton { iconName: "minus"; onClicked: { const l = root.copyList(); l.splice(fx.index, 1); root.update(l, false) } }
            }
            GridLayout {
                id: details
                visible: false
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 6
                rowSpacing: 4
                NumberField { Layout.fillWidth: true; visible: fx.shadow; label: "X"; value: fx.modelData.x
                    onCommitted: v => fx.set("x", v, false); onLive: v => fx.set("x", v, true); onLiveEnd: editor.endLive() }
                NumberField { Layout.fillWidth: true; visible: fx.shadow; label: "Y"; value: fx.modelData.y
                    onCommitted: v => fx.set("y", v, false); onLive: v => fx.set("y", v, true); onLiveEnd: editor.endLive() }
                NumberField { Layout.fillWidth: true; label: "B"; value: fx.modelData.radius; minimum: 0
                    onCommitted: v => fx.set("radius", v, false); onLive: v => fx.set("radius", v, true); onLiveEnd: editor.endLive() }
                NumberField { Layout.fillWidth: true; visible: fx.shadow; label: "S"; value: fx.modelData.spread
                    onCommitted: v => fx.set("spread", v, false); onLive: v => fx.set("spread", v, true); onLiveEnd: editor.endLive() }
                RowLayout {
                    visible: fx.shadow
                    Layout.columnSpan: 2
                    Swatch {
                        paint: ({ type: "SOLID", color: fx.modelData.color, opacity: 1 })
                        MouseArea {
                            anchors.fill: parent
                            onClicked: { colorPopup.paint = { type: "SOLID", color: fx.modelData.color, opacity: 1, blend: "NORMAL" }; colorPopup.open() }
                        }
                        PaintPopup {
                            id: colorPopup
                            x: -width - 30
                            allowImage: false
                            onEdited: (p, live) => { colorPopup.paint = p; fx.set("color", p.color, live) }
                            onClosed: editor.endLive()
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        readonly property var v: fx.modelData.colorVar ? editor.library.variables.find(x => x.id === fx.modelData.colorVar) : null
                        text: v ? v.name : qsTr("Color")
                        color: v ? Theme.text : Theme.textDim
                        font.pixelSize: Theme.fontSize
                    }
                    IconButton {
                        iconName: "variable"; iconSize: 12
                        checked: !!fx.modelData.colorVar
                        tip: qsTr("Apply color variable")
                        onClicked: { fxVar.current = fx.modelData.colorVar || ""; fxVar.open() }
                        VariablePicker { id: fxVar; x: -width - 20; varType: "COLOR"; onPicked: id => editor.bindVariable("effects/" + fx.index, id) }
                    }
                    CheckBox {
                        visible: fx.modelData.type === "DROP_SHADOW"
                        text: qsTr("Behind transparent")
                        checked: fx.modelData.behind
                        onToggled: fx.set("behind", checked, false)
                        contentItem: Text { leftPadding: 22; text: parent.text; color: Theme.textDim; font.pixelSize: Theme.fontSize; verticalAlignment: Text.AlignVCenter }
                    }
                }
            }
        }
    }
}
