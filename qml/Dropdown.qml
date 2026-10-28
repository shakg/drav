import QtQuick
import QtQuick.Controls.Basic

// Compact combo box. `model` is a list of {value, text} or plain strings; `current` is a value.
ComboBox {
    id: root
    property var current
    signal picked(var value)
    implicitHeight: 28
    implicitWidth: 100
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    textRole: "text"
    valueRole: "value"
    model: []
    readonly property var items: (model || []).map(m => typeof m === "string" ? ({ value: m, text: m }) : m)
    currentIndex: {
        for (let i = 0; i < items.length; ++i) if (items[i].value === current) return i
        return -1
    }
    displayText: current === undefined || current === null ? "Mixed"
        : (currentIndex >= 0 ? items[currentIndex].text : String(current))
    onActivated: index => picked(items[index].value)
    delegate: ItemDelegate {
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : 120
        height: 26
        highlighted: root.highlightedIndex === index
        contentItem: Text {
            text: typeof modelData === "string" ? modelData : modelData.text
            color: "white"
            font.pixelSize: Theme.fontSize
            font.family: Theme.fontFamily
            verticalAlignment: Text.AlignVCenter
            leftPadding: 18
            Icon {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                name: "check"
                size: 12
                visible: index === root.currentIndex
            }
        }
        background: Rectangle { color: highlighted ? Theme.accent : "transparent"; radius: 4 }
    }
    indicator: Icon {
        x: root.width - width - 6
        anchors.verticalCenter: parent.verticalCenter
        name: "chevronDown"
        size: 12
        color: Theme.textDim
    }
    contentItem: Text {
        leftPadding: 8
        rightPadding: 20
        text: root.displayText
        color: Theme.text
        font.pixelSize: Theme.fontSize
        font.family: Theme.fontFamily
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 5
        color: root.hovered || root.popup.visible ? Theme.field : "transparent"
        border.color: root.hovered ? Theme.panelBorder : "transparent"
    }
    popup: Popup {
        y: root.height + 2
        width: Math.max(root.width, 140)
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 360)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
        }
        background: Rectangle { color: "#1e1e1e"; radius: 8; border.color: "#3a3a3a" }
    }
}
