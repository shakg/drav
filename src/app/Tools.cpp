#include "Tools.h"

#include "core/Geometry.h"
#include "core/Text.h"

#include <include/core/SkCanvas.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPathBuilder.h>

#include <QClipboard>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>

#include <cmath>

namespace drav {

namespace {

constexpr SkColor kBlue = SkColorSetRGB(0x0D, 0x99, 0xFF);
constexpr float kPi = 3.14159265358979f;

float dist(QPointF a, QPointF b) { return float(std::hypot(a.x() - b.x(), a.y() - b.y())); }
float dist(SkPoint a, SkPoint b) { return SkPoint::Distance(a, b); }
QPointF qp(SkPoint p) { return {p.x(), p.y()}; }

SkPoint snap45(SkPoint origin, SkPoint p)
{
    SkVector d = p - origin;
    float len = d.length();
    float ang = std::atan2(d.y(), d.x());
    ang = std::round(ang / (kPi / 4)) * (kPi / 4);
    return origin + SkVector{std::cos(ang) * len, std::sin(ang) * len};
}

float distToSegment(QPointF p, QPointF a, QPointF b, float* t = nullptr)
{
    QPointF ab = b - a;
    double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    double u = len2 > 0 ? ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2 : 0;
    u = std::clamp(u, 0.0, 1.0);
    if (t)
        *t = float(u);
    return dist(p, a + ab * u);
}

void drawHandle(SkCanvas* c, SkPoint p, bool filled, bool round = false, float size = 6)
{
    SkPaint f, s;
    f.setAntiAlias(true);
    f.setColor(filled ? kBlue : SK_ColorWHITE);
    s.setAntiAlias(true);
    s.setStyle(SkPaint::kStroke_Style);
    s.setColor(kBlue);
    s.setStrokeWidth(1);
    if (round) {
        c->drawCircle(p, size / 2, f);
        c->drawCircle(p, size / 2, s);
    } else {
        SkRect r = SkRect::MakeXYWH(p.x() - size / 2, p.y() - size / 2, size, size);
        c->drawRect(r, f);
        c->drawRect(r, s);
    }
}

// Splits cubic (p0, c1, c2, p3) at t into two cubics.
void splitCubic(SkPoint p0, SkPoint c1, SkPoint c2, SkPoint p3, float t, SkPoint out[7])
{
    auto lerp = [t](SkPoint a, SkPoint b) { return a + (b - a) * t; };
    SkPoint a = lerp(p0, c1), b = lerp(c1, c2), c = lerp(c2, p3);
    SkPoint d = lerp(a, b), e = lerp(b, c);
    SkPoint f = lerp(d, e);
    out[0] = p0;
    out[1] = a;
    out[2] = d;
    out[3] = f;
    out[4] = e;
    out[5] = c;
    out[6] = p3;
}

} // namespace

// ================================================================ SelectTool

int SelectTool::handleAt(QPointF screen, bool* rotate) const
{
    *rotate = false;
    SkPoint q[4];
    if (!ed.selectionQuad(q))
        return HNone;
    const auto& sel = ed.doc().selection();
    const bool isLine = sel.size() == 1 && ed.doc().at(sel[0]).type() == NodeType::Line;
    if (isLine) {
        if (dist(screen, qp(q[0])) < 7)
            return HL;
        if (dist(screen, qp(q[1])) < 7)
            return HR;
        return HNone;
    }
    static const int corners[4] = {HTL, HTR, HBR, HBL};
    for (int i = 0; i < 4; ++i)
        if (dist(screen, qp(q[i])) < 7)
            return corners[i];
    static const int edges[4] = {HT, HR, HB, HL};
    SkRect aabb;
    aabb.setBounds(q, 4);
    const bool small = aabb.width() < 16 || aabb.height() < 16;
    for (int i = 0; i < 4; ++i) {
        float t;
        if (!small && distToSegment(screen, qp(q[i]), qp(q[(i + 1) % 4]), &t) < 4 && t > 0.05f && t < 0.95f)
            return edges[i];
    }
    // Rotation zone just outside corners.
    SkPath quad;
    quad.addPoly(q, 4, true);
    if (!quad.contains(float(screen.x()), float(screen.y())))
        for (int i = 0; i < 4; ++i)
            if (dist(screen, qp(q[i])) < 20) {
                *rotate = true;
                return corners[i];
            }
    return HNone;
}

void SelectTool::updateHoverCursor(const InputEvent& e)
{
    bool rotate = false;
    int h = handleAt(e.screen, &rotate);
    if (h != HNone) {
        if (rotate) {
            m_cursor = Qt::CrossCursor;
        } else {
            static const Qt::CursorShape shapes[] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor,
                Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor};
            m_cursor = shapes[h];
        }
        ed.setHovered(kNoNode);
        return;
    }
    m_cursor = Qt::ArrowCursor;
    NodeId label = ed.frameLabelAt(e.screen);
    ed.setHovered(label ? label : ed.selectableAt(e.page, e.mods & (Qt::ControlModifier | Qt::MetaModifier)));
}

void SelectTool::press(const InputEvent& e)
{
    if (e.button != Qt::LeftButton)
        return;
    m_start = e;
    m_duplicated = false;
    m_mode = Mode::None;
    const auto sel = doc().selection();

    bool rotate = false;
    int h = sel.empty() ? HNone : handleAt(e.screen, &rotate);
    if (h != HNone) {
        m_handle = h;
        m_nodes = ed.topLevelSelection();
        std::erase_if(m_nodes, [&](NodeId id) { return doc().isInsideInstance(id) || doc().at(id).p.locked; });
        if (m_nodes.empty())
            return;
        m_startWorld.clear();
        m_startSize.clear();
        for (NodeId id : m_nodes) {
            m_startWorld.push_back(doc().worldTransform(id));
            m_startSize.push_back({doc().at(id).p.width, doc().at(id).p.height});
        }
        m_startBounds = ed.selectionBounds();
        doc().begin(rotate ? QObject::tr("Rotate") : QObject::tr("Resize"));
        m_mode = rotate ? Mode::Rotate : Mode::Resize;
        return;
    }

    const bool deep = e.mods & (Qt::ControlModifier | Qt::MetaModifier);
    NodeId hit = ed.frameLabelAt(e.screen);
    if (!hit)
        hit = ed.selectableAt(e.page, deep);
    m_pressHit = hit;
    m_pressWasSelected = hit && std::find(sel.begin(), sel.end(), hit) != sel.end();
    if (hit) {
        if (e.mods & Qt::ShiftModifier) {
            auto next = sel;
            if (m_pressWasSelected)
                std::erase(next, hit);
            else
                next.push_back(hit);
            ed.select(next);
        } else if (!m_pressWasSelected) {
            ed.select({hit});
        }
        m_mode = Mode::Pending;
    } else {
        m_marqueeBase = (e.mods & Qt::ShiftModifier) ? sel : std::vector<NodeId>{};
        if (!(e.mods & Qt::ShiftModifier))
            ed.select({});
        m_mode = Mode::Pending;
    }
}

void SelectTool::beginMove(const InputEvent& e)
{
    m_nodes = ed.topLevelSelection();
    std::erase_if(m_nodes, [&](NodeId id) { return doc().isInsideInstance(id) || doc().at(id).p.locked; });
    if (m_nodes.empty()) {
        m_mode = Mode::None;
        return;
    }
    doc().begin(QObject::tr("Move"));
    if (e.mods & Qt::AltModifier) {
        // Alt-drag moves a copy.
        std::vector<NodeId> copies;
        for (NodeId id : m_nodes) {
            NodeId parent = doc().at(id).parent;
            copies.push_back(doc().duplicate(id, parent, doc().indexOf(id) + 1));
        }
        m_nodes = copies;
        ed.select(copies);
        m_duplicated = true;
    }
    m_startWorld.clear();
    for (NodeId id : m_nodes)
        m_startWorld.push_back(doc().worldTransform(id));
    m_startBounds = SkRect::MakeEmpty();
    for (NodeId id : m_nodes)
        m_startBounds.join(doc().worldBounds(id));
    m_mode = Mode::Move;
}

void SelectTool::doMove(const InputEvent& e)
{
    SkVector delta = e.page - m_start.page;
    if (e.mods & Qt::ShiftModifier) {
        if (std::fabs(delta.x()) > std::fabs(delta.y()))
            delta.fY = 0;
        else
            delta.fX = 0;
    }
    ed.guides.clear();

    NodeId parent = doc().at(m_nodes[0]).parent;

    // Snap edges and centers to siblings and the parent frame.
    if (!(e.mods & (Qt::ControlModifier | Qt::MetaModifier))) {
        SkRect moved = m_startBounds.makeOffset(delta.x(), delta.y());
        std::vector<SkRect> targets;
        for (NodeId s : doc().at(parent).children) {
            if (std::find(m_nodes.begin(), m_nodes.end(), s) != m_nodes.end() || !doc().at(s).p.visible)
                continue;
            SkRect b = doc().worldBounds(s);
            if (doc().at(parent).type() == NodeType::Page) {
                // Only nearby top-level layers matter.
                SkRect area = moved.makeOutset(float(800 / ed.zoom()), float(800 / ed.zoom()));
                if (!SkRect::Intersects(area, b))
                    continue;
            }
            targets.push_back(b);
        }
        if (doc().at(parent).type() != NodeType::Page)
            targets.push_back(doc().worldBounds(parent));
        const float thr = float(5 / ed.zoom());
        float bestX = thr + 1, bestY = thr + 1, snapX = 0, snapY = 0;
        const float mx[3] = {moved.left(), moved.centerX(), moved.right()};
        const float my[3] = {moved.top(), moved.centerY(), moved.bottom()};
        for (const SkRect& t : targets) {
            const float tx[3] = {t.left(), t.centerX(), t.right()};
            const float ty[3] = {t.top(), t.centerY(), t.bottom()};
            for (float a : mx)
                for (float b : tx)
                    if (std::fabs(b - a) < std::fabs(bestX)) {
                        bestX = b - a;
                        snapX = b;
                    }
            for (float a : my)
                for (float b : ty)
                    if (std::fabs(b - a) < std::fabs(bestY)) {
                        bestY = b - a;
                        snapY = b;
                    }
        }
        if (std::fabs(bestX) <= thr)
            delta.fX += bestX;
        if (std::fabs(bestY) <= thr)
            delta.fY += bestY;
        moved = m_startBounds.makeOffset(delta.x(), delta.y());
        for (const SkRect& t : targets) {
            if (std::fabs(bestX) <= thr) {
                const float tx[3] = {t.left(), t.centerX(), t.right()};
                for (float b : tx)
                    if (std::fabs(b - snapX) < 0.01f)
                        ed.guides.push_back({true, snapX, std::min(t.top(), moved.top()), std::max(t.bottom(), moved.bottom())});
            }
            if (std::fabs(bestY) <= thr) {
                const float ty[3] = {t.top(), t.centerY(), t.bottom()};
                for (float b : ty)
                    if (std::fabs(b - snapY) < 0.01f)
                        ed.guides.push_back({false, snapY, std::min(t.left(), moved.left()), std::max(t.right(), moved.right())});
            }
        }
    }
    delta.fX = ed.snapValue(m_startBounds.left() + delta.x()) - m_startBounds.left();
    delta.fY = ed.snapValue(m_startBounds.top() + delta.y()) - m_startBounds.top();

    // Drop target: container under the cursor.
    NodeId target = ed.containerAt(e.page, m_nodes);
    for (NodeId id : m_nodes)
        if (target == id || doc().isAncestor(id, target))
            target = parent;
    const bool targetAL = doc().at(target).p.layout.mode != LayoutMode::None;
    ed.dropTarget = target;

    if (targetAL) {
        // Insertion index along the layout axis.
        const bool horiz = doc().at(target).p.layout.mode == LayoutMode::Horizontal;
        std::vector<NodeId> sibs;
        for (NodeId s : doc().at(target).children)
            if (std::find(m_nodes.begin(), m_nodes.end(), s) == m_nodes.end() && !doc().at(s).p.layoutChild.absolute)
                sibs.push_back(s);
        size_t index = 0;
        for (NodeId s : sibs) {
            SkRect b = doc().worldBounds(s);
            if ((horiz ? e.page.x() : e.page.y()) > (horiz ? b.centerX() : b.centerY()))
                ++index;
        }
        for (NodeId id : m_nodes) {
            const bool same = doc().at(id).parent == target;
            int to = -1;
            if (index < sibs.size()) {
                to = doc().indexOf(sibs[index]);
                if (same && doc().indexOf(id) < to)
                    --to;
            } else if (same && doc().indexOf(id) == int(doc().at(target).children.size()) - 1) {
                continue;
            }
            if (!same)
                ed.reparentKeepWorld(id, target, to);
            else if (to != doc().indexOf(id))
                doc().move(id, target, to);
        }
    } else if (target != parent) {
        for (NodeId id : m_nodes)
            ed.reparentKeepWorld(id, target, -1);
    }
    if (!targetAL) {
        for (size_t i = 0; i < m_nodes.size(); ++i) {
            SkMatrix w = m_startWorld[i];
            w.postTranslate(delta.x(), delta.y());
            doc().setWorldTransform(m_nodes[i], w);
        }
    } else {
        ed.guides.clear();
    }
    doc().updateDerived();
    ed.requestUpdate();
}

void SelectTool::doResize(const InputEvent& e)
{
    const bool shift = e.mods & Qt::ShiftModifier;
    const bool alt = e.mods & Qt::AltModifier;
    const bool left = m_handle == HTL || m_handle == HL || m_handle == HBL;
    const bool right = m_handle == HTR || m_handle == HR || m_handle == HBR;
    const bool top = m_handle == HTL || m_handle == HT || m_handle == HTR;
    const bool bottom = m_handle == HBL || m_handle == HB || m_handle == HBR;

    if (m_nodes.size() == 1) {
        const NodeId id = m_nodes[0];
        const SkMatrix& W0 = m_startWorld[0];
        const float w0 = m_startSize[0].width(), h0 = m_startSize[0].height();
        const NodeType type = doc().at(id).type();
        SkPoint page = e.page;
        if (W0.rectStaysRect())
            page = {ed.snapValue(page.x()), ed.snapValue(page.y())};

        if (type == NodeType::Line) {
            // Move one endpoint; the other stays put.
            SkPoint a = W0.mapXY(0, 0), b = W0.mapXY(w0, 0);
            SkPoint fixed = m_handle == HL ? b : a;
            SkPoint moving = shift ? snap45(fixed, page) : page;
            SkPoint from = m_handle == HL ? moving : fixed, to = m_handle == HL ? fixed : moving;
            SkVector d = to - from;
            SkMatrix w = SkMatrix::Translate(from.x(), from.y());
            w.preRotate(std::atan2(d.y(), d.x()) * 180 / kPi);
            doc().setWorldTransform(id, w);
            doc().resize(id, std::max(d.length(), 0.01f), 0);
            doc().updateDerived();
            ed.requestUpdate();
            return;
        }

        SkMatrix inv;
        if (!W0.invert(&inv))
            return;
        SkPoint lp = inv.mapPoint(page);
        float l = 0, t = 0, r = w0, b = h0;
        if (left) l = lp.x();
        if (right) r = lp.x();
        if (top) t = lp.y();
        if (bottom) b = lp.y();
        if (alt) {
            if (left) r = w0 - l;
            if (right) l = w0 - r;
            if (top) b = h0 - t;
            if (bottom) t = h0 - b;
        }
        if (shift && w0 > 0 && h0 > 0) {
            // Keep aspect ratio around the opposite side (or the center with Alt).
            const bool hEdge = left || right, vEdge = top || bottom;
            const float sx = std::fabs(r - l) / w0, sy = std::fabs(b - t) / h0;
            const float s = (hEdge && vEdge) ? std::max(sx, sy) : hEdge ? sx : sy;
            const float nw = w0 * s, nh = h0 * s;
            const float dx = r >= l ? 1.f : -1.f, dy = b >= t ? 1.f : -1.f;
            if (!hEdge || alt) {
                l = w0 / 2 - dx * nw / 2;
                r = w0 / 2 + dx * nw / 2;
            } else if (left) {
                l = r - dx * nw;
            } else {
                r = l + dx * nw;
            }
            if (!vEdge || alt) {
                t = h0 / 2 - dy * nh / 2;
                b = h0 / 2 + dy * nh / 2;
            } else if (top) {
                t = b - dy * nh;
            } else {
                b = t + dy * nh;
            }
        }
        SkMatrix D = SkMatrix::Translate(std::min(l, r), std::min(t, b));
        const float nw = std::max(std::fabs(r - l), 0.01f), nh = std::max(std::fabs(b - t), 0.01f);
        if (r < l)
            D.preConcat(SkMatrix::MakeAll(-1, 0, nw, 0, 1, 0, 0, 0, 1));
        if (b < t)
            D.preConcat(SkMatrix::MakeAll(1, 0, 0, 0, -1, nh, 0, 0, 1));
        // Resizing turns hug/auto sizes into fixed ones on the dragged axis.
        const Props& cur = doc().at(id).p;
        if ((left || right) && cur.layoutChild.sizingH == LayoutSizing::Hug)
            doc().edit(id).layoutChild.sizingH = LayoutSizing::Fixed;
        if ((top || bottom) && cur.layoutChild.sizingV == LayoutSizing::Hug)
            doc().edit(id).layoutChild.sizingV = LayoutSizing::Fixed;
        if (type == NodeType::Text) {
            TextAutoResize& ar = doc().edit(id).textStyle.autoResize;
            if (top || bottom)
                ar = TextAutoResize::None;
            else if (ar == TextAutoResize::WidthAndHeight)
                ar = TextAutoResize::Height;
        }
        doc().resize(id, nw, nh);
        doc().setWorldTransform(id, SkMatrix::Concat(W0, D));
        doc().updateDerived();
        ed.requestUpdate();
        return;
    }

    // Multiple nodes: scale their arrangement within the selection bounds.
    SkRect sb = m_startBounds;
    float l = sb.left(), t = sb.top(), r = sb.right(), b = sb.bottom();
    SkPoint p = {ed.snapValue(e.page.x()), ed.snapValue(e.page.y())};
    if (left) l = p.x();
    if (right) r = p.x();
    if (top) t = p.y();
    if (bottom) b = p.y();
    if (alt) {
        if (left) r = sb.centerX() * 2 - l;
        if (right) l = sb.centerX() * 2 - r;
        if (top) b = sb.centerY() * 2 - t;
        if (bottom) t = sb.centerY() * 2 - b;
    }
    float sx = sb.width() > 0 ? (r - l) / sb.width() : 1, sy = sb.height() > 0 ? (b - t) / sb.height() : 1;
    if (shift) {
        float s = std::max(std::fabs(sx), std::fabs(sy));
        sx = sx < 0 ? -s : s;
        sy = sy < 0 ? -s : s;
        if (!(left || right))
            sx = s;
        if (!(top || bottom))
            sy = s;
    }
    sx = std::max(std::fabs(sx), 0.001f);
    sy = std::max(std::fabs(sy), 0.001f);
    const float ox = left ? sb.right() : sb.left(), oy = top ? sb.bottom() : sb.top();
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const NodeId id = m_nodes[i];
        SkRect b0 = m_startWorld[i].mapRect(SkRect::MakeWH(m_startSize[i].width(), m_startSize[i].height()));
        float nx = ox + (b0.left() - ox) * sx;
        float ny = oy + (b0.top() - oy) * sy;
        if (left)
            nx = ox - (ox - b0.right()) * sx - b0.width() * sx;
        if (top)
            ny = oy - (oy - b0.bottom()) * sy - b0.height() * sy;
        doc().resize(id, std::max(m_startSize[i].width() * sx, 0.01f), std::max(m_startSize[i].height() * sy, 0.01f));
        SkMatrix w = m_startWorld[i];
        SkRect cur = w.mapRect(SkRect::MakeWH(doc().at(id).p.width, doc().at(id).p.height));
        w.postTranslate(nx - cur.left(), ny - cur.top());
        doc().setWorldTransform(id, w);
    }
    doc().updateDerived();
    ed.requestUpdate();
}

void SelectTool::doRotate(const InputEvent& e)
{
    SkPoint c = m_startBounds.center();
    float a0 = std::atan2(m_start.page.y() - c.y(), m_start.page.x() - c.x());
    float a1 = std::atan2(e.page.y() - c.y(), e.page.x() - c.x());
    float deg = (a1 - a0) * 180 / kPi;
    if (e.mods & Qt::ShiftModifier) {
        // Snap the resulting absolute angle of the first node to 15 degrees.
        const SkMatrix& w = m_startWorld[0];
        float base = std::atan2(w.getSkewY(), w.getScaleX()) * 180 / kPi;
        deg = std::round((base + deg) / 15) * 15 - base;
    }
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        SkMatrix w = m_startWorld[i];
        w.postRotate(deg, c.x(), c.y());
        doc().setWorldTransform(m_nodes[i], w);
    }
    doc().updateDerived();
    ed.requestUpdate();
}

void SelectTool::doMarquee(const InputEvent& e)
{
    SkRect r;
    r.set(m_start.page, e.page);
    ed.marquee = r;
    // Marquee selects within the top-level frame it started in, otherwise top-level layers.
    NodeId scope = ed.doc().currentPage();
    NodeId start = ed.containerAt(m_start.page, {});
    if (start != scope) {
        NodeId top = start;
        while (doc().at(doc().at(top).parent).type() != NodeType::Page)
            top = doc().at(top).parent;
        if (ed.isTopLevelContainer(top))
            scope = top;
    }
    std::vector<NodeId> ids = m_marqueeBase;
    for (NodeId c : doc().at(scope).children) {
        const Node& n = doc().at(c);
        if (!n.p.visible || n.p.locked)
            continue;
        SkRect b = doc().worldBounds(c);
        const bool hit = SkRect::Intersects(r, b);
        if (hit && std::find(ids.begin(), ids.end(), c) == ids.end())
            ids.push_back(c);
    }
    ed.select(ids);
    ed.requestUpdate();
}

void SelectTool::move(const InputEvent& e)
{
    if (!(e.buttons & Qt::LeftButton)) {
        updateHoverCursor(e);
        return;
    }
    switch (m_mode) {
    case Mode::Pending:
        if (dist(e.screen, m_start.screen) > 3) {
            if (m_pressHit) {
                beginMove(e);
                if (m_mode == Mode::Move)
                    doMove(e);
            } else {
                m_mode = Mode::Marquee;
                doMarquee(e);
            }
        }
        break;
    case Mode::Move: doMove(e); break;
    case Mode::Resize: doResize(e); break;
    case Mode::Rotate: doRotate(e); break;
    case Mode::Marquee: doMarquee(e); break;
    case Mode::None: break;
    }
}

void SelectTool::release(const InputEvent& e)
{
    if (e.button != Qt::LeftButton)
        return;
    switch (m_mode) {
    case Mode::Pending:
        // Clicking an item of a multi-selection narrows to it.
        if (m_pressHit && m_pressWasSelected && !(e.mods & Qt::ShiftModifier) && doc().selection().size() > 1)
            ed.select({m_pressHit});
        break;
    case Mode::Move:
    case Mode::Resize:
    case Mode::Rotate:
        doc().commit();
        break;
    case Mode::Marquee:
        break;
    case Mode::None: break;
    }
    m_mode = Mode::None;
    ed.guides.clear();
    ed.marquee.setEmpty();
    ed.dropTarget = kNoNode;
    ed.requestUpdate();
}

void SelectTool::deactivate()
{
    if (m_mode == Mode::Move || m_mode == Mode::Resize || m_mode == Mode::Rotate)
        doc().commit();
    m_mode = Mode::None;
    ed.guides.clear();
    ed.marquee.setEmpty();
    ed.dropTarget = kNoNode;
    ed.setHovered(kNoNode);
}

void SelectTool::doubleClick(const InputEvent& e)
{
    NodeId deepest = ed.hitDeepest(e.page);
    if (!deepest)
        return;
    const Node& d = doc().at(deepest);
    if (d.type() == NodeType::Text) {
        ed.select({deepest});
        ed.enterTextEdit(deepest, e.page, false);
        return;
    }
    const auto sel = doc().selection();
    if (sel.size() == 1 && sel[0] == deepest && !doc().isInsideInstance(deepest)) {
        switch (d.type()) {
        case NodeType::Vector:
            ed.enterVectorEdit(deepest);
            return;
        case NodeType::Rectangle:
        case NodeType::Ellipse:
        case NodeType::Polygon:
        case NodeType::Star:
        case NodeType::Line:
            ed.flatten();
            if (doc().selection().size() == 1)
                ed.enterVectorEdit(doc().selection()[0]);
            return;
        default: break;
        }
    }
    // Drill one level into the selected container.
    for (NodeId c = deepest; c; c = doc().at(c).parent) {
        NodeId parent = doc().at(c).parent;
        if (std::find(sel.begin(), sel.end(), parent) != sel.end()) {
            ed.select({c});
            return;
        }
        if (doc().at(parent).type() == NodeType::Page)
            break;
    }
    ed.select({ed.selectableAt(e.page, false) ? ed.selectableAt(e.page, false) : deepest});
}

// ================================================================ HandTool

void HandTool::press(const InputEvent& e)
{
    m_drag = true;
    m_last = e.screen;
}

void HandTool::move(const InputEvent& e)
{
    if (!m_drag || !(e.buttons & (Qt::LeftButton | Qt::MiddleButton)))
        return;
    ed.panBy(e.screen - m_last);
    m_last = e.screen;
}

// ================================================================ ShapeTool

void ShapeTool::press(const InputEvent& e)
{
    if (e.button != Qt::LeftButton)
        return;
    m_origin = {ed.snapValue(e.page.x()), ed.snapValue(e.page.y())};
    NodeId parent = ed.parentForNewNode(m_origin);
    Props p = ed.defaultProps(m_type);
    if (m_arrow) {
        p.stroke.endCap = StrokeCap::LineArrow;
        p.name = QObject::tr("Arrow");
    }
    doc().begin(QObject::tr("Create %1").arg(p.name));
    m_node = doc().create(p, parent);
    doc().setWorldTransform(m_node, SkMatrix::Translate(m_origin.x(), m_origin.y()));
    doc().resize(m_node, 1, m_type == NodeType::Line ? 0 : 1);
    ed.select({m_node});
    m_dragging = true;
}

void ShapeTool::update(const InputEvent& e)
{
    SkPoint cur = {ed.snapValue(e.page.x()), ed.snapValue(e.page.y())};
    if (m_type == NodeType::Line) {
        if (e.mods & Qt::ShiftModifier)
            cur = snap45(m_origin, cur);
        SkVector d = cur - m_origin;
        SkMatrix w = SkMatrix::Translate(m_origin.x(), m_origin.y());
        w.preRotate(std::atan2(d.y(), d.x()) * 180 / kPi);
        doc().setWorldTransform(m_node, w);
        doc().resize(m_node, std::max(d.length(), 0.01f), 0);
    } else {
        float dx = cur.x() - m_origin.x(), dy = cur.y() - m_origin.y();
        if (e.mods & Qt::ShiftModifier) {
            float s = std::max(std::fabs(dx), std::fabs(dy));
            dx = dx < 0 ? -s : s;
            dy = dy < 0 ? -s : s;
        }
        SkRect r;
        if (e.mods & Qt::AltModifier)
            r = SkRect::MakeLTRB(m_origin.x() - std::fabs(dx), m_origin.y() - std::fabs(dy), m_origin.x() + std::fabs(dx),
                m_origin.y() + std::fabs(dy));
        else
            r.set(m_origin, m_origin + SkVector{dx, dy});
        doc().resize(m_node, std::max(r.width(), 0.01f), std::max(r.height(), 0.01f));
        doc().setWorldTransform(m_node, SkMatrix::Translate(r.left(), r.top()));
    }
    doc().updateDerived();
    ed.requestUpdate();
}

void ShapeTool::move(const InputEvent& e)
{
    if (m_dragging && (e.buttons & Qt::LeftButton))
        update(e);
}

void ShapeTool::release(const InputEvent& e)
{
    if (!m_dragging)
        return;
    m_dragging = false;
    if (dist(e.screen, ed.toScreen(m_origin)) < 3) {
        // A click creates a default-sized shape.
        if (m_type == NodeType::Line)
            doc().resize(m_node, 100, 0);
        else
            doc().resize(m_node, 100, 100);
    } else {
        update(e);
    }
    doc().commit();
    ed.setTool(QStringLiteral("select"));
}

// ================================================================ TextTool

void TextTool::press(const InputEvent& e)
{
    if (e.button != Qt::LeftButton)
        return;
    // Clicking an existing text layer edits it.
    NodeId hit = ed.hitDeepest(e.page);
    if (hit && doc().at(hit).type() == NodeType::Text) {
        ed.select({hit});
        ed.enterTextEdit(hit, e.page, false);
        return;
    }
    m_dragging = true;
    m_origin = m_current = {ed.snapValue(e.page.x()), ed.snapValue(e.page.y())};
}

void TextTool::move(const InputEvent& e)
{
    if (!m_dragging)
        return;
    m_current = {ed.snapValue(e.page.x()), ed.snapValue(e.page.y())};
    ed.requestUpdate();
}

void TextTool::release(const InputEvent&)
{
    if (!m_dragging)
        return;
    m_dragging = false;
    SkRect r;
    r.set(m_origin, m_current);
    Props p = ed.defaultProps(NodeType::Text);
    const bool box = r.width() * ed.zoom() > 4 && r.height() * ed.zoom() > 4;
    if (box) {
        p.textStyle.autoResize = TextAutoResize::Height;
        p.layoutChild.sizingH = LayoutSizing::Fixed;
    }
    NodeId parent = ed.parentForNewNode(m_origin);
    doc().begin(QObject::tr("Add text"));
    NodeId id = doc().create(p, parent);
    doc().setMergeKey(QStringLiteral("text:%1").arg(id));
    if (box)
        doc().resize(id, r.width(), r.height());
    // Click places the first baseline roughly at the cursor, like Figma.
    const float lineH = p.textStyle.size * 1.21f;
    doc().setWorldTransform(id, SkMatrix::Translate(r.left(), box ? r.top() : r.top() - lineH / 2));
    ed.select({id});
    doc().commit();
    ed.enterTextEdit(id, {0, 0}, false, true);
}

void TextTool::drawOverlay(SkCanvas* c)
{
    if (!m_dragging)
        return;
    SkRect r;
    r.set(m_origin, m_current);
    SkPaint p;
    p.setStyle(SkPaint::kStroke_Style);
    p.setColor(kBlue);
    p.setAntiAlias(true);
    c->drawRect(ed.view().mapRect(r), p);
}

// ================================================================ TextEditTool

QString TextEditTool::text() const
{
    return ed.doc().isAttached(m_node) ? ed.doc().at(m_node).p.text : QString();
}

int TextEditTool::toDisplay(int i) const
{
    // Paragraph spacing inserts one extra newline per paragraph break in the laid-out text.
    const Props& p = ed.doc().at(m_node).p;
    if (p.textStyle.paragraphSpacing <= 0)
        return i;
    return i + int(p.text.left(i).count(QLatin1Char('\n')));
}

int TextEditTool::fromDisplay(int d) const
{
    const Props& p = ed.doc().at(m_node).p;
    if (p.textStyle.paragraphSpacing <= 0)
        return std::clamp(d, 0, int(p.text.size()));
    int disp = 0;
    for (int i = 0; i < p.text.size(); ++i) {
        if (disp >= d)
            return i;
        disp += p.text[i] == QLatin1Char('\n') ? 2 : 1;
    }
    return int(p.text.size());
}

void TextEditTool::begin(NodeId id, SkPoint pagePos, bool selectAll, bool created)
{
    m_node = id;
    m_created = created;
    const int len = int(text().size());
    if (selectAll) {
        m_anchor = 0;
        m_caret = len;
    } else if (pagePos == SkPoint{0, 0}) {
        m_anchor = m_caret = len;
    } else {
        m_anchor = m_caret = indexAt(pagePos);
    }
    ed.select({id});
    if (!created)
        doc().breakMerge(); // a new text layer's typing merges into its creation step
    ed.requestUpdate();
}

void TextEditTool::activate()
{
    m_dragging = false;
}

void TextEditTool::deactivate()
{
    if (!doc().isAttached(m_node))
        return;
    if (text().trimmed().isEmpty()) {
        doc().begin(QObject::tr("Delete text"));
        if (m_created)
            doc().setMergeKey(QStringLiteral("text:%1").arg(m_node));
        doc().remove(m_node);
        doc().commit();
    } else {
        ed.select({m_node});
    }
    doc().breakMerge();
    m_node = kNoNode;
}

SkMatrix TextEditTool::localToScreen()
{
    auto* para = ed.renderer().paragraph(m_node);
    SkMatrix m = SkMatrix::Concat(ed.view(), doc().worldTransform(m_node));
    m.preTranslate(0, text::verticalOffset(doc().at(m_node).p, para->getHeight()));
    return m;
}

int TextEditTool::indexAt(SkPoint page)
{
    auto* para = ed.renderer().paragraph(m_node);
    SkMatrix inv;
    SkMatrix m = doc().worldTransform(m_node);
    m.preTranslate(0, text::verticalOffset(doc().at(m_node).p, para->getHeight()));
    if (!m.invert(&inv))
        return 0;
    SkPoint lp = inv.mapPoint(page);
    auto pos = para->getGlyphPositionAtCoordinate(lp.x(), lp.y());
    return fromDisplay(int(pos.position));
}

SkRect TextEditTool::caretRect(int index)
{
    using namespace skia::textlayout;
    const Props& p = doc().at(m_node).p;
    auto* para = ed.renderer().paragraph(m_node);
    const QString t = text();
    const float size = p.textStyle.size;
    const float lineH = p.textStyle.lineHeight > 0
        ? (p.textStyle.lineHeightPercent ? p.textStyle.lineHeight / 100 * size : p.textStyle.lineHeight)
        : size * 1.21f;
    auto alignX = [&](float lineWidth) {
        switch (p.textStyle.alignH) {
        case TextAlignH::Center: return (p.width - lineWidth) / 2;
        case TextAlignH::Right: return p.width - lineWidth;
        default: return 0.f;
        }
    };
    if (t.isEmpty())
        return SkRect::MakeXYWH(alignX(0), 0, 1, lineH);
    if (index > 0 && t[index - 1] != QLatin1Char('\n')) {
        auto boxes = para->getRectsForRange(toDisplay(index - 1), toDisplay(index), RectHeightStyle::kMax, RectWidthStyle::kTight);
        if (!boxes.empty()) {
            SkRect b = boxes.back().rect;
            return SkRect::MakeXYWH(b.right(), b.top(), 1, b.height());
        }
    }
    if (index < t.size() && t[index] != QLatin1Char('\n')) {
        auto boxes = para->getRectsForRange(toDisplay(index), toDisplay(index + 1), RectHeightStyle::kMax, RectWidthStyle::kTight);
        if (!boxes.empty()) {
            SkRect b = boxes.front().rect;
            return SkRect::MakeXYWH(b.left(), b.top(), 1, b.height());
        }
    }
    // Empty line: find its vertical position from the number of preceding lines.
    std::vector<LineMetrics> lines;
    para->getLineMetrics(lines);
    int lineNo = int(t.left(index).count(QLatin1Char('\n')));
    // Soft wraps before this point add lines too; use the paragraph's line containing the previous char.
    if (index > 0) {
        auto boxes = para->getRectsForRange(toDisplay(index - 1), toDisplay(index), RectHeightStyle::kMax, RectWidthStyle::kTight);
        if (!boxes.empty()) {
            SkRect b = boxes.back().rect;
            return SkRect::MakeXYWH(alignX(0), b.bottom(), 1, b.height());
        }
    }
    float y = 0;
    for (int i = 0; i < lineNo && i < int(lines.size()); ++i)
        y += float(lines[i].fHeight);
    return SkRect::MakeXYWH(alignX(0), y, 1, lineH);
}

void TextEditTool::setText(const QString& t, int caret)
{
    doc().begin(QObject::tr("Edit text"));
    doc().setMergeKey(QStringLiteral("text:%1").arg(m_node));
    doc().edit(m_node).text = t;
    doc().commit();
    m_caret = m_anchor = std::clamp(caret, 0, int(t.size()));
}

void TextEditTool::insert(const QString& s)
{
    QString t = text();
    int a = std::min(m_anchor, m_caret), b = std::max(m_anchor, m_caret);
    t.replace(a, b - a, s);
    setText(t, a + int(s.size()));
}

void TextEditTool::eraseSelection()
{
    insert(QString());
}

int TextEditTool::lineMove(int caret, int dir)
{
    SkRect r = caretRect(caret);
    auto* para = ed.renderer().paragraph(m_node);
    float y = dir < 0 ? r.top() - 2 : r.bottom() + 2;
    if (y < 0)
        return 0;
    if (y > para->getHeight())
        return int(text().size());
    return fromDisplay(int(para->getGlyphPositionAtCoordinate(r.left(), y).position));
}

static int wordBoundary(const QString& t, int i, int dir)
{
    const int n = int(t.size());
    if (dir < 0) {
        while (i > 0 && t[i - 1].isSpace())
            --i;
        while (i > 0 && !t[i - 1].isSpace())
            --i;
    } else {
        while (i < n && t[i].isSpace())
            ++i;
        while (i < n && !t[i].isSpace())
            ++i;
    }
    return i;
}

bool TextEditTool::key(QKeyEvent* e)
{
    if (!doc().isAttached(m_node))
        return false;
    const bool ctrl = e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const QString t = text();
    const int n = int(t.size());
    auto moveTo = [&](int pos) {
        m_caret = std::clamp(pos, 0, n);
        if (!shift)
            m_anchor = m_caret;
        ed.requestUpdate();
    };
    switch (e->key()) {
    case Qt::Key_Escape:
        ed.setTool(QStringLiteral("select"));
        return true;
    case Qt::Key_Left:
        if (!shift && hasSelection())
            moveTo(std::min(m_anchor, m_caret));
        else
            moveTo(ctrl ? wordBoundary(t, m_caret, -1) : m_caret - 1);
        return true;
    case Qt::Key_Right:
        if (!shift && hasSelection())
            moveTo(std::max(m_anchor, m_caret));
        else
            moveTo(ctrl ? wordBoundary(t, m_caret, 1) : m_caret + 1);
        return true;
    case Qt::Key_Up: moveTo(lineMove(m_caret, -1)); return true;
    case Qt::Key_Down: moveTo(lineMove(m_caret, 1)); return true;
    case Qt::Key_Home: {
        int i = m_caret;
        while (i > 0 && t[i - 1] != QLatin1Char('\n'))
            --i;
        moveTo(ctrl ? 0 : i);
        return true;
    }
    case Qt::Key_End: {
        int i = m_caret;
        while (i < n && t[i] != QLatin1Char('\n'))
            ++i;
        moveTo(ctrl ? n : i);
        return true;
    }
    case Qt::Key_Backspace:
        if (hasSelection())
            eraseSelection();
        else if (m_caret > 0) {
            m_anchor = ctrl ? wordBoundary(t, m_caret, -1) : m_caret - 1;
            eraseSelection();
        }
        return true;
    case Qt::Key_Delete:
        if (hasSelection())
            eraseSelection();
        else if (m_caret < n) {
            m_anchor = ctrl ? wordBoundary(t, m_caret, 1) : m_caret + 1;
            eraseSelection();
        }
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (ctrl) {
            ed.setTool(QStringLiteral("select"));
            return true;
        }
        insert(QStringLiteral("\n"));
        return true;
    default: break;
    }
    if (ctrl) {
        switch (e->key()) {
        case Qt::Key_A:
            m_anchor = 0;
            m_caret = n;
            ed.requestUpdate();
            return true;
        case Qt::Key_C:
        case Qt::Key_X:
            if (hasSelection()) {
                int a = std::min(m_anchor, m_caret), b = std::max(m_anchor, m_caret);
                if (auto* cb = QGuiApplication::clipboard())
                    cb->setText(t.mid(a, b - a));
                if (e->key() == Qt::Key_X)
                    eraseSelection();
            }
            return true;
        case Qt::Key_V:
            if (auto* cb = QGuiApplication::clipboard())
                insert(cb->text());
            return true;
        case Qt::Key_Z:
            ed.undo();
            return true;
        case Qt::Key_B:
            ed.setProp(QStringLiteral("fontWeight"), doc().at(m_node).p.textStyle.weight >= 600 ? 400 : 700);
            return true;
        case Qt::Key_I:
            ed.setProp(QStringLiteral("italic"), !doc().at(m_node).p.textStyle.italic);
            return true;
        case Qt::Key_U:
            ed.setProp(QStringLiteral("decoration"),
                doc().at(m_node).p.textStyle.decoration == TextDecoration::Underline ? QStringLiteral("NONE")
                                                                                     : QStringLiteral("UNDERLINE"));
            return true;
        default: return true; // swallow other shortcuts while typing
        }
    }
    const QString s = e->text();
    if (!s.isEmpty() && s[0].isPrint()) {
        insert(s);
        return true;
    }
    return true;
}

bool TextEditTool::inputMethod(QInputMethodEvent* e)
{
    if (!e->commitString().isEmpty())
        insert(e->commitString());
    return true;
}

void TextEditTool::press(const InputEvent& e)
{
    if (!doc().isAttached(m_node))
        return;
    SkMatrix w = doc().worldTransform(m_node), inv;
    if (!w.invert(&inv))
        return;
    SkPoint lp = inv.mapPoint(e.page);
    const float tol = float(4 / ed.zoom());
    if (!doc().at(m_node).localRect().makeOutset(tol, tol).contains(lp.x(), lp.y())) {
        // Clicking elsewhere ends editing and acts like a normal click.
        ed.setTool(QStringLiteral("select"));
        ed.mousePress(e);
        return;
    }
    m_caret = indexAt(e.page);
    if (!(e.mods & Qt::ShiftModifier))
        m_anchor = m_caret;
    m_dragging = true;
    ed.requestUpdate();
}

void TextEditTool::move(const InputEvent& e)
{
    if (!m_dragging || !(e.buttons & Qt::LeftButton))
        return;
    m_caret = indexAt(e.page);
    ed.requestUpdate();
}

void TextEditTool::release(const InputEvent&)
{
    m_dragging = false;
}

void TextEditTool::doubleClick(const InputEvent& e)
{
    const QString t = text();
    int i = indexAt(e.page);
    m_anchor = wordBoundary(t, std::min(i + 1, int(t.size())), -1);
    m_caret = wordBoundary(t, m_anchor, 1);
    ed.requestUpdate();
}

void TextEditTool::drawOverlay(SkCanvas* c)
{
    if (!doc().isAttached(m_node))
        return;
    SkPaint box;
    box.setStyle(SkPaint::kStroke_Style);
    box.setColor(kBlue);
    box.setAntiAlias(true);
    SkMatrix world = SkMatrix::Concat(ed.view(), doc().worldTransform(m_node));
    c->drawPath(SkPath::Rect(doc().at(m_node).localRect()).makeTransform(world), box);

    SkMatrix m = localToScreen();
    auto* para = ed.renderer().paragraph(m_node);
    if (hasSelection()) {
        int a = std::min(m_anchor, m_caret), b = std::max(m_anchor, m_caret);
        SkPaint sp;
        sp.setColor(SkColorSetARGB(70, 0x0D, 0x99, 0xFF));
        for (auto& tb : para->getRectsForRange(toDisplay(a), toDisplay(b), skia::textlayout::RectHeightStyle::kMax,
                 skia::textlayout::RectWidthStyle::kTight))
            c->drawPath(SkPath::Rect(tb.rect).makeTransform(m), sp);
    }
    SkRect cr = caretRect(m_caret);
    SkPaint cp;
    cp.setColor(kBlue);
    cp.setAntiAlias(true);
    cp.setStrokeWidth(1.5f);
    SkPoint a = m.mapXY(cr.left(), cr.top()), b = m.mapXY(cr.left(), cr.bottom());
    c->drawLine(a, b, cp);
}

// ================================================================ PenTool

void PenTool::activate()
{
    m_node = kNoNode;
    m_contour = {};
    m_hoverValid = false;
}

void PenTool::deactivate()
{
    if (m_node)
        finish(false);
}

SkPoint PenTool::constrain(SkPoint p, const InputEvent& e) const
{
    p = {ed.snapValue(p.x()), ed.snapValue(p.y())};
    if ((e.mods & Qt::ShiftModifier) && !m_contour.pts.empty())
        p = snap45(m_contour.pts.back().p, p);
    return p;
}

void PenTool::writeBack()
{
    const Node& n = doc().at(m_node);
    SkMatrix parentWorld = doc().worldTransform(n.parent), inv;
    if (!parentWorld.invert(&inv))
        return;
    VectorData v;
    v.contours = {m_contour};
    v.transform(inv); // page -> parent space
    SkRect b = v.toPath().computeTightBounds();
    if (b.isEmpty() && v.contours[0].pts.size() == 1)
        b = SkRect::MakeXYWH(v.contours[0].pts[0].p.x(), v.contours[0].pts[0].p.y(), 0, 0);
    v.transform(SkMatrix::Translate(-b.left(), -b.top()));
    Props& p = doc().edit(m_node);
    p.vector = v;
    p.transform = SkMatrix::Translate(b.left(), b.top());
    p.width = std::max(b.width(), 0.01f);
    p.height = std::max(b.height(), 0.01f);
    doc().updateDerived();
    ed.requestUpdate();
}

void PenTool::press(const InputEvent& e)
{
    if (e.button != Qt::LeftButton)
        return;
    SkPoint p = constrain(e.page, e);
    if (!m_node) {
        NodeId parent = ed.parentForNewNode(p);
        doc().begin(QObject::tr("Pen"));
        m_node = doc().create(ed.defaultProps(NodeType::Vector), parent);
        ed.select({m_node});
    }
    if (m_contour.pts.size() >= 2 && dist(ed.toScreen(m_contour.pts.front().p), e.screen) < 8) {
        finish(true);
        return;
    }
    m_contour.pts.push_back({p, p, p, AnchorMode::Corner});
    m_dragging = true;
    writeBack();
}

void PenTool::move(const InputEvent& e)
{
    m_hover = constrain(e.page, e);
    m_hoverValid = true;
    if (m_dragging && (e.buttons & Qt::LeftButton) && !m_contour.pts.empty()) {
        Anchor& a = m_contour.pts.back();
        SkPoint out = {ed.snapValue(e.page.x()), ed.snapValue(e.page.y())};
        if (dist(ed.toScreen(out), ed.toScreen(a.p)) > 2) {
            a.out = out;
            if (e.mods & Qt::AltModifier) {
                a.mode = AnchorMode::Disconnected;
            } else {
                a.in = a.p - (out - a.p);
                a.mode = AnchorMode::Mirrored;
            }
            writeBack();
        }
    }
    ed.requestUpdate();
}

void PenTool::release(const InputEvent&)
{
    m_dragging = false;
}

bool PenTool::key(QKeyEvent* e)
{
    const bool ctrl = e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
    switch (e->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_node)
            finish(false);
        else
            ed.setTool(QStringLiteral("select"));
        return true;
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        if (!m_contour.pts.empty()) {
            m_contour.pts.pop_back();
            if (!m_contour.pts.empty())
                writeBack();
        }
        return true;
    case Qt::Key_Z:
        if (ctrl && !m_contour.pts.empty()) {
            m_contour.pts.pop_back();
            if (!m_contour.pts.empty())
                writeBack();
            return true;
        }
        return false;
    default: return false;
    }
}

void PenTool::finish(bool close)
{
    if (!m_node)
        return;
    if (m_contour.pts.size() < 2) {
        doc().cancel();
    } else {
        m_contour.closed = close;
        writeBack();
        ed.select({m_node});
        doc().commit();
    }
    m_node = kNoNode;
    m_contour = {};
    ed.setTool(QStringLiteral("select"));
}

void PenTool::drawOverlay(SkCanvas* c)
{
    const SkMatrix v = ed.view();
    SkPaint line;
    line.setAntiAlias(true);
    line.setStyle(SkPaint::kStroke_Style);
    line.setColor(kBlue);
    line.setStrokeWidth(1);
    if (!m_contour.pts.empty() && m_hoverValid && !m_dragging) {
        const Anchor& last = m_contour.pts.back();
        SkPathBuilder b;
        b.moveTo(v.mapPoint(last.p));
        if (last.hasOut())
            b.cubicTo(v.mapPoint(last.out), v.mapPoint(m_hover), v.mapPoint(m_hover));
        else
            b.lineTo(v.mapPoint(m_hover));
        c->drawPath(b.detach(), line);
    }
    for (size_t i = 0; i < m_contour.pts.size(); ++i) {
        const Anchor& a = m_contour.pts[i];
        if (i + 1 == m_contour.pts.size()) {
            if (a.hasIn()) {
                c->drawLine(v.mapPoint(a.p), v.mapPoint(a.in), line);
                drawHandle(c, v.mapPoint(a.in), false, true, 5);
            }
            if (a.hasOut()) {
                c->drawLine(v.mapPoint(a.p), v.mapPoint(a.out), line);
                drawHandle(c, v.mapPoint(a.out), false, true, 5);
            }
        }
        drawHandle(c, v.mapPoint(a.p), i + 1 == m_contour.pts.size());
    }
}

// ================================================================ PencilTool

void PencilTool::press(const InputEvent& e)
{
    if (e.button != Qt::LeftButton)
        return;
    m_points = {e.page};
    m_drawing = true;
}

void PencilTool::move(const InputEvent& e)
{
    if (!m_drawing || !(e.buttons & Qt::LeftButton))
        return;
    if (dist(m_points.back(), e.page) * ed.zoom() > 1.5)
        m_points.push_back(e.page);
    ed.requestUpdate();
}

static void rdp(const std::vector<SkPoint>& pts, size_t a, size_t b, float eps, std::vector<bool>& keep)
{
    float maxD = 0;
    size_t idx = a;
    for (size_t i = a + 1; i < b; ++i) {
        float d = distToSegment(qp(pts[i]), qp(pts[a]), qp(pts[b]));
        if (d > maxD) {
            maxD = d;
            idx = i;
        }
    }
    if (maxD > eps) {
        keep[idx] = true;
        rdp(pts, a, idx, eps, keep);
        rdp(pts, idx, b, eps, keep);
    }
}

void PencilTool::release(const InputEvent&)
{
    if (!m_drawing)
        return;
    m_drawing = false;
    if (m_points.size() < 2) {
        m_points.clear();
        return;
    }
    // Simplify, then fit a smooth Catmull-Rom spline as cubic beziers.
    std::vector<bool> keep(m_points.size(), false);
    keep.front() = keep.back() = true;
    rdp(m_points, 0, m_points.size() - 1, float(1.5 / ed.zoom()), keep);
    std::vector<SkPoint> pts;
    for (size_t i = 0; i < m_points.size(); ++i)
        if (keep[i])
            pts.push_back(m_points[i]);
    const bool closed = pts.size() > 3 && dist(ed.toScreen(pts.front()), ed.toScreen(pts.back())) < 8;
    if (closed)
        pts.pop_back();
    Contour c;
    c.closed = closed;
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        SkPoint prev = (i == 0) ? (closed ? pts[n - 1] : pts[0]) : pts[i - 1];
        SkPoint next = (i + 1 == n) ? (closed ? pts[0] : pts[n - 1]) : pts[i + 1];
        SkVector tan = (next - prev) * (1.f / 6.f);
        c.pts.push_back({pts[i], pts[i] - tan, pts[i] + tan, AnchorMode::Asymmetric});
    }
    NodeId parent = ed.parentForNewNode(pts.front());
    SkMatrix pw = doc().worldTransform(parent), inv;
    if (!pw.invert(&inv))
        inv.reset();
    VectorData v;
    v.contours = {c};
    v.transform(inv);
    SkRect b = v.toPath().computeTightBounds();
    v.transform(SkMatrix::Translate(-b.left(), -b.top()));
    Props p = ed.defaultProps(NodeType::Vector);
    p.stroke.weight = 2;
    p.stroke.startCap = p.stroke.endCap = StrokeCap::Round;
    p.stroke.join = StrokeJoin::Round;
    p.vector = v;
    p.transform = SkMatrix::Translate(b.left(), b.top());
    p.width = std::max(b.width(), 0.01f);
    p.height = std::max(b.height(), 0.01f);
    doc().begin(QObject::tr("Pencil"));
    NodeId id = doc().create(p, parent);
    ed.select({id});
    doc().commit();
    m_points.clear();
}

void PencilTool::drawOverlay(SkCanvas* c)
{
    if (m_points.size() < 2)
        return;
    SkPathBuilder b;
    b.moveTo(ed.view().mapPoint(m_points[0]));
    for (size_t i = 1; i < m_points.size(); ++i)
        b.lineTo(ed.view().mapPoint(m_points[i]));
    SkPaint p;
    p.setAntiAlias(true);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(float(2 * ed.zoom()));
    p.setStrokeCap(SkPaint::kRound_Cap);
    p.setStrokeJoin(SkPaint::kRound_Join);
    p.setColor(SK_ColorBLACK);
    c->drawPath(b.detach(), p);
}

// ================================================================ VectorEditTool

void VectorEditTool::begin(NodeId id)
{
    m_node = id;
    m_selected.clear();
    ed.select({id});
    ed.requestUpdate();
}

void VectorEditTool::deactivate()
{
    if (m_dragging) {
        normalize();
        doc().commit();
        m_dragging = false;
    }
    m_node = kNoNode;
}

SkMatrix VectorEditTool::localToScreen() const
{
    return SkMatrix::Concat(ed.view(), ed.doc().worldTransform(m_node));
}

VectorEditTool::Ref VectorEditTool::hit(QPointF screen) const
{
    const VectorData& v = ed.doc().at(m_node).p.vector;
    const SkMatrix m = localToScreen();
    // Handles of selected anchors take priority.
    for (const Ref& s : m_selected) {
        if (s.contour >= int(v.contours.size()) || s.index >= int(v.contours[s.contour].pts.size()))
            continue;
        const Anchor& a = v.contours[s.contour].pts[s.index];
        if (a.hasIn() && dist(qp(m.mapPoint(a.in)), screen) < 6)
            return {s.contour, s.index, 1};
        if (a.hasOut() && dist(qp(m.mapPoint(a.out)), screen) < 6)
            return {s.contour, s.index, 2};
    }
    for (int ci = 0; ci < int(v.contours.size()); ++ci)
        for (int i = 0; i < int(v.contours[ci].pts.size()); ++i)
            if (dist(qp(m.mapPoint(v.contours[ci].pts[i].p)), screen) < 7)
                return {ci, i, 0};
    return {};
}

bool VectorEditTool::hitSegment(SkPoint local, int* contour, int* index, float* tOut) const
{
    const VectorData& v = ed.doc().at(m_node).p.vector;
    const float tol = float(5 / ed.zoom());
    for (int ci = 0; ci < int(v.contours.size()); ++ci) {
        const Contour& c = v.contours[ci];
        const int n = int(c.pts.size());
        const int segs = c.closed ? n : n - 1;
        for (int i = 0; i < segs; ++i) {
            const Anchor& a = c.pts[i];
            const Anchor& b = c.pts[(i + 1) % n];
            // Sample the cubic.
            SkPoint prev = a.p;
            for (int s = 1; s <= 24; ++s) {
                float t = s / 24.f, u = 1 - t;
                SkPoint p = a.p * (u * u * u) + a.out * (3 * u * u * t) + b.in * (3 * u * t * t) + b.p * (t * t * t);
                float segT;
                if (distToSegment(qp(local), qp(prev), qp(p), &segT) < tol) {
                    *contour = ci;
                    *index = i;
                    *tOut = (s - 1 + segT) / 24.f;
                    return true;
                }
                prev = p;
            }
        }
    }
    return false;
}

void VectorEditTool::normalize()
{
    Props& p = doc().edit(m_node);
    SkRect b = p.vector.toPath().computeTightBounds();
    if (b.left() == 0 && b.top() == 0 && b.width() == p.width && b.height() == p.height)
        return;
    p.vector.transform(SkMatrix::Translate(-b.left(), -b.top()));
    p.transform.preTranslate(b.left(), b.top());
    p.width = std::max(b.width(), 0.01f);
    p.height = std::max(b.height(), 0.01f);
}

void VectorEditTool::press(const InputEvent& e)
{
    if (!doc().isAttached(m_node) || e.button != Qt::LeftButton)
        return;
    SkMatrix inv;
    if (!doc().worldTransform(m_node).invert(&inv))
        return;
    const SkPoint local = inv.mapPoint(e.page);
    Ref r = hit(e.screen);
    if (r.valid()) {
        if (r.part == 0) {
            Ref anchor{r.contour, r.index, 0};
            auto it = std::find(m_selected.begin(), m_selected.end(), anchor);
            if (e.mods & Qt::ShiftModifier) {
                if (it != m_selected.end())
                    m_selected.erase(it);
                else
                    m_selected.push_back(anchor);
            } else if (it == m_selected.end()) {
                m_selected = {anchor};
            }
        }
        m_drag = r;
        m_dragStart = local;
        m_startData = doc().at(m_node).p.vector;
        doc().begin(QObject::tr("Edit vector"));
        m_dragging = true;
        ed.requestUpdate();
        return;
    }
    int ci, idx;
    float t;
    if (hitSegment(local, &ci, &idx, &t)) {
        // Insert an anchor on the segment.
        doc().begin(QObject::tr("Add point"));
        VectorData v = doc().at(m_node).p.vector;
        Contour& c = v.contours[ci];
        const int n = int(c.pts.size());
        Anchor& a = c.pts[idx];
        Anchor& b = c.pts[(idx + 1) % n];
        SkPoint s[7];
        splitCubic(a.p, a.out, b.in, b.p, t, s);
        const bool straight = !a.hasOut() && !b.hasIn();
        Anchor mid{s[3], straight ? s[3] : s[2], straight ? s[3] : s[4], straight ? AnchorMode::Corner : AnchorMode::Asymmetric};
        if (!straight) {
            a.out = s[1];
            b.in = s[5];
        }
        c.pts.insert(c.pts.begin() + idx + 1, mid);
        doc().edit(m_node).vector = v;
        doc().commit();
        m_selected = {{ci, idx + 1, 0}};
        ed.requestUpdate();
        return;
    }
    const float tol = float(8 / ed.zoom());
    if (!doc().at(m_node).localRect().makeOutset(tol, tol).contains(local.x(), local.y())) {
        ed.setTool(QStringLiteral("select"));
        ed.mousePress(e);
        return;
    }
    m_selected.clear();
    ed.requestUpdate();
}

void VectorEditTool::move(const InputEvent& e)
{
    if (!m_dragging || !(e.buttons & Qt::LeftButton))
        return;
    SkMatrix inv;
    if (!doc().worldTransform(m_node).invert(&inv))
        return;
    SkPoint local = inv.mapPoint({ed.snapValue(e.page.x()), ed.snapValue(e.page.y())});
    SkVector d = local - m_dragStart;
    VectorData v = m_startData;
    if (m_drag.part == 0) {
        for (const Ref& s : m_selected) {
            if (s.contour >= int(v.contours.size()) || s.index >= int(v.contours[s.contour].pts.size()))
                continue;
            Anchor& a = v.contours[s.contour].pts[s.index];
            a.p += d;
            a.in += d;
            a.out += d;
        }
    } else {
        Anchor& a = v.contours[m_drag.contour].pts[m_drag.index];
        SkPoint& h = m_drag.part == 1 ? a.in : a.out;
        SkPoint& o = m_drag.part == 1 ? a.out : a.in;
        h += d;
        if (e.mods & Qt::AltModifier)
            a.mode = AnchorMode::Disconnected;
        if (a.mode == AnchorMode::Mirrored) {
            o = a.p - (h - a.p);
        } else if (a.mode == AnchorMode::Asymmetric && o != a.p) {
            SkVector dir = a.p - h;
            if (dir.normalize())
                o = a.p + dir * SkPoint::Distance(o, a.p);
        }
    }
    doc().edit(m_node).vector = v;
    doc().updateDerived();
    ed.requestUpdate();
}

void VectorEditTool::release(const InputEvent&)
{
    if (!m_dragging)
        return;
    m_dragging = false;
    normalize();
    doc().commit();
    ed.requestUpdate();
}

void VectorEditTool::doubleClick(const InputEvent& e)
{
    Ref r = hit(e.screen);
    if (!r.valid() || r.part != 0) {
        if (!r.valid())
            ed.setTool(QStringLiteral("select"));
        return;
    }
    // Toggle corner <-> smooth.
    doc().begin(QObject::tr("Toggle point"));
    VectorData v = doc().at(m_node).p.vector;
    Contour& c = v.contours[r.contour];
    const int n = int(c.pts.size());
    Anchor& a = c.pts[r.index];
    if (a.hasIn() || a.hasOut()) {
        a.in = a.out = a.p;
        a.mode = AnchorMode::Corner;
    } else {
        SkPoint prev = (r.index > 0 || c.closed) ? c.pts[(r.index - 1 + n) % n].p : a.p;
        SkPoint next = (r.index + 1 < n || c.closed) ? c.pts[(r.index + 1) % n].p : a.p;
        SkVector tan = (next - prev) * 0.25f;
        a.in = a.p - tan;
        a.out = a.p + tan;
        a.mode = AnchorMode::Mirrored;
    }
    doc().edit(m_node).vector = v;
    normalize();
    doc().commit();
    ed.requestUpdate();
}

bool VectorEditTool::key(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        ed.setTool(QStringLiteral("select"));
        return true;
    case Qt::Key_Delete:
    case Qt::Key_Backspace: {
        if (m_selected.empty())
            return true;
        doc().begin(QObject::tr("Delete points"));
        VectorData v = doc().at(m_node).p.vector;
        auto sel = m_selected;
        std::sort(sel.begin(), sel.end(), [](const Ref& a, const Ref& b) {
            return a.contour != b.contour ? a.contour > b.contour : a.index > b.index;
        });
        for (const Ref& s : sel)
            if (s.contour < int(v.contours.size()) && s.index < int(v.contours[s.contour].pts.size()))
                v.contours[s.contour].pts.erase(v.contours[s.contour].pts.begin() + s.index);
        std::erase_if(v.contours, [](const Contour& c) { return c.pts.size() < 2; });
        m_selected.clear();
        if (v.contours.empty()) {
            doc().remove(m_node);
            doc().commit();
            ed.setTool(QStringLiteral("select"));
            return true;
        }
        doc().edit(m_node).vector = v;
        normalize();
        doc().commit();
        ed.requestUpdate();
        return true;
    }
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        if (m_selected.empty())
            return true;
        const float step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        SkVector d = {e->key() == Qt::Key_Left ? -step : e->key() == Qt::Key_Right ? step : 0,
            e->key() == Qt::Key_Up ? -step : e->key() == Qt::Key_Down ? step : 0};
        doc().begin(QObject::tr("Nudge points"));
        doc().setMergeKey(QStringLiteral("nudge-points"));
        VectorData v = doc().at(m_node).p.vector;
        for (const Ref& s : m_selected) {
            Anchor& a = v.contours[s.contour].pts[s.index];
            a.p += d;
            a.in += d;
            a.out += d;
        }
        doc().edit(m_node).vector = v;
        normalize();
        doc().commit();
        return true;
    }
    default: return false;
    }
}

void VectorEditTool::drawOverlay(SkCanvas* c)
{
    if (!doc().isAttached(m_node))
        return;
    const SkMatrix m = localToScreen();
    const VectorData& v = doc().at(m_node).p.vector;
    SkPaint line;
    line.setAntiAlias(true);
    line.setStyle(SkPaint::kStroke_Style);
    line.setColor(kBlue);
    line.setStrokeWidth(1);
    c->drawPath(v.toPath().makeTransform(m), line);
    auto isSel = [&](int ci, int i) { return std::find(m_selected.begin(), m_selected.end(), Ref{ci, i, 0}) != m_selected.end(); };
    for (int ci = 0; ci < int(v.contours.size()); ++ci) {
        const Contour& ct = v.contours[ci];
        const int n = int(ct.pts.size());
        for (int i = 0; i < n; ++i) {
            const bool show = isSel(ci, i) || isSel(ci, (i + 1) % n) || isSel(ci, (i - 1 + n) % n);
            const Anchor& a = ct.pts[i];
            if (show) {
                if (a.hasIn()) {
                    c->drawLine(m.mapPoint(a.p), m.mapPoint(a.in), line);
                    drawHandle(c, m.mapPoint(a.in), false, true, 6);
                }
                if (a.hasOut()) {
                    c->drawLine(m.mapPoint(a.p), m.mapPoint(a.out), line);
                    drawHandle(c, m.mapPoint(a.out), false, true, 6);
                }
            }
            drawHandle(c, m.mapPoint(a.p), isSel(ci, i), false, 7);
        }
    }
}

} // namespace drav
