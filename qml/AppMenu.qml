import QtQuick
import QtQuick.Controls.Basic

// Dark popup menu. Use with AppMenuItem / AppMenuSeparator; nested menus must be AppMenus too.
Menu {
    id: root
    padding: 4
    // Basic's Menu doesn't grow to fit its items, so long labels and shortcuts got clipped.
    width: {
        let w = 160
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i)
            if (item)
                w = Math.max(w, item.implicitWidth)
        }
        return w + leftPadding + rightPadding
    }
    delegate: AppMenuItem {}
    background: Rectangle {
        implicitWidth: 160
        color: "#1e1e1e"
        radius: 8
        border.color: "#3a3a3a"
    }
}
