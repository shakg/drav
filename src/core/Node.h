#pragma once

#include "Types.h"

#include <QMap>
#include <QString>

#include <memory>

namespace drav {

// Bits naming groups of properties that an instance child can override.
enum OverrideBit : std::uint32_t {
    OvName = 1u << 0,
    OvVisible = 1u << 1,
    OvOpacity = 1u << 2,
    OvFills = 1u << 3,
    OvStrokes = 1u << 4,
    OvStrokeStyle = 1u << 5,
    OvEffects = 1u << 6,
    OvText = 1u << 7,
    OvTextStyle = 1u << 8,
    OvCorners = 1u << 9,
    OvLayout = 1u << 10,
    OvSize = 1u << 11,
    OvBlend = 1u << 12,
    OvComponent = 1u << 13, // nested instance swapped
    OvVariantProps = 1u << 14,
    OvBindings = 1u << 15,
    OvPropValues = 1u << 16, // nested instance: component property values
};

struct Library;

// All per-node data except tree links. Copyable so undo can snapshot it.
struct Props {
    NodeType type = NodeType::Rectangle;
    QString name;
    bool visible = true;
    bool locked = false;
    float opacity = 1.f;
    BlendMode blend = BlendMode::PassThrough;
    bool isMask = false;

    // Geometry. transform maps local (0,0)-(w,h) into parent space; contains translation, rotation, flips.
    SkMatrix transform = SkMatrix::I();
    float width = 100, height = 100;
    bool constrainProportions = false;
    ConstraintH constraintH = ConstraintH::Left;
    ConstraintV constraintV = ConstraintV::Top;

    std::vector<Paint> fills;
    std::vector<Paint> strokes;
    StrokeStyle stroke;
    std::vector<Effect> effects;

    CornerRadii corners;
    float cornerSmoothing = 0;

    // Frame
    bool clipsContent = true;
    AutoLayout layout;
    std::vector<LayoutGrid> grids;
    // As a child of an auto layout frame
    LayoutChild layoutChild;

    // Ellipse arc
    float arcStart = 0, arcSweep = 360, arcInner = 0; // degrees, inner ratio 0..1
    // Polygon / star
    int pointCount = 5;
    float starInner = 0.382f;

    VectorData vector;

    QString text;
    TextStyle textStyle;
    std::vector<TextRun> textRuns;        // sorted, non-overlapping character overrides
    std::vector<ParagraphAttr> paragraphs; // per paragraph (split at '\n'); missing = defaults

    BooleanOpType boolOp = BooleanOpType::Union;

    // Components
    NodeId componentId = kNoNode; // Instance -> its main component
    NodeId sourceId = kNoNode;    // node inside an instance -> its counterpart in the main component
    std::uint32_t overrides = 0;
    QString description; // component docs
    QMap<QString, QString> variantProps; // instance: chosen values; component: own values (parsed from name)
    std::vector<ComponentProp> componentProps; // Component / ComponentSet: property definitions
    QMap<QString, QString> propRefs;   // layer in a component: field ("visible", "characters", "mainComponent") -> prop id
    QMap<QString, QString> propValues; // Instance: prop id -> value (missing = default)

    std::vector<ExportSetting> exports;

    SkColor4f pageBackground = {0.118f, 0.118f, 0.118f, 1.f};
    std::vector<Guide> guides; // Page
    QString libraryPage; // Page: holds components imported from this linked library (hidden)
    QString remote;      // node imported from a linked library: "<library id>:<source node id>"

    // Styles and variables. Style ids travel with the property group they drive.
    QString fillStyle, strokeStyle, textStyleId, effectStyle, gridStyle;
    QMap<QString, QString> bindings;      // bindable field key -> variable id (see Variables.h)
    QMap<QString, QString> variableModes; // collection id -> mode id, inherited by descendants
    std::shared_ptr<const Library> library; // Document node only; replaced (never mutated) on edit

    bool operator==(const Props&) const = default;
};

std::uint32_t diffOverrideMask(const Props& a, const Props& b);
// Copies only the property groups named in `mask`.
void copyGroups(Props& dst, const Props& src, std::uint32_t mask);
// Copies main-component data into an instance node, skipping overridden groups.
// Instance roots keep their own placement (constraints, layout participation, exports).
void copyNonOverridden(Props& dst, const Props& src, std::uint32_t overrides, bool isRoot = false);

struct Node {
    NodeId id = kNoNode;
    NodeId parent = kNoNode;
    std::vector<NodeId> children;
    Props p;

    // Monotonic stamps from the document's edit counter; used for render caches.
    std::uint64_t version = 0;
    std::uint64_t subtreeVersion = 0;
    bool attached = false;

    NodeType type() const { return p.type; }
    SkRect localRect() const { return SkRect::MakeWH(p.width, p.height); }
};

} // namespace drav
