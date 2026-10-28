import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Drav

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    visible: true
    color: Theme.bg
    title: (editor.dirty ? "• " : "") + editor.fileName + " — drav"
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize

    property bool forceClose: false
    // The canvas has its own `editor` property, so hand it the global through a differently named one.
    readonly property var appEditor: editor
    onClosing: close => {
        if (editor.dirty && !forceClose) {
            close.accepted = false
            unsavedDialog.open()
        }
    }

    // File shortcuts (canvas shortcuts are handled in C++ so they never steal keys while typing).
    Shortcut { sequences: [StandardKey.New]; onActivated: editor.newDocument() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: openDialog.open() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: if (!editor.save()) saveDialog.open() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: saveDialog.open() }
    Shortcut { sequence: "Ctrl+Shift+K"; onActivated: importDialog.open() }
    Shortcut { sequence: "Ctrl+Shift+E"; onActivated: { exportDialog.currentFile = "file:///" + editor.suggestedExportName("PNG"); exportDialog.open() } }
    Shortcut { sequences: [StandardKey.Quit]; onActivated: window.close() }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---------------- Left panel
        Rectangle {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            color: Theme.panel
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    IconButton {
                        iconName: "menu"
                        tip: qsTr("Menu")
                        onClicked: mainMenu.popup(this, 0, height)
                        MainMenu {
                            id: mainMenu
                            onOpenRequested: openDialog.open()
                            onSaveAsRequested: saveDialog.open()
                            onImportRequested: importDialog.open()
                            onExportRequested: { exportDialog.currentFile = "file:///" + editor.suggestedExportName("PNG"); exportDialog.open() }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: editor.fileName
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text { text: editor.dirty ? qsTr("Edited") : ""; color: Theme.textFaint; font.pixelSize: 10 }
                }
                TabBar {
                    id: leftTabs
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    background: Item {}
                    Repeater {
                        model: [qsTr("Layers"), qsTr("Assets")]
                        TabButton {
                            required property string modelData
                            width: implicitWidth
                            contentItem: Text {
                                text: modelData
                                color: parent.checked ? Theme.text : Theme.textDim
                                font.pixelSize: Theme.fontSize
                                font.weight: parent.checked ? Font.DemiBold : Font.Normal
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle { radius: 5; color: parent.checked ? Theme.hover : "transparent" }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.topMargin: 6; height: 1; color: Theme.panelBorder }
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: leftTabs.currentIndex
                    ColumnLayout {
                        spacing: 0
                        PagesList { Layout.fillWidth: true }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.panelBorder }
                        LayersPanel { Layout.fillWidth: true; Layout.fillHeight: true }
                    }
                    AssetsPanel { }
                }
            }
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.panelBorder }
        }

        // ---------------- Canvas
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            CanvasItem {
                id: canvas
                anchors.fill: parent
                editor: window.appEditor
                focus: true
                Component.onCompleted: forceActiveFocus()
                onContextMenuRequested: (x, y) => contextMenu.popup(canvas, x, y)
            }
            Toolbar {
                onImportRequested: importDialog.open()
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 16
            }
            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 10
                text: editor.status
                color: Theme.textDim
                font.pixelSize: Theme.fontSize
            }
            DropArea {
                anchors.fill: parent
                onDropped: drop => { for (const u of drop.urls) editor.importFile(u) }
            }
        }

        // ---------------- Right panel
        Rectangle {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            color: Theme.panel
            Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.panelBorder }
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 1
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    Layout.leftMargin: 16
                    Layout.rightMargin: 8
                    Text { text: qsTr("Design"); color: Theme.text; font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    Text {
                        text: editor.fps > 0 ? Math.round(editor.fps) + " fps" : ""
                        color: Theme.textFaint
                        font.pixelSize: 10
                    }
                    Dropdown {
                        Layout.preferredWidth: 70
                        current: -1
                        displayText: Math.round(editor.zoom * 100) + "%"
                        model: [{ value: 0, text: qsTr("Zoom to fit") }, { value: 0.5, text: "50%" }, { value: 1, text: "100%" }, { value: 2, text: "200%" }, { value: 4, text: "400%" }]
                        onPicked: v => v === 0 ? editor.zoomToFit() : editor.zoomTo(v)
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.panelBorder }
                DesignPanel { Layout.fillWidth: true; Layout.fillHeight: true }
            }
        }
    }

    // ---------------- Canvas context menu
    Menu {
        id: contextMenu
        MenuItem { text: qsTr("Copy"); onTriggered: editor.copy() }
        MenuItem { text: qsTr("Paste here"); onTriggered: editor.paste() }
        MenuItem { text: qsTr("Duplicate"); enabled: editor.selectionCount > 0; onTriggered: editor.duplicate() }
        MenuItem { text: qsTr("Delete"); enabled: editor.selectionCount > 0; onTriggered: editor.deleteSelection() }
        MenuSeparator {}
        MenuItem { text: qsTr("Bring to front"); enabled: editor.selectionCount > 0; onTriggered: editor.bringToFront() }
        MenuItem { text: qsTr("Send to back"); enabled: editor.selectionCount > 0; onTriggered: editor.sendToBack() }
        MenuSeparator {}
        MenuItem { text: qsTr("Group selection"); enabled: editor.selectionCount > 0; onTriggered: editor.group() }
        MenuItem { text: qsTr("Frame selection"); enabled: editor.selectionCount > 0; onTriggered: editor.frameSelection() }
        MenuItem { text: qsTr("Ungroup"); enabled: editor.selectionCount > 0; onTriggered: editor.ungroup() }
        MenuItem { text: qsTr("Add auto layout"); enabled: editor.selectionCount > 0; onTriggered: editor.addAutoLayout() }
        MenuSeparator {}
        MenuItem { text: qsTr("Create component"); enabled: editor.selectionCount > 0; onTriggered: editor.createComponent() }
        MenuItem { text: qsTr("Combine as variants"); enabled: editor.selectionCount > 1; onTriggered: editor.combineAsVariants() }
        MenuItem { text: qsTr("Detach instance"); enabled: editor.props.isInstance === true; onTriggered: editor.detachInstance() }
        MenuSeparator {}
        MenuItem { text: qsTr("Flatten"); enabled: editor.selectionCount > 0; onTriggered: editor.flatten() }
        MenuItem { text: qsTr("Outline stroke"); enabled: editor.selectionCount > 0; onTriggered: editor.outlineStroke() }
        MenuItem { text: qsTr("Use as mask"); enabled: editor.selectionCount > 0; onTriggered: editor.useAsMask() }
        MenuSeparator {}
        MenuItem { text: qsTr("Show/Hide"); enabled: editor.selectionCount > 0; onTriggered: editor.toggleVisible() }
        MenuItem { text: qsTr("Lock/Unlock"); enabled: editor.selectionCount > 0; onTriggered: editor.toggleLocked() }
    }

    // ---------------- Dialogs
    FileDialog {
        id: openDialog
        title: qsTr("Open")
        nameFilters: [qsTr("drav files (*.drav)"), qsTr("SVG (*.svg)"), qsTr("All files (*)")]
        onAccepted: editor.open(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "drav"
        nameFilters: [qsTr("drav files (*.drav)")]
        onAccepted: { editor.saveAs(selectedFile); if (window.forceClose) window.close() }
    }
    FileDialog {
        id: importDialog
        title: qsTr("Place image or SVG")
        nameFilters: [qsTr("Images and SVG (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.svg)")]
        onAccepted: editor.importFile(selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PNG (*.png)"), qsTr("JPG (*.jpg)"), qsTr("WEBP (*.webp)"), qsTr("SVG (*.svg)"), qsTr("PDF (*.pdf)")]
        onAccepted: {
            const s = String(selectedFile)
            const ext = s.substring(s.lastIndexOf(".") + 1).toUpperCase()
            editor.exportSelection(selectedFile, ["PNG", "JPG", "WEBP", "SVG", "PDF"].indexOf(ext) >= 0 ? ext : "PNG", 1)
        }
    }
    MessageDialog {
        id: unsavedDialog
        text: qsTr("Save changes to \"%1\" before closing?").arg(editor.fileName)
        buttons: MessageDialog.Save | MessageDialog.Discard | MessageDialog.Cancel
        onButtonClicked: (button, role) => {
            if (button === MessageDialog.Discard) { window.forceClose = true; window.close() }
            else if (button === MessageDialog.Save) {
                window.forceClose = true
                if (editor.save()) window.close(); else saveDialog.open()
            }
        }
    }
}
