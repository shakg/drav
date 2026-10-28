#include "Geometry.h"

#include "Document.h"

#include <include/core/SkPathBuilder.h>
#include <include/core/SkPathUtils.h>
#include <include/core/SkPaint.h>
#include <include/core/SkRRect.h>
#include <include/core/SkStrokeRec.h>
#include <include/core/SkPathEffect.h>
#include <include/effects/SkCornerPathEffect.h>
#include <include/pathops/SkPathOps.h>

#include <cmath>

namespace drav {

namespace {

constexpr float kPi = 3.14159265358979f;
float rad(float deg) { return deg * kPi / 180.f; }

struct CornerParams {
    float a, b, c, d, p, arcLen, radius;
};

// Corner smoothing as described in Figma's "Desperately seeking squircles".
CornerParams cornerParams(float radius, float smoothing, float budget)
{
    radius = std::min(radius, budget);
    if (radius > 0)
        smoothing = std::min(smoothing, std::max(0.f, budget / radius - 1));
    float p = std::min((1 + smoothing) * radius, budget);
    float arcMeasure = 90 * (1 - smoothing);
    float arcLen = std::sin(rad(arcMeasure / 2)) * radius * std::sqrt(2.f);
    float alpha = (90 - arcMeasure) / 2;
    float p3p4 = radius * std::tan(rad(alpha / 2));
    float beta = 45 * smoothing;
    float c = p3p4 * std::cos(rad(beta));
    float d = c * std::tan(rad(beta));
    float b = (p - arcLen - c - d) / 3;
    float a = 2 * b;
    return {a, b, c, d, p, arcLen, radius};
}

void corner(SkPathBuilder& pb, SkPoint k, SkVector u, SkVector v, const CornerParams& cp, bool first)
{
    auto at = [&](float su, float sv) { return SkPoint{k.x() + u.x() * su + v.x() * sv, k.y() + u.y() * su + v.y() * sv}; };
    if (cp.radius <= 0) {
        if (first)
            pb.moveTo(k);
        else
            pb.lineTo(k);
        return;
    }
    SkPoint start = at(-cp.p, 0);
    if (first)
        pb.moveTo(start);
    else
        pb.lineTo(start);
    SkPoint e1 = at(-(cp.p - cp.a - cp.b - cp.c), cp.d);
    pb.cubicTo(at(-(cp.p - cp.a), 0), at(-(cp.p - cp.a - cp.b), 0), e1);
    SkPoint e2 = at(-cp.d, cp.p - cp.a - cp.b - cp.c);
    pb.arcTo({cp.radius, cp.radius}, 0, SkPathBuilder::kSmall_ArcSize, SkPathDirection::kCW, e2);
    pb.cubicTo(at(0, cp.p - cp.a - cp.b), at(0, cp.p - cp.a), at(0, cp.p));
}

SkPath regularPolygon(int count, float w, float h, bool star, float inner)
{
    count = std::max(3, count);
    const int n = star ? count * 2 : count;
    std::vector<SkPoint> pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i) {
        float r = (star && (i % 2)) ? inner : 1.f;
        float ang = -kPi / 2 + i * 2 * kPi / n;
        pts.push_back({r * std::cos(ang), r * std::sin(ang)});
    }
    // Fit the unit shape's bounds to the box.
    SkRect b;
    b.setBounds(pts.data(), (int)pts.size());
    for (SkPoint& p : pts)
        p = {(p.x() - b.left()) / b.width() * w, (p.y() - b.top()) / b.height() * h};
    return SkPath::Polygon(pts.data(), (int)pts.size(), true);
}

SkPath ellipsePath(const Props& p)
{
    SkRect oval = SkRect::MakeWH(p.width, p.height);
    const bool full = std::fabs(p.arcSweep) >= 360;
    if (full && p.arcInner <= 0)
        return SkPath::Oval(oval);
    SkPathBuilder b;
    float inner = std::clamp(p.arcInner, 0.f, 1.f);
    SkRect in = oval;
    in.inset(oval.width() * (1 - inner) / 2, oval.height() * (1 - inner) / 2);
    if (full) {
        b.addOval(oval, SkPathDirection::kCW);
        b.addOval(in, SkPathDirection::kCCW);
        return b.detach();
    }
    b.arcTo(oval, p.arcStart, p.arcSweep, true);
    if (inner > 0)
        b.arcTo(in, p.arcStart + p.arcSweep, -p.arcSweep, false);
    else
        b.lineTo(oval.center());
    b.close();
    return b.detach();
}

SkPath withCorners(SkPath path, float radius)
{
    if (radius <= 0)
        return path;
    SkPath out;
    SkStrokeRec rec(SkStrokeRec::kFill_InitStyle);
    if (SkCornerPathEffect::Make(radius)->filterPath(&out, path, &rec, nullptr))
        return out;
    return path;
}

} // namespace

SkPath smoothRect(float w, float h, const CornerRadii& r, float smoothing)
{
    if (smoothing <= 0) {
        SkVector radii[4] = {{r.tl, r.tl}, {r.tr, r.tr}, {r.br, r.br}, {r.bl, r.bl}};
        SkRRect rr;
        rr.setRectRadii(SkRect::MakeWH(w, h), radii);
        return SkPath::RRect(rr);
    }
    const float budget = std::min(w, h) / 2;
    SkPathBuilder pb;
    corner(pb, {w, 0}, {1, 0}, {0, 1}, cornerParams(r.tr, smoothing, budget), true);
    corner(pb, {w, h}, {0, 1}, {-1, 0}, cornerParams(r.br, smoothing, budget), false);
    corner(pb, {0, h}, {-1, 0}, {0, -1}, cornerParams(r.bl, smoothing, budget), false);
    corner(pb, {0, 0}, {0, -1}, {1, 0}, cornerParams(r.tl, smoothing, budget), false);
    pb.close();
    return pb.detach();
}

SkPath fillPath(const Document& doc, NodeId id)
{
    const Node& n = doc.at(id);
    const Props& p = n.p;
    switch (n.type()) {
    case NodeType::Rectangle:
    case NodeType::Frame:
    case NodeType::Component:
    case NodeType::ComponentSet:
    case NodeType::Instance:
    case NodeType::Section:
        return smoothRect(p.width, p.height, p.corners, p.cornerSmoothing);
    case NodeType::Ellipse:
        return ellipsePath(p);
    case NodeType::Polygon:
        return withCorners(regularPolygon(p.pointCount, p.width, p.height, false, 0), p.corners.tl);
    case NodeType::Star:
        return withCorners(regularPolygon(p.pointCount, p.width, p.height, true, p.starInner), p.corners.tl);
    case NodeType::Line: {
        SkPathBuilder b;
        b.moveTo(0, 0);
        b.lineTo(p.width, 0);
        return b.detach();
    }
    case NodeType::Vector:
        return withCorners(p.vector.toPath(), p.corners.tl);
    case NodeType::Text:
        return SkPath::Rect(SkRect::MakeWH(p.width, p.height));
    case NodeType::BooleanOp: {
        SkPath result;
        bool first = true;
        for (NodeId c : n.children) {
            const Node& cn = doc.at(c);
            if (!cn.p.visible)
                continue;
            SkPath cp = fillPath(doc, c).makeTransform(cn.p.transform);
            if (first) {
                result = cp;
                first = false;
                continue;
            }
            SkPathOp op = kUnion_SkPathOp;
            switch (p.boolOp) {
            case BooleanOpType::Union: op = kUnion_SkPathOp; break;
            case BooleanOpType::Subtract: op = kDifference_SkPathOp; break;
            case BooleanOpType::Intersect: op = kIntersect_SkPathOp; break;
            case BooleanOpType::Exclude: op = kXOR_SkPathOp; break;
            }
            SkPath out;
            if (Op(result, cp, op, &out))
                result = out;
        }
        return result;
    }
    case NodeType::Group: {
        SkPath result;
        for (NodeId c : n.children)
            result.addPath(fillPath(doc, c), doc.at(c).p.transform);
        return result;
    }
    default:
        return {};
    }
}

SkPath strokePath(const Document& doc, NodeId id)
{
    const Props& p = doc.at(id).p;
    if (p.type == NodeType::Vector && p.vector.isNetwork())
        return withCorners(p.vector.strokePath(), p.corners.tl);
    return fillPath(doc, id);
}

SkPath strokeOutline(const Props& p, const SkPath& fill)
{
    SkPaint paint;
    paint.setStyle(SkPaint::kStroke_Style);
    const float w = p.stroke.align == StrokeAlign::Center ? p.stroke.weight : p.stroke.weight * 2;
    paint.setStrokeWidth(std::max(w, 0.01f));
    paint.setStrokeJoin(p.stroke.join == StrokeJoin::Round ? SkPaint::kRound_Join
            : p.stroke.join == StrokeJoin::Bevel           ? SkPaint::kBevel_Join
                                                           : SkPaint::kMiter_Join);
    paint.setStrokeMiter(p.stroke.miterLimit);
    paint.setStrokeCap(p.stroke.startCap == StrokeCap::Round ? SkPaint::kRound_Cap
            : p.stroke.startCap == StrokeCap::Square         ? SkPaint::kSquare_Cap
                                                             : SkPaint::kButt_Cap);
    SkPath out;
    skpathutils::FillPathWithPaint(fill, paint, &out);
    if (p.stroke.align == StrokeAlign::Center || fill.isLastContourClosed() == false)
        return out;
    SkPath clipped;
    if (Op(out, fill, p.stroke.align == StrokeAlign::Inside ? kIntersect_SkPathOp : kDifference_SkPathOp, &clipped))
        return clipped;
    return out;
}

VectorData vectorFromPath(const SkPath& path)
{
    VectorData v;
    v.fill = path.getFillType();
    SkPath::Iter it(path, false);
    SkPoint pts[4];
    Contour cur;
    auto flush = [&] {
        if (!cur.pts.empty()) {
            // A closing segment that returns onto the first point duplicates it.
            if (cur.closed && cur.pts.size() > 1 && cur.pts.back().p == cur.pts.front().p) {
                cur.pts.front().in = cur.pts.back().in;
                cur.pts.pop_back();
            }
            v.contours.push_back(cur);
        }
        cur = {};
    };
    for (SkPath::Verb verb; (verb = it.next(pts)) != SkPath::kDone_Verb;) {
        switch (verb) {
        case SkPath::kMove_Verb:
            flush();
            cur.pts.push_back({pts[0], pts[0], pts[0]});
            break;
        case SkPath::kLine_Verb:
            cur.pts.push_back({pts[1], pts[1], pts[1]});
            break;
        case SkPath::kQuad_Verb: {
            SkPoint c1 = pts[0] + (pts[1] - pts[0]) * (2.f / 3.f);
            SkPoint c2 = pts[2] + (pts[1] - pts[2]) * (2.f / 3.f);
            cur.pts.back().out = c1;
            cur.pts.push_back({pts[2], c2, pts[2], AnchorMode::Asymmetric});
            break;
        }
        case SkPath::kConic_Verb: {
            // Approximate conics with quads, then elevate to cubics.
            SkPoint q[1 + 2 * 4];
            const int count = SkPath::ConvertConicToQuads(pts[0], pts[1], pts[2], it.conicWeight(), q, 2);
            for (int i = 0; i < count; ++i) {
                const SkPoint* qq = q + i * 2;
                SkPoint c1 = qq[0] + (qq[1] - qq[0]) * (2.f / 3.f);
                SkPoint c2 = qq[2] + (qq[1] - qq[2]) * (2.f / 3.f);
                cur.pts.back().out = c1;
                cur.pts.push_back({qq[2], c2, qq[2], AnchorMode::Asymmetric});
            }
            break;
        }
        case SkPath::kCubic_Verb:
            cur.pts.back().out = pts[1];
            cur.pts.push_back({pts[3], pts[2], pts[3], AnchorMode::Asymmetric});
            break;
        case SkPath::kClose_Verb:
            cur.closed = true;
            break;
        case SkPath::kDone_Verb:
            break;
        }
    }
    flush();
    return v;
}

} // namespace drav
