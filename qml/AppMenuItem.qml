import QtQuick
import QtQuick.Controls.Basic

// Menu row: label on the left, optional shortcut on the right, chevron for submenus.
MenuItem {
    id: root
    property string shortcut
    implicitWidth: leftPadding + rightPadding + label.implicitWidth + (shortcut ? 32 + keys.implicitWidth : 0)
    implicitHeight: 26
    leftPadding: 10
    rightPadding: subMenu ? 26 : 10
    hoverEnabled: true
    contentItem: Item {
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.enabled ? Theme.text : Theme.textFaint
            font.pixelSize: Theme.fontSize
            font.family: Theme.fontFamily
        }
        Text {
            id: keys
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.shortcut
            color: root.highlighted && root.enabled ? Theme.text : Theme.textFaint
            font.pixelSize: Theme.fontSize
            font.family: Theme.fontFamily
        }
    }
    indicator: null
    arrow: Icon {
        x: root.width - width - 8
        y: (root.height - height) / 2
        visible: root.subMenu
        name: "chevronRight"
        size: 12
        color: root.enabled ? Theme.text : Theme.textFaint
    }
    background: Rectangle {
        radius: 4
        color: root.highlighted && root.enabled ? Theme.accent : "transparent"
    }
}
