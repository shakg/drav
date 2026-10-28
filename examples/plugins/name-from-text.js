// name: Name layers from their text
// description: Renames selected text layers (and frames, by their first text) after their content.
function firstText(id) {
    var p = drav.get(id);
    if (p.type === "TEXT")
        return p.characters;
    var kids = drav.children(id);
    for (var i = 0; i < kids.length; ++i) {
        var t = firstText(kids[i]);
        if (t)
            return t;
    }
    return "";
}
var count = 0;
drav.selection().forEach(function (id) {
    var t = firstText(id).split("\n")[0].trim().slice(0, 40);
    if (t) {
        drav.set(id, "name", t);
        ++count;
    }
});
drav.notify("Renamed " + count + " layers");
