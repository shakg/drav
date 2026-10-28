import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Right-hand inspector ("Design" tab).
ScrollView {
    id: root
    readonly property var p: editor.props
    readonly property bool has: p.count > 0
    readonly property string type: p.type || ""
    readonly property bool frameLike: ["FRAME", "COMPONENT", "COMPONENT_SET", "INSTANCE"].indexOf(type) >= 0
    readonly property bool isText: type === "TEXT"
    readonly property bool al: has && p.layoutMode !== undefined && p.layoutMode !== null && p.layoutMode !== "NONE"
    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: root.availableWidth
        spacing: 0

        // ---------------- Page (nothing selected)
        Section {
            visible: !root.has
            title: qsTr("Page")
            RowLayout {
                Swatch {
                    paint: ({ type: "SOLID", color: root.p.pageBackground, opacity: 1 })
                    MouseArea { anchors.fill: parent; onClicked: { pagePopup.paint = { type: "SOLID", color: root.p.pageBackground, opacity: 1, blend: "NORMAL" }; pagePopup.open() } }
                    PaintPopup {
                        id: pagePopup
                        x: -width - 30
                        allowImage: false
                        onEdited: (pt, live) => { pagePopup.paint = pt; editor.setProp("pageBackground", pt.color) }
                    }
                }
                Text { text: qsTr("Background"); color: Theme.text; font.pixelSize: Theme.fontSize }
            }
        }

        LocalAssets { visible: !root.has }

        // ---------------- Instance / component header
        Section {
            visible: root.has && (root.p.isInstance === true || root.p.isComponent === true || root.p.isComponentSet === true)
            title: root.p.isInstance ? (root.p.componentName || qsTr("Instance")) : root.p.isComponentSet ? qsTr("Component set") : qsTr("Component")
            actions: [
                IconButton { iconName: "target"; tip: qsTr("Go to main component"); visible: root.p.isInstance === true; onClicked: editor.goToMainComponent() },
                IconButton { iconName: "reset"; tip: qsTr("Reset all overrides"); visible: root.p.isInstance === true && root.p.overrideCount > 0; onClicked: editor.resetOverrides() },
                IconButton { iconName: "detach"; tip: qsTr("Detach instance (Ctrl+Alt+B)"); visible: root.p.isInstance === true; onClicked: editor.detachInstance() }
            ]
            Repeater {
                model: root.p.variants || []
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Text { text: modelData.name; color: Theme.textDim; font.pixelSize: Theme.fontSize; Layout.preferredWidth: 80; elide: Text.ElideRight }
                    Dropdown { Layout.fillWidth: true; current: modelData.value; model: modelData.options; onPicked: v => editor.setVariant(modelData.name, v) }
                }
            }
            Dropdown {
                visible: root.p.isInstance === true
                Layout.fillWidth: true
                displayText: qsTr("Swap instance…")
                current: null
                model: editor.components.map(c => ({ value: c.id, text: (c.set ? c.set + " / " : "") + c.name }))
                onPicked: v => editor.swapComponent(v)
            }
            TextBox {
                visible: root.p.isComponent === true
                Layout.fillWidth: true
                placeholder: qsTr("Description")
                text: root.p.description || ""
                onAccepted: t => editor.setProp("description", t)
            }
            Text {
                visible: root.p.mainMissing === true
                text: qsTr("Main component is missing")
                color: Theme.danger
                font.pixelSize: Theme.fontSize
            }
        }

        ComponentPropsSection { visible: root.has }

        // ---------------- Alignment
        Section {
            visible: root.has
            title: ""
            RowLayout {
                spacing: 2
                IconButton { iconName: "alignLeft"; tip: qsTr("Align left (Alt+A)"); onClicked: editor.align("left") }
                IconButton { iconName: "alignHCenter"; tip: qsTr("Align horizontal centers (Alt+H)"); onClicked: editor.align("hcenter") }
                IconButton { iconName: "alignRight"; tip: qsTr("Align right (Alt+D)"); onClicked: editor.align("right") }
                IconButton { iconName: "alignTop"; tip: qsTr("Align top (Alt+W)"); onClicked: editor.align("top") }
                IconButton { iconName: "alignVCenter"; tip: qsTr("Align vertical centers (Alt+V)"); onClicked: editor.align("vcenter") }
                IconButton { iconName: "alignBottom"; tip: qsTr("Align bottom (Alt+S)"); onClicked: editor.align("bottom") }
                IconButton { iconName: "distH"; tip: qsTr("Distribute horizontal spacing"); enabled: root.p.count > 2; onClicked: editor.distribute(true) }
                IconButton { iconName: "distV"; tip: qsTr("Distribute vertical spacing"); enabled: root.p.count > 2; onClicked: editor.distribute(false) }
            }
            RowLayout {
                visible: root.p.count > 1
                spacing: 2
                IconButton { iconName: "union"; tip: qsTr("Union"); onClicked: editor.booleanOp("union") }
                IconButton { iconName: "subtract"; tip: qsTr("Subtract"); onClicked: editor.booleanOp("subtract") }
                IconButton { iconName: "intersect"; tip: qsTr("Intersect"); onClicked: editor.booleanOp("intersect") }
                IconButton { iconName: "exclude"; tip: qsTr("Exclude"); onClicked: editor.booleanOp("exclude") }
                Item { Layout.fillWidth: true }
                IconButton { iconName: "component"; tip: qsTr("Create component (Ctrl+Alt+K)"); onClicked: editor.createComponent() }
                IconButton { iconName: "frame"; tip: qsTr("Frame selection (Ctrl+Alt+G)"); onClicked: editor.frameSelection() }
                IconButton { iconName: "mask"; tip: qsTr("Use as mask (Ctrl+Alt+M)"); onClicked: editor.useAsMask() }
            }
        }

        // ---------------- Position & size
        Section {
            visible: root.has
            title: root.frameLike ? qsTr("Frame") : root.type === "BOOLEAN_OPERATION" ? qsTr("Boolean") : qsTr("Position")
            actions: [
                IconButton { iconName: "union"; visible: root.type === "BOOLEAN_OPERATION"; checked: root.p.boolOp === "UNION"; onClicked: editor.setProp("boolOp", "UNION"); tip: qsTr("Union") },
                IconButton { iconName: "subtract"; visible: root.type === "BOOLEAN_OPERATION"; checked: root.p.boolOp === "SUBTRACT"; onClicked: editor.setProp("boolOp", "SUBTRACT"); tip: qsTr("Subtract") },
                IconButton { iconName: "intersect"; visible: root.type === "BOOLEAN_OPERATION"; checked: root.p.boolOp === "INTERSECT"; onClicked: editor.setProp("boolOp", "INTERSECT"); tip: qsTr("Intersect") },
                IconButton { iconName: "exclude"; visible: root.type === "BOOLEAN_OPERATION"; checked: root.p.boolOp === "EXCLUDE"; onClicked: editor.setProp("boolOp", "EXCLUDE"); tip: qsTr("Exclude") }
            ]
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 6
                rowSpacing: 4
                NumberField { Layout.fillWidth: true; label: "X"; key: root.p.count > 1 ? "selX" : "x"; value: root.p.count > 1 ? root.p.selX : root.p.x }
                NumberField { Layout.fillWidth: true; label: "Y"; key: root.p.count > 1 ? "selY" : "y"; value: root.p.count > 1 ? root.p.selY : root.p.y }
                Item { implicitWidth: 28 }
                NumberField { Layout.fillWidth: true; label: "W"; key: "w"; bindKey: "w"; value: root.p.w; minimum: 0.01 }
                NumberField { Layout.fillWidth: true; label: "H"; key: "h"; bindKey: "h"; value: root.p.h; minimum: 0.01; enabled: root.type !== "LINE" }
                IconButton { iconName: "link"; checked: root.p.lockRatio === true; tip: qsTr("Constrain proportions"); onClicked: editor.setProp("lockRatio", !root.p.lockRatio) }
                NumberField { Layout.fillWidth: true; icon: "rotate"; key: "rotation"; value: root.p.rotation; suffix: "°"; decimals: 1 }
                NumberField {
                    id: radiusField
                    Layout.fillWidth: true
                    icon: "corner"
                    key: "cornerRadius"
                    bindKey: "cornerRadius"
                    minimum: 0
                    value: root.p.cornerRadius
                    visible: ["RECTANGLE", "FRAME", "COMPONENT", "COMPONENT_SET", "INSTANCE", "POLYGON", "STAR", "VECTOR"].indexOf(root.type) >= 0
                }
                IconButton { id: cornersToggle; iconName: "corners"; checkable: true; visible: radiusField.visible; tip: qsTr("Independent corners") }
            }
            GridLayout {
                visible: cornersToggle.checked && radiusField.visible
                Layout.fillWidth: true
                columns: 4
                columnSpacing: 4
                Repeater {
                    model: 4
                    NumberField {
                        required property int index
                        Layout.fillWidth: true
                        minimum: 0
                        value: root.p.corners ? root.p.corners[index] : undefined
                        onCommitted: v => { const c = root.p.corners.slice(); c[index] = v; editor.setProp("corners", c) }
                    }
                }
            }
            RowLayout {
                visible: radiusField.visible && root.type !== "POLYGON" && root.type !== "STAR" && root.type !== "VECTOR"
                Text { text: qsTr("Corner smoothing"); color: Theme.textDim; font.pixelSize: Theme.fontSize; Layout.fillWidth: true }
                NumberField { Layout.preferredWidth: 70; key: "cornerSmoothing"; value: root.p.cornerSmoothing; displayScale: 100; suffix: "%"; decimals: 0; minimum: 0; maximum: 1 }
            }
            // Resizing behavior inside auto layout / for auto layout frames.
            RowLayout {
                visible: root.p.inAutoLayout === true || root.al || root.isText
                Layout.fillWidth: true
                Dropdown {
                    Layout.fillWidth: true
                    current: root.p.sizingH
                    model: [{ value: "FIXED", text: qsTr("W  Fixed") }]
                        .concat(root.al || root.isText ? [{ value: "HUG", text: qsTr("W  Hug") }] : [])
                        .concat(root.p.inAutoLayout ? [{ value: "FILL", text: qsTr("W  Fill") }] : [])
                    onPicked: v => editor.setProp("sizingH", v)
                }
                Dropdown {
                    Layout.fillWidth: true
                    current: root.p.sizingV
                    model: [{ value: "FIXED", text: qsTr("H  Fixed") }]
                        .concat(root.al || root.isText ? [{ value: "HUG", text: qsTr("H  Hug") }] : [])
                        .concat(root.p.inAutoLayout ? [{ value: "FILL", text: qsTr("H  Fill") }] : [])
                    onPicked: v => editor.setProp("sizingV", v)
                }
            }
            RowLayout {
                visible: root.p.inAutoLayout === true
                IconButton { iconName: "target"; checked: root.p.absolute === true; tip: qsTr("Absolute position"); onClicked: editor.setProp("absolute", !root.p.absolute) }
                Text { text: qsTr("Ignore auto layout"); color: Theme.textDim; font.pixelSize: Theme.fontSize }
            }
            RowLayout {
                visible: root.frameLike
                IconButton { iconName: "clip"; checked: root.p.clipsContent === true; tip: qsTr("Clip content"); onClicked: editor.setProp("clipsContent", !root.p.clipsContent) }
                Text { text: qsTr("Clip content"); color: Theme.textDim; font.pixelSize: Theme.fontSize }
            }
        }

        // ---------------- Shape specifics
        Section {
            visible: root.has && ["ELLIPSE", "POLYGON", "STAR"].indexOf(root.type) >= 0
            title: qsTr("Shape")
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 6
                NumberField { Layout.fillWidth: true; visible: root.type === "ELLIPSE"; label: "S"; key: "arcStart"; value: root.p.arcStart; suffix: "°" }
                NumberField { Layout.fillWidth: true; visible: root.type === "ELLIPSE"; label: "E"; key: "arcSweep"; value: root.p.arcSweep; suffix: "°"; minimum: -360; maximum: 360 }
                NumberField { Layout.fillWidth: true; visible: root.type === "ELLIPSE"; label: "R"; key: "arcInner"; value: root.p.arcInner; displayScale: 100; suffix: "%"; decimals: 0; minimum: 0; maximum: 1 }
                NumberField { Layout.fillWidth: true; visible: root.type !== "ELLIPSE"; label: "#"; key: "pointCount"; value: root.p.pointCount; decimals: 0; minimum: 3; maximum: 60 }
                NumberField { Layout.fillWidth: true; visible: root.type === "STAR"; label: "R"; key: "starInner"; value: root.p.starInner; displayScale: 100; suffix: "%"; decimals: 0; minimum: 0.01; maximum: 1 }
            }
        }

        // ---------------- Auto layout
        Section {
            visible: root.has && root.frameLike
            title: qsTr("Auto layout")
            showAdd: !root.al
            showRemove: root.al
            onAdd: editor.addAutoLayout()
            onRemove: editor.removeAutoLayout()
            RowLayout {
                visible: root.al
                spacing: 2
                IconButton { iconName: "layoutV"; checked: root.p.layoutMode === "VERTICAL"; tip: qsTr("Vertical"); onClicked: editor.setProp("layoutMode", "VERTICAL") }
                IconButton { iconName: "layoutH"; checked: root.p.layoutMode === "HORIZONTAL" && !root.p.layoutWrap; tip: qsTr("Horizontal"); onClicked: { editor.setProp("layoutMode", "HORIZONTAL"); editor.setProp("layoutWrap", false) } }
                IconButton { iconName: "wrap"; checked: root.p.layoutMode === "HORIZONTAL" && root.p.layoutWrap === true; tip: qsTr("Wrap"); onClicked: { editor.setProp("layoutMode", "HORIZONTAL"); editor.setProp("layoutWrap", true) } }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                visible: root.al
                Layout.fillWidth: true
                spacing: 8
                // 3x3 alignment pad
                Grid {
                    columns: 3
                    spacing: 0
                    Repeater {
                        model: 9
                        Rectangle {
                            required property int index
                            readonly property bool horiz: root.p.layoutMode === "HORIZONTAL"
                            readonly property string prim: ["MIN", "CENTER", "MAX"][horiz ? index % 3 : Math.floor(index / 3)]
                            readonly property string cnt: ["MIN", "CENTER", "MAX"][horiz ? Math.floor(index / 3) : index % 3]
                            readonly property bool active: (root.p.primaryAlign === prim || root.p.primaryAlign === "SPACE_BETWEEN") && root.p.counterAlign === cnt
                            width: 22; height: 22
                            color: Theme.field
                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.active ? 10 : 3; height: parent.active ? 10 : 3
                                radius: parent.active ? 2 : 1.5
                                color: parent.active ? Theme.accent : Theme.textFaint
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (root.p.primaryAlign !== "SPACE_BETWEEN") editor.setProp("primaryAlign", parent.prim)
                                    editor.setProp("counterAlign", parent.cnt)
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    NumberField {
                        Layout.fillWidth: true
                        icon: root.p.layoutMode === "HORIZONTAL" ? "spacingH" : "spacingV"
                        key: "spacing"
                        bindKey: "spacing"
                        value: root.p.primaryAlign === "SPACE_BETWEEN" ? undefined : root.p.spacing
                    }
                    RowLayout {
                        IconButton { iconName: "distH"; checked: root.p.primaryAlign === "SPACE_BETWEEN"; tip: qsTr("Space between")
                            onClicked: editor.setProp("primaryAlign", root.p.primaryAlign === "SPACE_BETWEEN" ? "MIN" : "SPACE_BETWEEN") }
                        NumberField { Layout.fillWidth: true; visible: root.p.layoutWrap === true; icon: "spacingV"; key: "counterSpacing"; bindKey: "counterSpacing"; value: root.p.counterSpacing }
                    }
                }
            }
            GridLayout {
                visible: root.al
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 6
                rowSpacing: 4
                NumberField { Layout.fillWidth: true; icon: "padH"; key: "padH"; minimum: 0; value: root.p.padL === root.p.padR ? root.p.padL : undefined }
                NumberField { Layout.fillWidth: true; icon: "padV"; key: "padV"; minimum: 0; value: root.p.padT === root.p.padB ? root.p.padT : undefined }
                NumberField { Layout.fillWidth: true; label: "L"; key: "padL"; bindKey: "padL"; minimum: 0; value: root.p.padL; visible: padToggle.checked }
                NumberField { Layout.fillWidth: true; label: "T"; key: "padT"; bindKey: "padT"; minimum: 0; value: root.p.padT; visible: padToggle.checked }
                NumberField { Layout.fillWidth: true; label: "R"; key: "padR"; bindKey: "padR"; minimum: 0; value: root.p.padR; visible: padToggle.checked }
                NumberField { Layout.fillWidth: true; label: "B"; key: "padB"; bindKey: "padB"; minimum: 0; value: root.p.padB; visible: padToggle.checked }
            }
            RowLayout {
                visible: root.al
                IconButton { id: padToggle; iconName: "corners"; checkable: true; tip: qsTr("Individual padding") }
                IconButton { iconName: "strokeIcon"; checked: root.p.strokesIncluded === true; tip: qsTr("Include strokes in layout"); onClicked: editor.setProp("strokesIncluded", !root.p.strokesIncluded) }
                IconButton { iconName: "swap"; checked: root.p.reverseZ === true; tip: qsTr("Last on top (canvas stacking)"); onClicked: editor.setProp("reverseZ", !root.p.reverseZ) }
            }
        }

        // ---------------- Constraints
        Section {
            visible: root.has && !root.p.inAutoLayout && ["FRAME", "COMPONENT", "INSTANCE", "COMPONENT_SET", "GROUP"].indexOf(root.p.parentType) >= 0
            title: qsTr("Constraints")
            RowLayout {
                Layout.fillWidth: true
                Dropdown {
                    Layout.fillWidth: true
                    current: root.p.constraintH
                    model: [{ value: "LEFT", text: qsTr("Left") }, { value: "RIGHT", text: qsTr("Right") }, { value: "LEFT_RIGHT", text: qsTr("Left & right") },
                        { value: "CENTER", text: qsTr("Center") }, { value: "SCALE", text: qsTr("Scale") }]
                    onPicked: v => editor.setProp("constraintH", v)
                }
                Dropdown {
                    Layout.fillWidth: true
                    current: root.p.constraintV
                    model: [{ value: "TOP", text: qsTr("Top") }, { value: "BOTTOM", text: qsTr("Bottom") }, { value: "TOP_BOTTOM", text: qsTr("Top & bottom") },
                        { value: "CENTER", text: qsTr("Center") }, { value: "SCALE", text: qsTr("Scale") }]
                    onPicked: v => editor.setProp("constraintV", v)
                }
            }
        }

        // ---------------- Layer
        Section {
            visible: root.has
            title: qsTr("Layer")
            RowLayout {
                Layout.fillWidth: true
                Dropdown {
                    Layout.fillWidth: true
                    current: root.p.blend
                    model: ["PASS_THROUGH", "NORMAL", "DARKEN", "MULTIPLY", "COLOR_BURN", "LIGHTEN", "SCREEN", "COLOR_DODGE", "OVERLAY",
                        "SOFT_LIGHT", "HARD_LIGHT", "DIFFERENCE", "EXCLUSION", "HUE", "SATURATION", "COLOR", "LUMINOSITY"]
                        .map(v => ({ value: v, text: v.charAt(0) + v.slice(1).toLowerCase().replace(/_/g, " ") }))
                    onPicked: v => editor.setProp("blend", v)
                }
                NumberField { Layout.preferredWidth: 70; icon: "effects"; key: "opacity"; bindKey: "opacity"; value: root.p.opacity; displayScale: 100; suffix: "%"; decimals: 0; minimum: 0; maximum: 1 }
                IconButton { iconName: root.p.visible === false ? "eyeOff" : "eye"; onClicked: editor.toggleVisible() }
                IconButton {
                    iconName: "variable"
                    iconSize: 13
                    checked: !!root.p.bindings && !!root.p.bindings.visible
                    tip: qsTr("Bind visibility to a boolean variable")
                    onClicked: { visPicker.current = root.p.bindings ? (root.p.bindings.visible || "") : ""; visPicker.open() }
                    VariablePicker { id: visPicker; y: parent.height + 2; x: -width + parent.width; varType: "BOOLEAN"; onPicked: id => editor.bindVariable("visible", id) }
                }
            }
        }

        // ---------------- Variable modes
        Section {
            id: modesSection
            readonly property var multi: editor.library.collections.filter(c => c.modes.length > 1)
            visible: root.has && multi.length > 0 && (root.frameLike || root.type === "GROUP" || root.type === "SECTION")
            title: qsTr("Variable modes")
            Repeater {
                model: modesSection.multi
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Text { text: modelData.name; color: Theme.textDim; font.pixelSize: Theme.fontSize; Layout.preferredWidth: 90; elide: Text.ElideRight }
                    Dropdown {
                        Layout.fillWidth: true
                        current: root.p.variableModes ? (root.p.variableModes[modelData.id] || "") : null
                        model: [{ value: "", text: qsTr("Auto (inherit)") }].concat(modelData.modes.map(m => ({ value: m.id, text: m.name })))
                        onPicked: v => editor.setVariableMode(modelData.id, v)
                    }
                }
            }
        }

        // ---------------- Text
        TextSection { visible: root.has && root.isText }

        PaintList { visible: root.has && root.type !== "GROUP"; title: qsTr("Fill"); key: "fills" }

        PaintList {
            id: strokes
            visible: root.has && root.type !== "GROUP"
            title: qsTr("Stroke")
            key: "strokes"
            GridLayout {
                visible: !!strokes.paints && strokes.paints.length > 0
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 6
                rowSpacing: 4
                Dropdown {
                    Layout.fillWidth: true
                    current: root.p.strokeAlign
                    model: [{ value: "INSIDE", text: qsTr("Inside") }, { value: "CENTER", text: qsTr("Center") }, { value: "OUTSIDE", text: qsTr("Outside") }]
                    onPicked: v => editor.setProp("strokeAlign", v)
                }
                NumberField { Layout.fillWidth: true; icon: "minus"; key: "strokeWeight"; bindKey: "strokeWeight"; value: root.p.strokeWeight; minimum: 0 }
                Dropdown {
                    Layout.fillWidth: true
                    current: root.p.strokeJoin
                    model: [{ value: "MITER", text: qsTr("Miter join") }, { value: "BEVEL", text: qsTr("Bevel join") }, { value: "ROUND", text: qsTr("Round join") }]
                    onPicked: v => editor.setProp("strokeJoin", v)
                }
                TextBox {
                    Layout.fillWidth: true
                    placeholder: qsTr("Dash, gap")
                    text: (root.p.dashes || []).join(", ")
                    onAccepted: t => editor.setProp("dashes", t.split(/[ ,]+/).map(Number).filter(x => x > 0))
                }
                Dropdown {
                    visible: root.type === "LINE" || root.type === "VECTOR"
                    Layout.fillWidth: true
                    current: root.p.startCap
                    model: capModel
                    onPicked: v => editor.setProp("startCap", v)
                }
                Dropdown {
                    visible: root.type === "LINE" || root.type === "VECTOR"
                    Layout.fillWidth: true
                    current: root.p.endCap
                    model: capModel
                    onPicked: v => editor.setProp("endCap", v)
                }
            }
        }

        EffectsSection { visible: root.has }

        // ---------------- Layout grids
        Section {
            visible: root.has && root.frameLike
            title: qsTr("Layout grid")
            showAdd: true
            actions: [
                IconButton { iconName: "style"; iconSize: 14; tip: qsTr("Grid styles"); onClicked: { gridStyles.current = root.p.gridStyle || ""; gridStyles.open() }
                    StylePicker { id: gridStyles; slot: "grids"; x: -width - 20 } }
            ]
            StyleChip { slot: "grids"; styleId: root.p.gridStyle || "" }
            onAdd: {
                const g = (root.p.grids || []).slice()
                g.push({ kind: "COLUMNS", count: 12, size: 10, gutter: 20, margin: 0, color: Qt.rgba(1, 0, 0, 0.1), visible: true })
                editor.setProp("grids", g)
            }
            Repeater {
                model: root.p.grids || []
                RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    function set(k, v) { const g = root.p.grids.map(x => Object.assign({}, x)); g[index][k] = v; editor.setProp("grids", g) }
                    Dropdown {
                        Layout.preferredWidth: 90
                        current: modelData.kind
                        model: [{ value: "COLUMNS", text: qsTr("Columns") }, { value: "ROWS", text: qsTr("Rows") }, { value: "GRID", text: qsTr("Grid") }]
                        onPicked: v => parent.set("kind", v)
                    }
                    NumberField { Layout.fillWidth: true; label: "#"; visible: modelData.kind !== "GRID"; value: modelData.count; decimals: 0; minimum: 1; onCommitted: v => parent.set("count", v) }
                    NumberField { Layout.fillWidth: true; label: "G"; visible: modelData.kind !== "GRID"; value: modelData.gutter; minimum: 0; onCommitted: v => parent.set("gutter", v) }
                    NumberField { Layout.fillWidth: true; label: "S"; visible: modelData.kind === "GRID"; value: modelData.size; minimum: 1; onCommitted: v => parent.set("size", v) }
                    IconButton { iconName: modelData.visible ? "eye" : "eyeOff"; onClicked: parent.set("visible", !modelData.visible) }
                    IconButton { iconName: "minus"; onClicked: { const g = root.p.grids.slice(); g.splice(index, 1); editor.setProp("grids", g) } }
                }
            }
        }

        ExportSection { }
        Item { Layout.preferredHeight: 40 }
    }

    readonly property var capModel: [{ value: "NONE", text: qsTr("None") }, { value: "ROUND", text: qsTr("Round") },
        { value: "SQUARE", text: qsTr("Square") }, { value: "LINE_ARROW", text: qsTr("Line arrow") },
        { value: "TRIANGLE_ARROW", text: qsTr("Triangle arrow") }, { value: "CIRCLE_FILLED", text: qsTr("Circle") },
        { value: "DIAMOND_FILLED", text: qsTr("Diamond") }]
}
