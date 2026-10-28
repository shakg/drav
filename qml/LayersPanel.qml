import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Layer tree of the current page with selection, rename, visibility/lock and drag reordering.
ListView {
    id: list
    model: editor.layers
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { }
    property int dropRow: -1
    property var drop: null
    property int dragRow: -1

    function iconFor(type) {
        return ({ FRAME: "frame", GROUP: "group", RECTANGLE: "rect", ELLIPSE: "ellipse", POLYGON: "polygon", STAR: "star",
                  LINE: "line", VECTOR: "vector", TEXT: "text", BOOLEAN_OPERATION: "boolean", COMPONENT: "component",
                  COMPONENT_SET: "component", INSTANCE: "instance" })[type] || "rect"
    }

    Connections {
        target: editor
        function onRequestRename(id) {
            const r = editor.layers.rowOf(id)
            if (r >= 0) { list.positionViewAtIndex(r, ListView.Contain); const item = list.itemAtIndex(r); if (item) item.startRename() }
        }
        function onSelectionChanged() {
            const id = editor.props.id
            if (id === undefined || id === null) return
            const r = editor.layers.rowOf(id)
            if (r >= 0) list.positionViewAtIndex(r, ListView.Contain)
        }
    }

    delegate: Rectangle {
        id: row
        required property int index
        required property var nodeId
        required property string name
        required property string type
        required property int depth
        required property bool hasChildren
        required property bool expanded
        required property bool layerVisible
        required property bool locked
        required property bool selected
        required property bool parentSelected
        required property int component
        required property bool mask
        required property int autoLayout
        width: ListView.view.width
        height: 32
        color: selected ? Theme.selectionRow : parentSelected ? Theme.selectionChild : (hover.hovered ? Theme.hover : "transparent")
        readonly property color fg: component === 1 ? Theme.componentText : component === 2 ? Theme.componentText : (layerVisible ? Theme.text : Theme.textFaint)

        function startRename() { nameEdit.text = name; nameEdit.visible = true; nameEdit.forceActiveFocus(); nameEdit.selectAll() }

        HoverHandler { id: hover }

        Rectangle {
            visible: list.dropRow === row.index && list.drop
            color: Theme.accent
            x: 8 + (list.drop && list.drop.into ? 0 : row.depth * 16)
            width: parent.width - x
            height: list.drop && list.drop.into ? parent.height : 2
            opacity: list.drop && list.drop.into ? 0.25 : 1
            y: !list.drop || list.drop.into ? 0 : (list.drop.above ? 0 : parent.height - 2)
        }

        MouseArea {
            id: rowMouse
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            property point pressPos
            property bool dragging: false
            onPressed: m => { pressPos = Qt.point(m.x, m.y); dragging = false }
            onPositionChanged: m => {
                if (!pressed) return
                if (!dragging && Math.abs(m.y - pressPos.y) > 4) { dragging = true; list.dragRow = row.index }
                if (!dragging) return
                const p = mapToItem(list.contentItem, m.x, m.y)
                const r = list.indexAt(10, p.y)
                if (r < 0) { list.dropRow = -1; return }
                const item = list.itemAtIndex(r)
                const frac = (p.y - item.y) / item.height
                list.dropRow = r
                const t = editor.layers.dropTarget(r, frac)
                t.above = frac < 0.5
                list.drop = t
            }
            onReleased: m => {
                if (dragging && list.drop && list.dropRow >= 0 && list.dropRow !== row.index) {
                    editor.moveNodeInTree(row.nodeId, list.drop.parent, list.drop.index)
                } else if (!dragging) {
                    if (m.modifiers & Qt.ShiftModifier) editor.selectRange(row.nodeId)
                    else editor.selectNode(row.nodeId, (m.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0)
                }
                dragging = false
                list.dropRow = -1
                list.drop = null
                list.dragRow = -1
            }
            onDoubleClicked: row.startRename()
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8 + row.depth * 16
            anchors.rightMargin: 6
            spacing: 4
            Item {
                implicitWidth: 14
                implicitHeight: 14
                Icon {
                    anchors.centerIn: parent
                    visible: row.hasChildren
                    name: row.expanded ? "chevronDown" : "chevronRight"
                    size: 10
                    color: Theme.textDim
                }
                MouseArea { anchors.fill: parent; anchors.margins: -4; enabled: row.hasChildren; onClicked: editor.setNodeExpanded(row.nodeId, !row.expanded) }
            }
            Icon {
                name: row.mask ? "mask" : (row.autoLayout && row.type === "FRAME") ? (row.autoLayout === 1 ? "layoutH" : "layoutV") : list.iconFor(row.type)
                size: 14
                color: row.component ? Theme.component : Theme.textDim
            }
            Text {
                Layout.fillWidth: true
                text: row.name
                visible: !nameEdit.visible
                color: row.fg
                font.pixelSize: Theme.fontSize
                font.family: Theme.fontFamily
                font.weight: row.depth === 0 && (row.type === "FRAME" || row.type === "COMPONENT" || row.type === "COMPONENT_SET") ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
            }
            TextInput {
                id: nameEdit
                Layout.fillWidth: true
                visible: false
                color: Theme.text
                selectionColor: Theme.accent
                font.pixelSize: Theme.fontSize
                font.family: Theme.fontFamily
                onAccepted: { editor.renameNode(row.nodeId, text); visible = false }
                onActiveFocusChanged: if (!activeFocus && visible) { editor.renameNode(row.nodeId, text); visible = false }
                Keys.onEscapePressed: visible = false
            }
            IconButton {
                implicitWidth: 22; implicitHeight: 22
                iconSize: 13
                iconName: row.locked ? "lock" : "unlock"
                visible: hover.hovered || row.locked
                onClicked: editor.setNodeLocked(row.nodeId, !row.locked)
            }
            IconButton {
                implicitWidth: 22; implicitHeight: 22
                iconSize: 13
                iconName: row.layerVisible ? "eye" : "eyeOff"
                visible: hover.hovered || !row.layerVisible
                onClicked: editor.setNodeVisible(row.nodeId, !row.layerVisible)
            }
        }
    }
}
