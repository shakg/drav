#pragma once

#include <include/core/SkColor.h>
#include <include/core/SkMatrix.h>
#include <include/core/SkPath.h>
#include <include/core/SkRect.h>

#include <QMap>
#include <QString>

#include <array>
#include <cstdint>
#include <vector>

namespace drav {

using NodeId = std::uint64_t;
constexpr NodeId kNoNode = 0;

enum class NodeType : std::uint8_t {
    Document,
    Page,
    Frame,
    Group,
    Rectangle,
    Ellipse,
    Polygon,
    Star,
    Line,
    Vector,
    Text,
    BooleanOp,
    Component,
    ComponentSet,
    Instance,
    Section, // canvas organizer: holds frames, labelled, no auto layout
};

const char* nodeTypeName(NodeType t);
NodeType nodeTypeFromName(const QString& s);

// Containers lay out and clip children; groups only wrap them.
inline bool isFrameLike(NodeType t)
{
    return t == NodeType::Frame || t == NodeType::Component || t == NodeType::ComponentSet
        || t == NodeType::Instance || t == NodeType::Section;
}
inline bool isContainer(NodeType t)
{
    return isFrameLike(t) || t == NodeType::Group || t == NodeType::BooleanOp || t == NodeType::Page
        || t == NodeType::Document;
}

enum class BlendMode : std::uint8_t {
    PassThrough,
    Normal,
    Darken,
    Multiply,
    ColorBurn,
    Lighten,
    Screen,
    ColorDodge,
    Overlay,
    SoftLight,
    HardLight,
    Difference,
    Exclusion,
    Hue,
    Saturation,
    Color,
    Luminosity,
};

struct ColorStop {
    float pos = 0;
    SkColor4f color = SkColors::kBlack;
    bool operator==(const ColorStop&) const = default;
};

// Color space a document's colors are defined in. Display P3 uses the sRGB transfer curve.
enum class ColorProfile : std::uint8_t { SRGB, DisplayP3 };

enum class PaintType : std::uint8_t { Solid, LinearGradient, RadialGradient, AngularGradient, DiamondGradient, Image };
enum class ImageScaleMode : std::uint8_t { Fill, Fit, Crop, Tile };

struct Paint {
    PaintType type = PaintType::Solid;
    SkColor4f color = {0.85f, 0.85f, 0.85f, 1.f};
    float opacity = 1.f;
    bool visible = true;
    BlendMode blend = BlendMode::Normal;
    // Gradient handles in unit space of the node's bounds (0..1).
    SkPoint gradStart = {0, 0.5f};
    SkPoint gradEnd = {1, 0.5f};
    float gradWidth = 1.f; // radial/diamond: size of the second axis relative to the first.
    std::vector<ColorStop> stops = {{0, SkColors::kWhite}, {1, SkColors::kBlack}};
    // Image paints reference the document image store by hash.
    QString imageHash;
    ImageScaleMode scaleMode = ImageScaleMode::Fill;
    float imageScale = 1.f;
    // Image adjustments, -1..1 (0 = unchanged).
    float exposure = 0, contrast = 0, saturation = 0, temperature = 0, tint = 0, highlights = 0, shadows = 0;
    // Crop mode: the part of the image (normalized 0..1 coordinates) shown in the layer's box.
    SkRect crop = SkRect::MakeWH(1, 1);
    float strokeWeight = 0; // strokes: this stroke's own weight (0 = the layer's weight)
    QString colorVar; // bound color variable id (solid paints)
    bool hasAdjustments() const
    {
        return exposure || contrast || saturation || temperature || tint || highlights || shadows;
    }
    bool operator==(const Paint&) const = default;

    static Paint solid(SkColor4f c)
    {
        Paint p;
        p.color = c;
        return p;
    }
};

enum class StrokeAlign : std::uint8_t { Inside, Center, Outside };
enum class StrokeCap : std::uint8_t { None, Round, Square, LineArrow, TriangleArrow, CircleArrow, DiamondArrow };
enum class StrokeJoin : std::uint8_t { Miter, Bevel, Round };

struct StrokeStyle {
    float weight = 1.f;
    StrokeAlign align = StrokeAlign::Inside;
    StrokeCap startCap = StrokeCap::None;
    StrokeCap endCap = StrokeCap::None;
    StrokeJoin join = StrokeJoin::Miter;
    float miterLimit = 4.f;
    std::vector<float> dashes; // empty = solid
    // Variable width: (position along the path 0..1, width multiplier) pairs, sorted. Empty = uniform.
    std::vector<SkPoint> widthProfile;
    bool operator==(const StrokeStyle&) const = default;
};

enum class EffectType : std::uint8_t { DropShadow, InnerShadow, LayerBlur, BackgroundBlur };

struct Effect {
    EffectType type = EffectType::DropShadow;
    bool visible = true;
    SkColor4f color = {0, 0, 0, 0.25f};
    SkPoint offset = {0, 4};
    float radius = 4.f;
    float spread = 0.f;
    BlendMode blend = BlendMode::Normal;
    bool showBehindTransparent = false;
    QString colorVar; // bound color variable id
    bool operator==(const Effect&) const = default;
};

enum class ConstraintH : std::uint8_t { Left, Right, LeftRight, Center, Scale };
enum class ConstraintV : std::uint8_t { Top, Bottom, TopBottom, Center, Scale };

enum class LayoutMode : std::uint8_t { None, Horizontal, Vertical, Grid };
enum class LayoutAlign : std::uint8_t { Min, Center, Max, SpaceBetween, Baseline };
enum class LayoutSizing : std::uint8_t { Fixed, Hug, Fill };

struct AutoLayout {
    LayoutMode mode = LayoutMode::None;
    bool wrap = false;
    float spacing = 0;
    float counterSpacing = 0;
    float padL = 0, padT = 0, padR = 0, padB = 0;
    LayoutAlign primaryAlign = LayoutAlign::Min;
    LayoutAlign counterAlign = LayoutAlign::Min;
    bool reverseZ = false;
    bool strokesIncluded = false;
    // Grid: column count and row count (0 = as many rows as needed). Gaps: spacing (columns),
    // counterSpacing (rows). primaryAlign/counterAlign place children inside their cells.
    int gridColumns = 2;
    int gridRows = 0;
    bool operator==(const AutoLayout&) const = default;
};

// Per-child layout participation.
struct LayoutChild {
    LayoutSizing sizingH = LayoutSizing::Fixed;
    LayoutSizing sizingV = LayoutSizing::Fixed;
    bool absolute = false;
    float minW = 0, maxW = 0, minH = 0, maxH = 0; // 0 = unset
    int colSpan = 1, rowSpan = 1; // in a grid
    bool operator==(const LayoutChild&) const = default;
};

enum class TextAlignH : std::uint8_t { Left, Center, Right, Justified };
enum class TextAlignV : std::uint8_t { Top, Center, Bottom };
enum class TextAutoResize : std::uint8_t { None, Height, WidthAndHeight, Truncate };
enum class TextCase : std::uint8_t { Original, Upper, Lower, Title };
enum class TextDecoration : std::uint8_t { None, Underline, Strikethrough };

struct TextStyle {
    QString family = QStringLiteral("Inter");
    // OpenType features that differ from the font's defaults ("tnum" -> 1, "liga" -> 0, ...)
    // and variable font axes other than weight ("wdth" -> 87.5, ...).
    QMap<QString, int> features;
    QMap<QString, float> axes;
    int weight = 400;
    bool italic = false;
    float size = 16.f;
    float lineHeight = 0; // 0 = auto
    bool lineHeightPercent = false;
    float letterSpacing = 0; // percent of font size
    float paragraphSpacing = 0;
    TextAlignH alignH = TextAlignH::Left;
    TextAlignV alignV = TextAlignV::Top;
    TextAutoResize autoResize = TextAutoResize::WidthAndHeight;
    TextCase textCase = TextCase::Original;
    TextDecoration decoration = TextDecoration::None;
    int maxLines = 0;
    bool operator==(const TextStyle&) const = default;
};

// Character style override for [start, end) of a text layer's characters (UTF-16 indices).
// Only the fields named in `mask` apply; the rest come from the layer's TextStyle and fills.
enum TextRunField : std::uint32_t {
    RunFamily = 1u << 0,
    RunWeight = 1u << 1,
    RunItalic = 1u << 2,
    RunSize = 1u << 3,
    RunLetterSpacing = 1u << 4,
    RunDecoration = 1u << 5,
    RunCase = 1u << 6,
    RunFill = 1u << 7,
    RunLink = 1u << 8,
};
struct TextRun {
    int start = 0, end = 0;
    std::uint32_t mask = 0;
    QString family;
    int weight = 400;
    bool italic = false;
    float size = 16;
    float letterSpacing = 0;
    TextDecoration decoration = TextDecoration::None;
    TextCase textCase = TextCase::Original;
    SkColor4f fill = SkColors::kBlack;
    QString link;
    bool operator==(const TextRun&) const = default;
    bool sameStyle(const TextRun& o) const
    {
        TextRun a = *this, b = o;
        a.start = b.start = a.end = b.end = 0;
        return a == b;
    }
};

enum class ListType : std::uint8_t { None, Bullet, Ordered };
struct ParagraphAttr {
    ListType list = ListType::None;
    int indent = 0; // list nesting level
    bool operator==(const ParagraphAttr&) const = default;
};

enum class BooleanOpType : std::uint8_t { Union, Subtract, Intersect, Exclude };

// Cubic bezier contour point. Handles are absolute positions in node-local space.
enum class AnchorMode : std::uint8_t { Corner, Mirrored, Asymmetric, Disconnected };
struct Anchor {
    SkPoint p{};
    SkPoint in{};
    SkPoint out{};
    AnchorMode mode = AnchorMode::Corner;
    float cornerRadius = 0;
    bool hasIn() const { return in != p; }
    bool hasOut() const { return out != p; }
    bool operator==(const Anchor&) const = default;
};
struct Contour {
    std::vector<Anchor> pts;
    bool closed = false;
    bool operator==(const Contour&) const = default;
};
// Vector network: vertices joined by cubic segments; a vertex can join any number of segments.
// Regions are loops of segments that get filled (a loop that doesn't return to its start is
// closed implicitly, like an open path with a fill).
struct NetVertex {
    SkPoint p{};
    AnchorMode mode = AnchorMode::Corner;
    bool operator==(const NetVertex&) const = default;
};
struct NetSegment {
    int a = 0, b = 0;
    SkPoint ca{}, cb{}; // control points; equal to the vertex point for a straight end
    bool operator==(const NetSegment&) const = default;
};
struct NetRegion {
    std::vector<std::vector<int>> loops; // segment indices, in walking order
    bool operator==(const NetRegion&) const = default;
};

struct VectorData {
    std::vector<Contour> contours;
    SkPathFillType fill = SkPathFillType::kWinding;
    // Network form; when it has segments it replaces `contours`.
    std::vector<NetVertex> vertices;
    std::vector<NetSegment> segments;
    std::vector<NetRegion> regions;
    bool operator==(const VectorData&) const = default;
    bool isNetwork() const { return !segments.empty() || !vertices.empty(); }
    SkPath toPath() const;     // fill geometry
    SkPath strokePath() const; // stroke geometry (all segments, chained through simple vertices)
    void transform(const SkMatrix& m);
    SkRect bounds() const;

    // ---- network editing (VectorNetwork.cpp) ----
    VectorData asNetwork() const; // same shape as a network
    std::vector<int> segmentsAt(int vertex) const;
    // Splits segment s at t; returns the new vertex.
    int splitSegment(int s, float t);
    // Adds a straight or curved segment; if it closes a loop and `fillLoops`, the loop is filled.
    int addSegment(int a, int b, SkPoint ca, SkPoint cb, bool fillLoops);
    void removeVertices(std::vector<int> vertices);
    // Closed faces of the network (as segment loops) and toggling the fill of the face at a point.
    std::vector<std::vector<int>> faces() const;
    bool toggleRegionAt(SkPoint p);
    SkPath loopPath(const std::vector<int>& loop) const;
};

struct CornerRadii {
    float tl = 0, tr = 0, br = 0, bl = 0;
    bool uniform() const { return tl == tr && tr == br && br == bl; }
    bool operator==(const CornerRadii&) const = default;
};

struct ExportSetting {
    QString format = QStringLiteral("PNG"); // PNG, JPG, SVG, PDF
    float scale = 1.f;
    QString suffix;
    bool operator==(const ExportSetting&) const = default;
};

// Ruler guide on a page (page coordinates).
struct Guide {
    bool vertical = true; // vertical line at x = pos, else horizontal at y = pos
    float pos = 0;
    bool operator==(const Guide&) const = default;
};

// Component properties: defined on a component or component set, linked from layers inside it,
// set per instance. Values are strings: "true"/"false", the text, or a component's node id.
enum class ComponentPropType : std::uint8_t { Boolean, Text, InstanceSwap };
struct ComponentProp {
    QString id;
    QString name;
    ComponentPropType type = ComponentPropType::Boolean;
    QString defaultValue;
    std::vector<std::uint64_t> preferred; // instance swap: suggested components
    bool operator==(const ComponentProp&) const = default;
};

struct LayoutGrid {
    enum class Kind : std::uint8_t { Grid, Columns, Rows } kind = Kind::Columns;
    int count = 12;
    float size = 10; // grid cell size
    float gutter = 20;
    float margin = 0;
    SkColor4f color = {1, 0, 0, 0.1f};
    bool visible = true;
    bool operator==(const LayoutGrid&) const = default;
};

} // namespace drav
