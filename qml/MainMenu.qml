import QtQuick
import QtQuick.Controls.Basic

// Application menu (opened from the top-left button).
Menu {
    id: root
    signal openRequested()
    signal saveAsRequested()
    signal importRequested()
    signal exportRequested()

    component Item_: MenuItem {
        property string shortcut
        text: ""
        contentItem: Row {
            spacing: 8
            Text { text: parent.parent.text; color: parent.parent.enabled ? "white" : Theme.textFaint; font.pixelSize: 12; width: 180 }
            Text { text: parent.parent.shortcut; color: Theme.textFaint; font.pixelSize: 12 }
        }
    }

    Menu {
        title: qsTr("File")
        Item_ { text: qsTr("New"); shortcut: "Ctrl+N"; onTriggered: editor.newDocument() }
        Item_ { text: qsTr("Open…"); shortcut: "Ctrl+O"; onTriggered: root.openRequested() }
        Item_ { text: qsTr("Save"); shortcut: "Ctrl+S"; onTriggered: if (!editor.save()) root.saveAsRequested() }
        Item_ { text: qsTr("Save as…"); shortcut: "Ctrl+Shift+S"; onTriggered: root.saveAsRequested() }
        MenuSeparator {}
        Item_ { text: qsTr("Place image / SVG…"); shortcut: "Ctrl+Shift+K"; onTriggered: root.importRequested() }
        Item_ { text: qsTr("Export…"); shortcut: "Ctrl+Shift+E"; onTriggered: root.exportRequested() }
        MenuSeparator {}
        Item_ { text: qsTr("Open showcase file"); onTriggered: editor.loadDemo() }
        Item_ { text: qsTr("Quit"); shortcut: "Ctrl+Q"; onTriggered: Qt.quit() }
    }
    Menu {
        title: qsTr("Edit")
        Item_ { text: qsTr("Undo"); shortcut: "Ctrl+Z"; enabled: editor.canUndo; onTriggered: editor.undo() }
        Item_ { text: qsTr("Redo"); shortcut: "Ctrl+Shift+Z"; enabled: editor.canRedo; onTriggered: editor.redo() }
        MenuSeparator {}
        Item_ { text: qsTr("Copy"); shortcut: "Ctrl+C"; onTriggered: editor.copy() }
        Item_ { text: qsTr("Cut"); shortcut: "Ctrl+X"; onTriggered: editor.cut() }
        Item_ { text: qsTr("Paste"); shortcut: "Ctrl+V"; onTriggered: editor.paste() }
        Item_ { text: qsTr("Paste in place"); shortcut: "Ctrl+Shift+V"; onTriggered: editor.pasteInPlace() }
        Item_ { text: qsTr("Duplicate"); shortcut: "Ctrl+D"; onTriggered: editor.duplicate() }
        Item_ { text: qsTr("Delete"); shortcut: "Del"; onTriggered: editor.deleteSelection() }
        MenuSeparator {}
        Item_ { text: qsTr("Select all"); shortcut: "Ctrl+A"; onTriggered: editor.selectAll() }
        Item_ { text: qsTr("Select none"); shortcut: "Esc"; onTriggered: editor.selectNone() }
    }
    Menu {
        title: qsTr("View")
        Item_ { text: qsTr("Zoom in"); shortcut: "Ctrl++"; onTriggered: editor.zoomIn() }
        Item_ { text: qsTr("Zoom out"); shortcut: "Ctrl+-"; onTriggered: editor.zoomOut() }
        Item_ { text: qsTr("Zoom to 100%"); shortcut: "Shift+0"; onTriggered: editor.zoomTo(1) }
        Item_ { text: qsTr("Zoom to fit"); shortcut: "Shift+1"; onTriggered: editor.zoomToFit() }
        Item_ { text: qsTr("Zoom to selection"); shortcut: "Shift+2"; onTriggered: editor.zoomToSelection() }
        MenuSeparator {}
        Item_ { text: (editor.outlines ? "✓ " : "") + qsTr("Outlines"); shortcut: "Ctrl+Y"; onTriggered: editor.outlines = !editor.outlines }
        Item_ { text: (editor.showGrids ? "✓ " : "") + qsTr("Layout grids"); onTriggered: editor.showGrids = !editor.showGrids }
        Item_ { text: (editor.snapToPixels ? "✓ " : "") + qsTr("Snap to pixel grid"); onTriggered: editor.snapToPixels = !editor.snapToPixels }
    }
    Menu {
        title: qsTr("Object")
        Item_ { text: qsTr("Group selection"); shortcut: "Ctrl+G"; onTriggered: editor.group() }
        Item_ { text: qsTr("Ungroup"); shortcut: "Ctrl+Shift+G"; onTriggered: editor.ungroup() }
        Item_ { text: qsTr("Frame selection"); shortcut: "Ctrl+Alt+G"; onTriggered: editor.frameSelection() }
        Item_ { text: qsTr("Add auto layout"); shortcut: "Shift+A"; onTriggered: editor.addAutoLayout() }
        Item_ { text: qsTr("Remove auto layout"); onTriggered: editor.removeAutoLayout() }
        MenuSeparator {}
        Item_ { text: qsTr("Create component"); shortcut: "Ctrl+Alt+K"; onTriggered: editor.createComponent() }
        Item_ { text: qsTr("Combine as variants"); onTriggered: editor.combineAsVariants() }
        Item_ { text: qsTr("Detach instance"); shortcut: "Ctrl+Alt+B"; onTriggered: editor.detachInstance() }
        Item_ { text: qsTr("Reset overrides"); onTriggered: editor.resetOverrides() }
        Item_ { text: qsTr("Go to main component"); onTriggered: editor.goToMainComponent() }
        MenuSeparator {}
        Item_ { text: qsTr("Bring to front"); shortcut: "Ctrl+Shift+]"; onTriggered: editor.bringToFront() }
        Item_ { text: qsTr("Bring forward"); shortcut: "Ctrl+]"; onTriggered: editor.bringForward() }
        Item_ { text: qsTr("Send backward"); shortcut: "Ctrl+["; onTriggered: editor.sendBackward() }
        Item_ { text: qsTr("Send to back"); shortcut: "Ctrl+Shift+["; onTriggered: editor.sendToBack() }
        MenuSeparator {}
        Item_ { text: qsTr("Flip horizontal"); shortcut: "Shift+H"; onTriggered: editor.flip(true) }
        Item_ { text: qsTr("Flip vertical"); shortcut: "Shift+V"; onTriggered: editor.flip(false) }
        Item_ { text: qsTr("Rotate 90° right"); onTriggered: editor.rotateSelection(90) }
        Item_ { text: qsTr("Rotate 90° left"); onTriggered: editor.rotateSelection(-90) }
        MenuSeparator {}
        Item_ { text: qsTr("Union"); onTriggered: editor.booleanOp("union") }
        Item_ { text: qsTr("Subtract"); onTriggered: editor.booleanOp("subtract") }
        Item_ { text: qsTr("Intersect"); onTriggered: editor.booleanOp("intersect") }
        Item_ { text: qsTr("Exclude"); onTriggered: editor.booleanOp("exclude") }
        Item_ { text: qsTr("Flatten"); shortcut: "Ctrl+E"; onTriggered: editor.flatten() }
        Item_ { text: qsTr("Outline stroke"); shortcut: "Ctrl+Shift+O"; onTriggered: editor.outlineStroke() }
        Item_ { text: qsTr("Use as mask"); shortcut: "Ctrl+Alt+M"; onTriggered: editor.useAsMask() }
        MenuSeparator {}
        Item_ { text: qsTr("Show/Hide"); shortcut: "Ctrl+Shift+H"; onTriggered: editor.toggleVisible() }
        Item_ { text: qsTr("Lock/Unlock"); shortcut: "Ctrl+Shift+L"; onTriggered: editor.toggleLocked() }
    }
}
