import QtQuick

// Small preview of a style: color swatch, "Ag" for text, icon for effects and grids.
Item {
    id: root
    property var style
    implicitWidth: 16
    implicitHeight: 16
    Swatch {
        anchors.fill: parent
        visible: !!root.style && root.style.type === "FILL"
        paint: root.style && root.style.paints && root.style.paints.length ? root.style.paints[root.style.paints.length - 1] : null
    }
    Text {
        anchors.centerIn: parent
        visible: !!root.style && root.style.type === "TEXT"
        text: "Ag"
        color: Theme.text
        font.pixelSize: 11
        font.weight: root.style && root.style.text ? Math.min(900, root.style.text.fontWeight) : Font.Normal
    }
    Icon {
        anchors.centerIn: parent
        visible: !!root.style && (root.style.type === "EFFECT" || root.style.type === "GRID")
        name: root.style && root.style.type === "GRID" ? "grid" : "effects"
        size: 14
        color: Theme.textDim
    }
}
