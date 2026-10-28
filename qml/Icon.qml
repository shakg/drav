import QtQuick
import QtQuick.Shapes
import "Icons.js" as Icons

// Stroked Lucide icon from Icons.js, scaled from a 24x24 grid.
Item {
    id: root
    property string name
    property color color: Theme.text
    property real size: 16
    // Lucide's 2/24 stroke ratio, floored so small icons don't go faint.
    property real strokeWidth: Math.max(1.25, size / 12)
    implicitWidth: size
    implicitHeight: size

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: root.size / 24
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.strokeWidth * 24 / root.size
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: Icons.paths[root.name] || "" }
        }
    }
}
