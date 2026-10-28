import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Find and replace in text layers (optionally layer names) on the current page.
Rectangle {
    id: root
    visible: false
    implicitWidth: 520
    implicitHeight: col.implicitHeight + 16
    radius: 10
    color: Theme.panel
    border.color: Theme.panelBorder
    property bool matchCase: false
    property bool names: false
    readonly property var matches: visible && findBox.text.length ? (editor.props, editor.findLayers(findBox.text, matchCase, names)) : []

    function show() { visible = true; findBox.forceActiveFocus() }

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Icon { name: "search"; size: 14; color: Theme.textDim }
            TextBox { id: findBox; Layout.fillWidth: true; placeholder: qsTr("Find"); onAccepted: root.selectAll() }
            Text { text: root.matches.length + qsTr(" layers"); color: Theme.textDim; font.pixelSize: Theme.fontSize }
            IconButton { iconName: "caseSensitive"; checkable: true; checked: root.matchCase; tip: qsTr("Match case"); onClicked: root.matchCase = !root.matchCase }
            IconButton { iconName: "text"; checkable: true; checked: root.names; tip: qsTr("Also search layer names"); onClicked: root.names = !root.names }
            IconButton { iconName: "close"; tip: qsTr("Close (Esc)"); onClicked: root.visible = false }
        }
        RowLayout {
            Layout.fillWidth: true
            Icon { name: "replace"; size: 14; color: Theme.textDim }
            TextBox { id: replaceBox; Layout.fillWidth: true; placeholder: qsTr("Replace with") }
            Button { text: qsTr("Select all"); enabled: root.matches.length > 0; onClicked: root.selectAll() }
            Button { text: qsTr("Replace all"); enabled: root.matches.length > 0; onClicked: editor.replaceText(findBox.text, replaceBox.text, root.matchCase, root.names) }
        }
    }
    function selectAll() {
        if (!matches.length) return
        editor.selectNode(matches[0], false)
        for (let i = 1; i < matches.length; ++i) editor.selectNode(matches[i], true)
    }
    Keys.onEscapePressed: visible = false
}
