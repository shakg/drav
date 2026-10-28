// name: Tidy into a grid
// description: Arranges the selected layers in a grid with even gaps, in selection order.
var ids = drav.selection();
var gap = 20;
var cols = Math.max(1, Math.ceil(Math.sqrt(ids.length)));
if (ids.length) {
    var first = drav.get(ids[0]);
    var x0 = first.x, y0 = first.y, cellW = 0, cellH = 0;
    ids.forEach(function (id) {
        var p = drav.get(id);
        cellW = Math.max(cellW, p.w);
        cellH = Math.max(cellH, p.h);
    });
    ids.forEach(function (id, i) {
        drav.set(id, "x", x0 + (i % cols) * (cellW + gap));
        drav.set(id, "y", y0 + Math.floor(i / cols) * (cellH + gap));
    });
    drav.notify("Arranged " + ids.length + " layers");
}
