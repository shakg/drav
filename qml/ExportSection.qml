import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Export presets for the selection (or the whole page when nothing is selected).
Section {
    id: root
    title: qsTr("Export")
    showAdd: true
    readonly property var list: editor.props.count > 0 ? (editor.props.exports || []) : pageExports
    property var pageExports: [{ format: "PNG", scale: 1, suffix: "" }]
    function setList(l) {
        if (editor.props.count > 0) editor.setProp("exports", l)
        else pageExports = l
    }
    onAdd: { const l = list.slice(); l.push({ format: "PNG", scale: l.length ? 2 : 1, suffix: l.length ? "@2x" : "" }); setList(l) }

    Repeater {
        model: root.list
        RowLayout {
            required property var modelData
            required property int index
            Layout.fillWidth: true
            function set(k, v) { const l = root.list.map(x => Object.assign({}, x)); l[index][k] = v; root.setList(l) }
            Dropdown {
                Layout.preferredWidth: 70
                current: modelData.scale
                model: [0.5, 0.75, 1, 1.5, 2, 3, 4].map(s => ({ value: s, text: s + "x" }))
                onPicked: v => parent.set("scale", v)
            }
            TextBox { Layout.fillWidth: true; placeholder: qsTr("Suffix"); text: modelData.suffix; onAccepted: t => parent.set("suffix", t) }
            Dropdown {
                Layout.preferredWidth: 70
                current: modelData.format
                model: ["PNG", "JPG", "WEBP", "SVG", "PDF"]
                onPicked: v => parent.set("format", v)
            }
            IconButton { iconName: "minus"; onClicked: { const l = root.list.slice(); l.splice(index, 1); root.setList(l) } }
        }
    }
    Button {
        Layout.fillWidth: true
        visible: root.list.length > 0
        text: editor.props.count > 0 ? qsTr("Export %1").arg(editor.props.count === 1 ? (editor.props.name || "") : qsTr("%1 layers").arg(editor.props.count)) : qsTr("Export page")
        onClicked: {
            exportDialog.currentFile = "file:///" + editor.suggestedExportName(root.list[0].format)
            exportDialog.open()
        }
    }
    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        title: qsTr("Export")
        onAccepted: {
            const base = String(selectedFile).replace(/\.[a-zA-Z]+$/, "")
            for (const e of root.list)
                editor.exportSelection(base + (e.suffix || "") + "." + e.format.toLowerCase().replace("jpeg", "jpg"), e.format, e.scale)
        }
    }
}
