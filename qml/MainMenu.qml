import QtQuick
import QtQuick.Controls.Basic

// Application menu (opened from the top-left button).
AppMenu {
    id: root
    signal openRequested()
    signal saveAsRequested()
    signal importRequested()
    signal exportRequested()
    signal versionsRequested()
    signal findRequested()
    signal consoleRequested()

    AppMenu {
        id: pluginsMenu
        title: qsTr("Plugins")
        property var list: []
        onAboutToShow: list = editor.plugins()
        Instantiator {
            model: pluginsMenu.list
            delegate: AppMenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: editor.runPlugin(modelData.path)
            }
            onObjectAdded: (i, o) => pluginsMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => pluginsMenu.removeItem(o)
        }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Script console…"); onTriggered: root.consoleRequested() }
        AppMenuItem { text: qsTr("Open plugins folder"); onTriggered: Qt.openUrlExternally("file://" + editor.pluginsFolder()) }
    }

    AppMenu {
        title: qsTr("File")
        AppMenuItem { text: qsTr("New"); shortcut: "Ctrl+N"; onTriggered: editor.newDocument() }
        AppMenuItem { text: qsTr("Open…"); shortcut: "Ctrl+O"; onTriggered: root.openRequested() }
        AppMenuItem { text: qsTr("Save"); shortcut: "Ctrl+S"; onTriggered: if (!editor.save()) root.saveAsRequested() }
        AppMenuItem { text: qsTr("Save as…"); shortcut: "Ctrl+Shift+S"; onTriggered: root.saveAsRequested() }
        AppMenu {
            id: recentMenu
            title: qsTr("Open recent")
            enabled: editor.recentFiles.length > 0
            Instantiator {
                model: editor.recentFiles
                delegate: AppMenuItem {
                    required property string modelData
                    text: modelData.substring(modelData.lastIndexOf("/") + 1)
                    onTriggered: editor.open("file://" + modelData)
                }
                onObjectAdded: (i, o) => recentMenu.insertItem(i, o)
                onObjectRemoved: (i, o) => recentMenu.removeItem(o)
            }
            AppMenuSeparator {}
            AppMenuItem { text: qsTr("Clear recent"); onTriggered: editor.clearRecentFiles() }
        }
        AppMenuItem { text: qsTr("Version history…"); onTriggered: root.versionsRequested() }
        AppMenu {
            title: qsTr("Color profile")
            // Assigning keeps the color values; converting keeps how colors look.
            AppMenuItem { text: (editor.colorProfile === "srgb" ? "✓ " : "") + qsTr("sRGB"); onTriggered: editor.setColorProfile("srgb", false) }
            AppMenuItem { text: (editor.colorProfile === "display-p3" ? "✓ " : "") + qsTr("Display P3"); onTriggered: editor.setColorProfile("display-p3", false) }
            AppMenuSeparator {}
            AppMenuItem {
                text: editor.colorProfile === "srgb" ? qsTr("Convert colors to Display P3") : qsTr("Convert colors to sRGB")
                onTriggered: editor.setColorProfile(editor.colorProfile === "srgb" ? "display-p3" : "srgb", true)
            }
        }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Place image / SVG…"); shortcut: "Ctrl+Shift+K"; onTriggered: root.importRequested() }
        AppMenuItem { text: qsTr("Export…"); shortcut: "Ctrl+Shift+E"; onTriggered: root.exportRequested() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Open showcase file"); onTriggered: editor.loadDemo() }
        AppMenuItem { text: qsTr("Quit"); shortcut: "Ctrl+Q"; onTriggered: Qt.quit() }
    }
    AppMenu {
        title: qsTr("Edit")
        AppMenuItem { text: qsTr("Undo"); shortcut: "Ctrl+Z"; enabled: editor.canUndo; onTriggered: editor.undo() }
        AppMenuItem { text: qsTr("Redo"); shortcut: "Ctrl+Shift+Z"; enabled: editor.canRedo; onTriggered: editor.redo() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Copy"); shortcut: "Ctrl+C"; onTriggered: editor.copy() }
        AppMenuItem { text: qsTr("Cut"); shortcut: "Ctrl+X"; onTriggered: editor.cut() }
        AppMenuItem { text: qsTr("Paste"); shortcut: "Ctrl+V"; onTriggered: editor.paste() }
        AppMenuItem { text: qsTr("Paste in place"); shortcut: "Ctrl+Shift+V"; onTriggered: editor.pasteInPlace() }
        AppMenuItem { text: qsTr("Duplicate"); shortcut: "Ctrl+D"; onTriggered: editor.duplicate() }
        AppMenuItem { text: qsTr("Delete"); shortcut: "Del"; onTriggered: editor.deleteSelection() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Select all"); shortcut: "Ctrl+A"; onTriggered: editor.selectAll() }
        AppMenuItem { text: qsTr("Select none"); shortcut: "Esc"; onTriggered: editor.selectNone() }
        AppMenuItem { text: qsTr("Select matching layers"); shortcut: "Ctrl+Alt+A"; onTriggered: editor.selectSame("matching") }
        AppMenuItem { text: qsTr("Find and replace…"); shortcut: "Ctrl+F"; onTriggered: root.findRequested() }
    }
    AppMenu {
        title: qsTr("View")
        AppMenuItem { text: qsTr("Zoom in"); shortcut: "Ctrl++"; onTriggered: editor.zoomIn() }
        AppMenuItem { text: qsTr("Zoom out"); shortcut: "Ctrl+-"; onTriggered: editor.zoomOut() }
        AppMenuItem { text: qsTr("Zoom to 100%"); shortcut: "Shift+0"; onTriggered: editor.zoomTo(1) }
        AppMenuItem { text: qsTr("Zoom to fit"); shortcut: "Shift+1"; onTriggered: editor.zoomToFit() }
        AppMenuItem { text: qsTr("Zoom to selection"); shortcut: "Shift+2"; onTriggered: editor.zoomToSelection() }
        AppMenuSeparator {}
        AppMenuItem { text: (editor.outlines ? "✓ " : "") + qsTr("Outlines"); shortcut: "Ctrl+Y"; onTriggered: editor.outlines = !editor.outlines }
        AppMenuItem { text: (editor.showGrids ? "✓ " : "") + qsTr("Layout grids"); onTriggered: editor.showGrids = !editor.showGrids }
        AppMenuItem { text: (editor.snapToPixels ? "✓ " : "") + qsTr("Snap to pixel grid"); onTriggered: editor.snapToPixels = !editor.snapToPixels }
        AppMenuItem { text: (editor.rulers ? "✓ " : "") + qsTr("Rulers"); shortcut: "Shift+R"; onTriggered: editor.rulers = !editor.rulers }
    }
    AppMenu {
        title: qsTr("Object")
        AppMenuItem { text: qsTr("Group selection"); shortcut: "Ctrl+G"; onTriggered: editor.group() }
        AppMenuItem { text: qsTr("Ungroup"); shortcut: "Ctrl+Shift+G"; onTriggered: editor.ungroup() }
        AppMenuItem { text: qsTr("Frame selection"); shortcut: "Ctrl+Alt+G"; onTriggered: editor.frameSelection() }
        AppMenuItem { text: qsTr("Add auto layout"); shortcut: "Shift+A"; onTriggered: editor.addAutoLayout() }
        AppMenuItem { text: qsTr("Remove auto layout"); onTriggered: editor.removeAutoLayout() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Create component"); shortcut: "Ctrl+Alt+K"; onTriggered: editor.createComponent() }
        AppMenuItem { text: qsTr("Combine as variants"); onTriggered: editor.combineAsVariants() }
        AppMenuItem { text: qsTr("Detach instance"); shortcut: "Ctrl+Alt+B"; onTriggered: editor.detachInstance() }
        AppMenuItem { text: qsTr("Reset overrides"); onTriggered: editor.resetOverrides() }
        AppMenuItem { text: qsTr("Go to main component"); onTriggered: editor.goToMainComponent() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Bring to front"); shortcut: "Ctrl+Shift+]"; onTriggered: editor.bringToFront() }
        AppMenuItem { text: qsTr("Bring forward"); shortcut: "Ctrl+]"; onTriggered: editor.bringForward() }
        AppMenuItem { text: qsTr("Send backward"); shortcut: "Ctrl+["; onTriggered: editor.sendBackward() }
        AppMenuItem { text: qsTr("Send to back"); shortcut: "Ctrl+Shift+["; onTriggered: editor.sendToBack() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Flip horizontal"); shortcut: "Shift+H"; onTriggered: editor.flip(true) }
        AppMenuItem { text: qsTr("Flip vertical"); shortcut: "Shift+V"; onTriggered: editor.flip(false) }
        AppMenuItem { text: qsTr("Rotate 90° right"); onTriggered: editor.rotateSelection(90) }
        AppMenuItem { text: qsTr("Rotate 90° left"); onTriggered: editor.rotateSelection(-90) }
        AppMenuItem { text: qsTr("Scale tool"); shortcut: "K"; onTriggered: editor.tool = "scale" }
        AppMenuItem { text: qsTr("Pick color"); shortcut: "I"; onTriggered: editor.pickColor() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Union"); onTriggered: editor.booleanOp("union") }
        AppMenuItem { text: qsTr("Subtract"); onTriggered: editor.booleanOp("subtract") }
        AppMenuItem { text: qsTr("Intersect"); onTriggered: editor.booleanOp("intersect") }
        AppMenuItem { text: qsTr("Exclude"); onTriggered: editor.booleanOp("exclude") }
        AppMenuItem { text: qsTr("Flatten"); shortcut: "Ctrl+E"; onTriggered: editor.flatten() }
        AppMenuItem { text: qsTr("Outline stroke"); shortcut: "Ctrl+Shift+O"; onTriggered: editor.outlineStroke() }
        AppMenuItem { text: qsTr("Use as mask"); shortcut: "Ctrl+Alt+M"; onTriggered: editor.useAsMask() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Show/Hide"); shortcut: "Ctrl+Shift+H"; onTriggered: editor.toggleVisible() }
        AppMenuItem { text: qsTr("Lock/Unlock"); shortcut: "Ctrl+Shift+L"; onTriggered: editor.toggleLocked() }
    }
}
