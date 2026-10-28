import QtQuick

// Transparency checkerboard.
Canvas {
    property int cell: 4
    onPaint: {
        const ctx = getContext("2d")
        ctx.fillStyle = "#ffffff"
        ctx.fillRect(0, 0, width, height)
        ctx.fillStyle = "#cccccc"
        for (let y = 0; y < height; y += cell)
            for (let x = (y / cell) % 2 ? cell : 0; x < width; x += cell * 2)
                ctx.fillRect(x, y, cell, cell)
    }
}
