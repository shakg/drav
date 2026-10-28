import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Floating tool bar at the bottom of the canvas.
Rectangle {
    id: root
    implicitWidth: row.implicitWidth + 16
    implicitHeight: 44
    radius: 12
    color: Theme.panel
    border.color: Theme.panelBorder

    readonly property var shapeTools: [
        { tool: "rect", icon: "rect", text: qsTr("Rectangle"), key: "R" },
        { tool: "line", icon: "line", text: qsTr("Line"), key: "L" },
        { tool: "arrow", icon: "arrow", text: qsTr("Arrow"), key: "Shift+L" },
        { tool: "ellipse", icon: "ellipse", text: qsTr("Ellipse"), key: "O" },
        { tool: "polygon", icon: "polygon", text: qsTr("Polygon"), key: "" },
        { tool: "star", icon: "star", text: qsTr("Star"), key: "" }
    ]
    property string lastShape: "rect"
    signal importRequested()
    readonly property bool shapeActive: shapeTools.some(t => t.tool === editor.tool)
    onShapeActiveChanged: if (shapeActive) lastShape = editor.tool

    component ToolButton: IconButton {
        property string tool
        implicitWidth: 34
        implicitHeight: 34
        iconSize: 18
        checked: editor.tool === tool
        onClicked: editor.tool = tool
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 2
        ToolButton { tool: "select"; iconName: "select"; tip: qsTr("Move (V)"); checked: editor.tool === "select" || editor.tool === "vectorEdit" || editor.tool === "textEdit" }
        ToolButton { tool: "scale"; iconName: "scale"; tip: qsTr("Scale (K)") }
        ToolButton { tool: "frame"; iconName: "frame"; tip: qsTr("Frame (F)") }
        Row {
            ToolButton {
                tool: root.lastShape
                iconName: root.shapeTools.find(t => t.tool === root.lastShape).icon
                tip: root.shapeTools.find(t => t.tool === root.lastShape).text
                checked: root.shapeActive
            }
            IconButton {
                implicitWidth: 14; implicitHeight: 34; iconSize: 10; iconName: "chevronDown"
                onClicked: shapeMenu.open()
                Menu {
                    id: shapeMenu
                    y: -implicitHeight - 8
                    Repeater {
                        model: root.shapeTools
                        MenuItem {
                            required property var modelData
                            text: modelData.text + (modelData.key ? "\t" + modelData.key : "")
                            onTriggered: editor.tool = modelData.tool
                        }
                    }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Place image…\tCtrl+Shift+K"); onTriggered: root.importRequested() }
                }
            }
        }
        Row {
            ToolButton {
                tool: editor.tool === "pencil" ? "pencil" : "pen"
                iconName: editor.tool === "pencil" ? "pencil" : "pen"
                tip: editor.tool === "pencil" ? qsTr("Pencil (Shift+P)") : qsTr("Pen (P)")
                checked: editor.tool === "pen" || editor.tool === "pencil"
            }
            IconButton {
                implicitWidth: 14; implicitHeight: 34; iconSize: 10; iconName: "chevronDown"
                onClicked: penMenu.open()
                Menu {
                    id: penMenu
                    y: -implicitHeight - 8
                    MenuItem { text: qsTr("Pen\tP"); onTriggered: editor.tool = "pen" }
                    MenuItem { text: qsTr("Pencil\tShift+P"); onTriggered: editor.tool = "pencil" }
                }
            }
        }
        ToolButton { tool: "text"; iconName: "text"; tip: qsTr("Text (T)") }
        ToolButton { tool: "hand"; iconName: "hand"; tip: qsTr("Hand tool (H, or hold Space)") }
        Rectangle { width: 1; height: 24; color: Theme.panelBorder }
        IconButton { implicitWidth: 34; implicitHeight: 34; iconSize: 18; iconName: "component"; tip: qsTr("Create component (Ctrl+Alt+K)"); enabled: editor.selectionCount > 0; onClicked: editor.createComponent() }
        IconButton { implicitWidth: 34; implicitHeight: 34; iconSize: 18; iconName: "layoutV"; tip: qsTr("Add auto layout (Shift+A)"); enabled: editor.selectionCount > 0; onClicked: editor.addAutoLayout() }
    }
}
