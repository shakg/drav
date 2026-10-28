#pragma once

#include <include/core/SkColor.h>
#include <include/core/SkMatrix.h>
#include <include/core/SkPath.h>
#include <include/core/SkRect.h>

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
};

const char* nodeTypeName(NodeType t);
NodeType nodeTypeFromName(const QString& s);

// Containers lay out and clip children; groups only wrap them.
inline bool isFrameLike(NodeType t)
{
    return t == NodeType::Frame || t == NodeType::Component || t == NodeType::ComponentSet
        || t == NodeType::Instance;
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
    QString colorVar; // bound color variable id (solid paints)
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

enum class LayoutMode : std::uint8_t { None, Horizontal, Vertical };
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
    bool operator==(const AutoLayout&) const = default;
};

// Per-child layout participation.
struct LayoutChild {
    LayoutSizing sizingH = LayoutSizing::Fixed;
    LayoutSizing sizingV = LayoutSizing::Fixed;
    bool absolute = false;
    float minW = 0, maxW = 0, minH = 0, maxH = 0; // 0 = unset
    bool operator==(const LayoutChild&) const = default;
};

enum class TextAlignH : std::uint8_t { Left, Center, Right, Justified };
enum class TextAlignV : std::uint8_t { Top, Center, Bottom };
enum class TextAutoResize : std::uint8_t { None, Height, WidthAndHeight, Truncate };
enum class TextCase : std::uint8_t { Original, Upper, Lower, Title };
enum class TextDecoration : std::uint8_t { None, Underline, Strikethrough };

struct TextStyle {
    QString family = QStringLiteral("Inter");
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
struct VectorData {
    std::vector<Contour> contours;
    SkPathFillType fill = SkPathFillType::kWinding;
    bool operator==(const VectorData&) const = default;
    SkPath toPath() const;
    void transform(const SkMatrix& m);
    SkRect bounds() const;
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
