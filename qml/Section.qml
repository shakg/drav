import QtQuick
import QtQuick.Layouts

// Inspector section with a title row, optional trailing buttons and content below.
ColumnLayout {
    id: root
    property string title
    property bool showAdd: false
    property bool showRemove: false
    default property alias content: body.data
    property alias actions: actionRow.data
    signal add()
    signal remove()
    spacing: 4
    Layout.fillWidth: true

    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.panelBorder }
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 8
        Layout.topMargin: 6
        Text {
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontSize
            font.weight: Font.DemiBold
            font.family: Theme.fontFamily
            Layout.fillWidth: true
        }
        Row { id: actionRow; spacing: 0 }
        IconButton { iconName: "minus"; visible: root.showRemove; onClicked: root.remove(); tip: qsTr("Remove") }
        IconButton { iconName: "plus"; visible: root.showAdd; onClicked: root.add(); tip: qsTr("Add") }
    }
    ColumnLayout {
        id: body
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 12
        Layout.bottomMargin: 10
        spacing: 6
    }
}
