import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Drav

ThemedWindow {
    id: window
    width: 1440
    height: 900
    visible: true
    title: (editor.dirty ? "• " : "") + editor.fileName + " — drav"

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
                            onVersionsRequested: versionsWindow.show()
                            onFindRequested: findBar.show()
                            onConsoleRequested: scriptConsole.show()
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
                        TextBox {
                            Layout.fillWidth: true
                            Layout.margins: 6
                            placeholder: qsTr("Search layers")
                            onTextChanged: editor.layers.filter = text
                        }
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
            FindBar {
                id: findBar
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: editor.rulers ? 28 : 10
                z: 5
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
                    Repeater {
                        model: [qsTr("Design"), qsTr("Inspect")]
                        Text {
                            required property string modelData
                            required property int index
                            text: modelData
                            color: rightTabs.currentIndex === index ? Theme.text : Theme.textDim
                            font.pixelSize: Theme.fontSize
                            font.weight: rightTabs.currentIndex === index ? Font.DemiBold : Font.Normal
                            rightPadding: 12
                            MouseArea { anchors.fill: parent; onClicked: rightTabs.currentIndex = index }
                        }
                    }
                    Item { Layout.fillWidth: true }
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
                StackLayout {
                    id: rightTabs
                    objectName: "rightTabs"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    DesignPanel { }
                    InspectPanel { }
                }
            }
        }
    }

    Connections {
        target: editor
        function onRequestFind() { findBar.show() }
        function onRequestBulkRename() { renameDialog.open() }
    }
    RenameDialog { id: renameDialog }
    VersionsWindow { id: versionsWindow; objectName: "versionsWindow" }
    ScriptConsole { id: scriptConsole; objectName: "scriptConsole" }

    // Offer to recover work left by a crash.
    Component.onCompleted: Qt.callLater(() => { recoveryDialog.files = editor.recoveryFiles(); if (recoveryDialog.files.length) recoveryDialog.open() })
    Popup {
        id: recoveryDialog
        objectName: "recoveryDialog"
        property var files: []
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 420
        padding: 16
        background: Rectangle { color: "#1e1e1e"; radius: 10; border.color: "#3a3a3a" }
        contentItem: ColumnLayout {
            spacing: 8
            Text { text: qsTr("Recover unsaved changes?"); color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("drav closed before these documents were saved.")
                color: Theme.textDim
                font.pixelSize: Theme.fontSize
            }
            Repeater {
                model: recoveryDialog.files
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { text: modelData.name; color: Theme.text; font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold }
                        Text { text: Qt.formatDateTime(modelData.time, "yyyy-MM-dd hh:mm") + (modelData.original ? " · " + modelData.original : ""); color: Theme.textFaint; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    }
                    Button { text: qsTr("Recover"); onClicked: { editor.recover(modelData.path); recoveryDialog.close() } }
                    Button { text: qsTr("Discard"); onClicked: { editor.discardRecovery(modelData.path); recoveryDialog.files = editor.recoveryFiles(); if (!recoveryDialog.files.length) recoveryDialog.close() } }
                }
            }
            Button { Layout.alignment: Qt.AlignRight; text: qsTr("Later"); onClicked: recoveryDialog.close() }
        }
    }

    // ---------------- Canvas context menu
    AppMenu {
        id: contextMenu
        AppMenuItem { text: qsTr("Copy"); onTriggered: editor.copy() }
        AppMenu {
            title: qsTr("Copy as")
            enabled: editor.selectionCount > 0
            AppMenuItem { text: qsTr("PNG"); onTriggered: editor.copyAs("PNG") }
            AppMenuItem { text: qsTr("SVG"); onTriggered: editor.copyAs("SVG") }
            AppMenuItem { text: qsTr("CSS"); onTriggered: editor.copyAs("CSS") }
        }
        AppMenuItem { text: qsTr("Paste here"); onTriggered: editor.paste() }
        AppMenuItem { text: qsTr("Duplicate"); enabled: editor.selectionCount > 0; onTriggered: editor.duplicate() }
        AppMenuItem { text: qsTr("Delete"); enabled: editor.selectionCount > 0; onTriggered: editor.deleteSelection() }
        AppMenuSeparator {}
        AppMenu {
            title: qsTr("Select")
            enabled: editor.selectionCount > 0
            AppMenuItem { text: qsTr("Matching layers"); onTriggered: editor.selectSame("matching") }
            AppMenuItem { text: qsTr("Same fill"); onTriggered: editor.selectSame("fill") }
            AppMenuItem { text: qsTr("Same stroke"); onTriggered: editor.selectSame("stroke") }
            AppMenuItem { text: qsTr("Same effect"); onTriggered: editor.selectSame("effect") }
            AppMenuItem { text: qsTr("Same font"); onTriggered: editor.selectSame("font") }
            AppMenuItem { text: qsTr("Same instance"); onTriggered: editor.selectSame("instance") }
        }
        AppMenuItem { text: qsTr("Rename…"); enabled: editor.selectionCount > 1; onTriggered: renameDialog.open() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Bring to front"); enabled: editor.selectionCount > 0; onTriggered: editor.bringToFront() }
        AppMenuItem { text: qsTr("Send to back"); enabled: editor.selectionCount > 0; onTriggered: editor.sendToBack() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Group selection"); enabled: editor.selectionCount > 0; onTriggered: editor.group() }
        AppMenuItem { text: qsTr("Frame selection"); enabled: editor.selectionCount > 0; onTriggered: editor.frameSelection() }
        AppMenuItem { text: qsTr("Ungroup"); enabled: editor.selectionCount > 0; onTriggered: editor.ungroup() }
        AppMenuItem { text: qsTr("Add auto layout"); enabled: editor.selectionCount > 0; onTriggered: editor.addAutoLayout() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Create component"); enabled: editor.selectionCount > 0; onTriggered: editor.createComponent() }
        AppMenuItem { text: qsTr("Combine as variants"); enabled: editor.selectionCount > 1; onTriggered: editor.combineAsVariants() }
        AppMenuItem { text: qsTr("Detach instance"); enabled: editor.props.isInstance === true; onTriggered: editor.detachInstance() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Flatten"); enabled: editor.selectionCount > 0; onTriggered: editor.flatten() }
        AppMenuItem { text: qsTr("Outline stroke"); enabled: editor.selectionCount > 0; onTriggered: editor.outlineStroke() }
        AppMenuItem { text: qsTr("Use as mask"); enabled: editor.selectionCount > 0; onTriggered: editor.useAsMask() }
        AppMenuItem { text: qsTr("Put text on path"); enabled: editor.selectionCount === 2; onTriggered: editor.attachTextToPath() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Show/Hide"); enabled: editor.selectionCount > 0; onTriggered: editor.toggleVisible() }
        AppMenuItem { text: qsTr("Lock/Unlock"); enabled: editor.selectionCount > 0; onTriggered: editor.toggleLocked() }
    }

    // ---------------- Dialogs
    FileDialog {
        id: openDialog
        title: qsTr("Open")
        nameFilters: [qsTr("drav files (*.drav)"), qsTr("Figma files (*.fig)"), qsTr("SVG (*.svg)"), qsTr("All files (*)")]
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
        nameFilters: [qsTr("Images, SVG and Figma files (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.svg *.fig)")]
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
            if (button === MessageDialog.Discard) { editor.discardChanges(); window.forceClose = true; window.close() }
            else if (button === MessageDialog.Save) {
                window.forceClose = true
                if (editor.save()) window.close(); else saveDialog.open()
            }
        }
    }
}
