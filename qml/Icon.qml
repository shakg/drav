import QtQuick
import QtQuick.Shapes
import "Icons.js" as Icons

// Crisp stroked icon from Icons.js, scaled from a 24x24 grid.
Item {
    id: root
    property string name
    property color color: Theme.text
    property real size: 16
    property real strokeWidth: 1.5
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
