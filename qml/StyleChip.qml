import QtQuick
import QtQuick.Layouts

// Row naming the style applied to a property group, with edit and detach.
Rectangle {
    id: root
    property string slot
    property string styleId
    readonly property var style: editor.library.styles.find(s => s.id === styleId) || null
    visible: styleId.length > 0
    Layout.fillWidth: true
    implicitHeight: 28
    radius: 5
    color: hover.hovered ? Theme.field : "transparent"
    HoverHandler { id: hover }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        spacing: 6
        StylePreview { style: root.style }
        Text {
            Layout.fillWidth: true
            text: root.style ? root.style.name : qsTr("Missing style")
            color: root.style ? Theme.text : Theme.danger
            font.pixelSize: Theme.fontSize
            elide: Text.ElideRight
            MouseArea { anchors.fill: parent; enabled: !!root.style; onClicked: editorPopup.open() }
        }
        IconButton { iconName: "detach"; tip: qsTr("Detach style"); onClicked: editor.applyStyle(root.slot, "") }
    }
    StyleEditor {
        id: editorPopup
        styleId: root.styleId
        x: -width - 30
    }
}
