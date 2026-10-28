import QtQuick

// Single-line text input in inspector style.
Rectangle {
    id: root
    property alias text: input.text
    property string placeholder
    signal accepted(string text)
    implicitWidth: 120
    implicitHeight: 28
    radius: 5
    color: input.activeFocus || hover.hovered ? Theme.field : "transparent"
    border.color: input.activeFocus ? Theme.accent : (hover.hovered ? Theme.panelBorder : "transparent")
    HoverHandler { id: hover }
    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        verticalAlignment: Text.AlignVCenter
        color: Theme.text
        selectionColor: Theme.accent
        font.pixelSize: Theme.fontSize
        font.family: Theme.fontFamily
        selectByMouse: true
        clip: true
        onAccepted: { root.accepted(text); focus = false }
        Keys.onEscapePressed: focus = false
        Text {
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            text: root.placeholder
            visible: !input.text.length && !input.activeFocus
            color: Theme.textFaint
            font: input.font
        }
    }
}
