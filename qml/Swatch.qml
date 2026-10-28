import QtQuick

// Small preview square for a paint map (solid, gradient or image).
Item {
    id: root
    property var paint
    implicitWidth: 16
    implicitHeight: 16
    readonly property bool isGradient: !!paint && !!paint.type && paint.type.startsWith("GRADIENT")
    Checker { anchors.fill: parent; cell: 3 }
    Rectangle {
        anchors.fill: parent
        radius: 2
        visible: !root.isGradient
        border.color: "#55ffffff"
        color: !root.paint ? "transparent"
             : root.paint.type === "SOLID" ? Qt.rgba(root.paint.color.r, root.paint.color.g, root.paint.color.b, root.paint.color.a * root.paint.opacity)
             : "#8a8a8a"
    }
    Canvas {
        anchors.fill: parent
        visible: root.isGradient
        property var stops: root.isGradient ? root.paint.stops : []
        onStopsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!stops.length) return
            const g = ctx.createLinearGradient(0, 0, width, 0)
            for (const s of stops) g.addColorStop(Math.max(0, Math.min(1, s.pos)), s.color)
            ctx.fillStyle = g
            ctx.fillRect(0, 0, width, height)
        }
    }
}
