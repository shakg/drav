import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Local components and components from linked libraries; click to insert an instance.
ColumnLayout {
    id: root
    spacing: 4
    readonly property string filter: search.text.toLowerCase()
    readonly property var local: editor.components.filter(c => !c.remote && (c.set + " " + c.name).toLowerCase().indexOf(filter) >= 0)
    // Flat rows: section headers, then components.
    readonly property var rows: {
        const out = []
        if (local.length) out.push({ header: true, name: qsTr("Local components") })
        for (const c of local) out.push({ header: false, lib: "", id: c.id, name: (c.set ? c.set + " / " : "") + c.name, page: c.page })
        for (const l of editor.libraries) {
            const comps = l.components.filter(c => (c.set + " " + c.name).toLowerCase().indexOf(filter) >= 0)
            if (!comps.length) continue
            out.push({ header: true, name: l.name })
            for (const c of comps) out.push({ header: false, lib: l.id, id: c.id, name: (c.set ? c.set + " / " : "") + c.name, page: "" })
        }
        return out
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 8
        TextBox { id: search; Layout.fillWidth: true; placeholder: qsTr("Search assets") }
        IconButton {
            iconName: "library"
            tip: qsTr("Libraries")
            checked: librariesPopup.visible
            onClicked: { editor.refreshLibraries(); librariesPopup.open() }
            Rectangle {
                visible: editor.libraries.some(l => l.updateAvailable)
                width: 7; height: 7; radius: 3.5
                color: Theme.accent
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 3
            }
        }
    }
    Text {
        visible: root.rows.length === 0
        Layout.margins: 16
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("No components yet. Select layers and press Ctrl+Alt+K to create one, or add a library.")
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
    }
    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: root.rows
        delegate: Rectangle {
            required property var modelData
            width: ListView.view.width
            height: modelData.header ? 28 : 32
            color: !modelData.header && hover.hovered ? Theme.hover : "transparent"
            HoverHandler { id: hover }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: modelData.header ? 12 : 16
                anchors.rightMargin: 8
                Icon { visible: !modelData.header; name: "component"; size: 14; color: Theme.component }
                Text {
                    Layout.fillWidth: true
                    text: modelData.name
                    color: modelData.header ? Theme.textDim : Theme.text
                    font.pixelSize: Theme.fontSize
                    font.weight: modelData.header ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }
                Text { visible: !modelData.header; text: modelData.page || ""; color: Theme.textFaint; font.pixelSize: 10 }
            }
            MouseArea {
                anchors.fill: parent
                enabled: !modelData.header
                onClicked: modelData.lib ? editor.placeLibraryComponent(modelData.lib, modelData.id) : editor.placeComponent(modelData.id)
            }
            ToolTip.visible: !modelData.header && hover.hovered
            ToolTip.delay: 800
            ToolTip.text: qsTr("Click to insert an instance")
        }
    }

    Popup {
        id: librariesPopup
        x: root.width + 8
        y: 8
        width: 320
        padding: 12
        background: Rectangle { color: "#1e1e1e"; radius: 10; border.color: "#3a3a3a" }
        contentItem: ColumnLayout {
            spacing: 8
            Text { text: qsTr("Libraries"); color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Any .drav file can be a library. Its styles and variables become available here, and its components can be inserted from Assets.")
                color: Theme.textDim
                font.pixelSize: Theme.fontSize
            }
            Repeater {
                model: editor.libraries
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Icon { name: modelData.missing ? "warning" : "library"; size: 14; color: modelData.missing ? Theme.danger : Theme.textDim }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { text: modelData.name; color: Theme.text; font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.missing ? qsTr("File not found: ") + modelData.path : modelData.path
                            color: modelData.missing ? Theme.danger : Theme.textFaint
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                        }
                    }
                    Button { visible: modelData.updateAvailable; text: qsTr("Update"); onClicked: editor.updateLibrary(modelData.id) }
                    IconButton { iconName: "minus"; tip: qsTr("Remove library (imported items stay as local copies)"); onClicked: editor.removeLibrary(modelData.id) }
                }
            }
            Button { Layout.fillWidth: true; text: qsTr("Add library…"); onClicked: libraryDialog.open() }
        }
    }
    FileDialog {
        id: libraryDialog
        title: qsTr("Add library")
        nameFilters: [qsTr("drav files (*.drav)")]
        onAccepted: editor.addLibrary(selectedFile)
    }
}
