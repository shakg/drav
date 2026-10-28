import QtQuick
import QtQuick.Controls.Basic

// Square toolbar/inspector button with an icon and optional checked state.
AbstractButton {
    id: root
    property string iconName
    property string tip
    property real iconSize: 16
    property color iconColor: checked ? "white" : (enabled ? Theme.text : Theme.textFaint)
    property color checkedColor: Theme.accent
    implicitWidth: 28
    implicitHeight: 28
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        radius: 5
        color: root.checked ? root.checkedColor : (root.hovered || root.down ? Theme.hover : "transparent")
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: root.iconName
            size: root.iconSize
            color: root.iconColor
        }
    }
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: tip
}
