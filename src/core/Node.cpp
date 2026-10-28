#include "Node.h"

#include <include/core/SkPathBuilder.h>

namespace drav {

static const char* const kTypeNames[] = {
    "DOCUMENT", "PAGE", "FRAME", "GROUP", "RECTANGLE", "ELLIPSE", "POLYGON", "STAR",
    "LINE", "VECTOR", "TEXT", "BOOLEAN_OPERATION", "COMPONENT", "COMPONENT_SET", "INSTANCE",
};

const char* nodeTypeName(NodeType t) { return kTypeNames[int(t)]; }

NodeType nodeTypeFromName(const QString& s)
{
    for (int i = 0; i < int(std::size(kTypeNames)); ++i)
        if (s == QLatin1String(kTypeNames[i]))
            return NodeType(i);
    return NodeType::Rectangle;
}

std::uint32_t diffOverrideMask(const Props& a, const Props& b)
{
    std::uint32_t m = 0;
    if (a.name != b.name) m |= OvName;
    if (a.visible != b.visible) m |= OvVisible;
    if (a.opacity != b.opacity) m |= OvOpacity;
    if (a.blend != b.blend) m |= OvBlend;
    if (a.fills != b.fills) m |= OvFills;
    if (a.strokes != b.strokes) m |= OvStrokes;
    if (a.stroke != b.stroke) m |= OvStrokeStyle;
    if (a.effects != b.effects) m |= OvEffects;
    if (a.text != b.text) m |= OvText;
    if (a.textStyle != b.textStyle) m |= OvTextStyle;
    if (a.corners != b.corners || a.cornerSmoothing != b.cornerSmoothing) m |= OvCorners;
    if (a.layout != b.layout || a.clipsContent != b.clipsContent) m |= OvLayout;
    if (a.width != b.width || a.height != b.height) m |= OvSize;
    if (a.variantProps != b.variantProps) m |= OvVariantProps;
    return m;
}

void copyGroups(Props& dst, const Props& src, std::uint32_t mask)
{
    if (mask & OvName) dst.name = src.name;
    if (mask & OvVisible) dst.visible = src.visible;
    if (mask & OvOpacity) dst.opacity = src.opacity;
    if (mask & OvBlend) dst.blend = src.blend;
    if (mask & OvFills) dst.fills = src.fills;
    if (mask & OvStrokes) dst.strokes = src.strokes;
    if (mask & OvStrokeStyle) dst.stroke = src.stroke;
    if (mask & OvEffects) dst.effects = src.effects;
    if (mask & OvText) dst.text = src.text;
    if (mask & OvTextStyle) dst.textStyle = src.textStyle;
    if (mask & OvCorners) {
        dst.corners = src.corners;
        dst.cornerSmoothing = src.cornerSmoothing;
    }
    if (mask & OvLayout) {
        dst.layout = src.layout;
        dst.clipsContent = src.clipsContent;
    }
    if (mask & OvSize) {
        dst.width = src.width;
        dst.height = src.height;
        dst.vector = src.vector;
    }
    if (mask & OvComponent) dst.componentId = src.componentId;
    if (mask & OvVariantProps) dst.variantProps = src.variantProps;
}

void copyNonOverridden(Props& dst, const Props& src, std::uint32_t ov, bool isRoot)
{
    std::uint32_t mask = ~ov;
    if (isRoot)
        mask &= ~(OvComponent | OvVariantProps);
    copyGroups(dst, src, mask);
    dst.grids = src.grids;
    dst.arcStart = src.arcStart;
    dst.arcSweep = src.arcSweep;
    dst.arcInner = src.arcInner;
    dst.pointCount = src.pointCount;
    dst.starInner = src.starInner;
    dst.boolOp = src.boolOp;
    dst.description = src.description;
    if (!isRoot) {
        dst.isMask = src.isMask;
        dst.constraintH = src.constraintH;
        dst.constraintV = src.constraintV;
        dst.layoutChild = src.layoutChild;
        dst.exports = src.exports;
        dst.constrainProportions = src.constrainProportions;
    }
}

// ---------------------------------------------------------------- vector data

SkPath VectorData::toPath() const
{
    SkPathBuilder b(fill);
    for (const Contour& c : contours) {
        if (c.pts.empty())
            continue;
        b.moveTo(c.pts[0].p);
        const size_t n = c.pts.size();
        const size_t segs = c.closed ? n : n - 1;
        for (size_t i = 0; i < segs; ++i) {
            const Anchor& a = c.pts[i];
            const Anchor& z = c.pts[(i + 1) % n];
            if (!a.hasOut() && !z.hasIn())
                b.lineTo(z.p);
            else
                b.cubicTo(a.out, z.in, z.p);
        }
        if (c.closed)
            b.close();
    }
    return b.detach();
}

void VectorData::transform(const SkMatrix& m)
{
    for (Contour& c : contours)
        for (Anchor& a : c.pts) {
            a.p = m.mapPoint(a.p);
            a.in = m.mapPoint(a.in);
            a.out = m.mapPoint(a.out);
        }
}

SkRect VectorData::bounds() const
{
    return toPath().computeTightBounds();
}

} // namespace drav
