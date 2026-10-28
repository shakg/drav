// Rulers, page guides, Alt-hover measurements, scaling and the eyedropper.

#include "Editor.h"

#include "core/Text.h"

#include <include/core/SkCanvas.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPixmap.h>
#include <include/core/SkRRect.h>
#include <include/core/SkSurface.h>

#include <QColor>

#include <cmath>

namespace drav {

namespace {
constexpr SkColor kRed = SkColorSetRGB(0xF2, 0x48, 0x22);
constexpr SkColor kGuideColor = SkColorSetARGB(0xCC, 0xF2, 0x48, 0x22);
constexpr SkColor kBlue = SkColorSetRGB(0x0D, 0x99, 0xFF);

QString fmt(float v)
{
    return std::fabs(v - std::round(v)) < 0.01f ? QString::number(int(std::round(v))) : QString::number(v, 'f', 1);
}

void pill(SkCanvas* c, float cx, float cy, const QString& text, SkColor color)
{
    const QByteArray utf8 = text.toUtf8();
    const SkFont font = text::uiFont(11, true);
    const float tw = font.measureText(utf8.constData(), utf8.size(), SkTextEncoding::kUTF8);
    SkRect r = SkRect::MakeXYWH(cx - tw / 2 - 4, cy - 8.5f, tw + 8, 17);
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    c->drawRRect(SkRRect::MakeRectXY(r, 3, 3), p);
    p.setColor(SK_ColorWHITE);
    c->drawSimpleText(utf8.constData(), utf8.size(), SkTextEncoding::kUTF8, r.left() + 4, r.top() + 12.5f, font, p);
}
} // namespace

void Editor::setRulers(bool on)
{
    if (on == m_rulers)
        return;
    m_rulers = on;
    emit viewChanged();
    requestUpdate();
}

bool Editor::inRuler(QPointF s, bool* vertical) const
{
    // The top ruler makes horizontal guides; the left ruler vertical ones.
    if (!m_rulers)
        return false;
    if (s.y() < kRulerSize && s.x() > kRulerSize) {
        *vertical = false;
        return true;
    }
    if (s.x() < kRulerSize && s.y() > kRulerSize) {
        *vertical = true;
        return true;
    }
    return false;
}

int Editor::guideAt(QPointF s) const
{
    const auto& guides = m_doc->at(m_doc->currentPage()).p.guides;
    for (int i = int(guides.size()) - 1; i >= 0; --i) {
        const Guide& g = guides[i];
        const QPointF at = toScreen(g.vertical ? SkPoint{g.pos, 0} : SkPoint{0, g.pos});
        if (g.vertical ? std::fabs(at.x() - s.x()) <= 3 : std::fabs(at.y() - s.y()) <= 3)
            return i;
    }
    return -1;
}

void Editor::drawPageGuides(SkCanvas* c)
{
    const auto& guides = m_doc->at(m_doc->currentPage()).p.guides;
    SkPaint p;
    p.setStrokeWidth(1);
    const float w = float(m_viewport.width()), h = float(m_viewport.height());
    auto line = [&](bool vertical, float pos, SkColor color) {
        p.setColor(color);
        const QPointF s = toScreen(vertical ? SkPoint{pos, 0} : SkPoint{0, pos});
        if (vertical)
            c->drawLine(std::round(s.x()) + 0.5f, 0, std::round(s.x()) + 0.5f, h, p);
        else
            c->drawLine(0, std::round(s.y()) + 0.5f, w, std::round(s.y()) + 0.5f, p);
    };
    for (int i = 0; i < int(guides.size()); ++i)
        if (!(m_guideDrag.active && m_guideDrag.index == i))
            line(guides[i].vertical, guides[i].pos, i == m_guideHover ? kRed : kGuideColor);
    if (m_guideDrag.active && !m_guideDrag.remove) {
        line(m_guideDrag.vertical, m_guideDrag.pos, kRed);
        const QPointF s = toScreen(m_guideDrag.vertical ? SkPoint{m_guideDrag.pos, 0} : SkPoint{0, m_guideDrag.pos});
        pill(c, m_guideDrag.vertical ? float(s.x()) : 60.f, m_guideDrag.vertical ? 30.f : float(s.y()), fmt(m_guideDrag.pos), kRed);
    }
}

void Editor::drawRulers(SkCanvas* c)
{
    const float w = float(m_viewport.width()), h = float(m_viewport.height()), r = kRulerSize;
    SkPaint bg;
    bg.setColor(SkColorSetRGB(0x2C, 0x2C, 0x2C));
    c->drawRect(SkRect::MakeXYWH(0, 0, w, r), bg);
    c->drawRect(SkRect::MakeXYWH(0, 0, r, h), bg);

    // Selection extent highlighted on both rulers.
    if (!m_doc->selection().empty()) {
        const SkRect b = view().mapRect(selectionBounds());
        SkPaint hl;
        hl.setColor(SkColorSetARGB(0x55, 0x0D, 0x99, 0xFF));
        c->drawRect(SkRect::MakeLTRB(std::max(b.left(), r), 0, b.right(), r), hl);
        c->drawRect(SkRect::MakeLTRB(0, std::max(b.top(), r), r, b.bottom()), hl);
    }

    // Major tick spacing: the smallest 1/2/5 step at least 60 px apart.
    float step = 1;
    for (float base = 1; base < 1e6f; base *= 10) {
        bool found = false;
        for (float m : {1.f, 2.f, 5.f})
            if (base * m * m_zoom >= 60) {
                step = base * m;
                found = true;
                break;
            }
        if (found)
            break;
    }
    SkPaint tick;
    tick.setColor(SkColorSetRGB(0x7A, 0x7A, 0x7A));
    tick.setStrokeWidth(1);
    SkPaint label;
    label.setAntiAlias(true);
    label.setColor(SkColorSetRGB(0xB3, 0xB3, 0xB3));
    const SkFont font = text::uiFont(9);
    const SkPoint topLeft = toPage({r, r}), bottomRight = toPage({w, h});
    for (float x = std::floor(topLeft.x() / step) * step; x <= bottomRight.x(); x += step / 5) {
        const float sx = std::round(float(toScreen({x, 0}).x())) + 0.5f;
        const bool major = std::fabs(std::remainder(x, step)) < step / 50;
        c->drawLine(sx, major ? 0 : r - 4, sx, r, tick);
        if (major) {
            const QByteArray t = fmt(x).toUtf8();
            c->drawSimpleText(t.constData(), t.size(), SkTextEncoding::kUTF8, sx + 3, 10, font, label);
        }
    }
    for (float y = std::floor(topLeft.y() / step) * step; y <= bottomRight.y(); y += step / 5) {
        const float sy = std::round(float(toScreen({0, y}).y())) + 0.5f;
        const bool major = std::fabs(std::remainder(y, step)) < step / 50;
        c->drawLine(major ? 0 : r - 4, sy, r, sy, tick);
        if (major) {
            const QByteArray t = fmt(y).toUtf8();
            c->save();
            c->translate(10, sy - 3);
            c->rotate(-90);
            c->drawSimpleText(t.constData(), t.size(), SkTextEncoding::kUTF8, 0, 0, font, label);
            c->restore();
        }
    }
    SkPaint edge;
    edge.setColor(SkColorSetRGB(0x44, 0x44, 0x44));
    c->drawLine(r, r - 0.5f, w, r - 0.5f, edge);
    c->drawLine(r - 0.5f, r, r - 0.5f, h, edge);
    c->drawRect(SkRect::MakeWH(r, r), bg);
}

void Editor::drawMeasurements(SkCanvas* c)
{
    const auto& sel = m_doc->selection();
    if (sel.empty())
        return;
    NodeId target = kNoNode;
    if (m_hover && m_doc->isAttached(m_hover) && std::find(sel.begin(), sel.end(), m_hover) == sel.end())
        target = m_hover;
    else if (NodeId parent = m_doc->at(sel.front()).parent; m_doc->at(parent).type() != NodeType::Page)
        target = parent;
    if (!target)
        return;
    const SkRect a = selectionBounds(), b = m_doc->worldBounds(target);
    SkPaint line;
    line.setAntiAlias(true);
    line.setColor(kRed);
    line.setStrokeWidth(1);
    line.setStyle(SkPaint::kStroke_Style);
    c->drawRect(view().mapRect(b), line);

    auto measure = [&](SkPoint p0, SkPoint p1) {
        const float d = SkPoint::Distance(p0, p1);
        if (d < 0.01f)
            return;
        const QPointF s0 = toScreen(p0), s1 = toScreen(p1);
        c->drawLine(float(s0.x()), float(s0.y()), float(s1.x()), float(s1.y()), line);
        const bool horiz = std::fabs(p0.y() - p1.y()) < 0.01f;
        for (const QPointF& e : {s0, s1}) {
            if (horiz)
                c->drawLine(float(e.x()), float(e.y()) - 4, float(e.x()), float(e.y()) + 4, line);
            else
                c->drawLine(float(e.x()) - 4, float(e.y()), float(e.x()) + 4, float(e.y()), line);
        }
        pill(c, float(s0.x() + s1.x()) / 2 + (horiz ? 0 : 16), float(s0.y() + s1.y()) / 2 + (horiz ? 12 : 0), fmt(d), kRed);
    };
    // One rectangle inside the other: distances to all four sides.
    auto inside = [&](const SkRect& in, const SkRect& out) {
        measure({out.left(), in.centerY()}, {in.left(), in.centerY()});
        measure({in.right(), in.centerY()}, {out.right(), in.centerY()});
        measure({in.centerX(), out.top()}, {in.centerX(), in.top()});
        measure({in.centerX(), in.bottom()}, {in.centerX(), out.bottom()});
    };
    if (b.contains(a)) {
        inside(a, b);
        return;
    }
    if (a.contains(b)) {
        inside(b, a);
        return;
    }
    // Separate or overlapping: gaps along each axis, drawn through the shared span if any.
    const float midY = std::max(a.top(), b.top()) < std::min(a.bottom(), b.bottom())
        ? (std::max(a.top(), b.top()) + std::min(a.bottom(), b.bottom())) / 2
        : a.centerY();
    const float midX = std::max(a.left(), b.left()) < std::min(a.right(), b.right())
        ? (std::max(a.left(), b.left()) + std::min(a.right(), b.right())) / 2
        : a.centerX();
    if (a.right() <= b.left())
        measure({a.right(), midY}, {b.left(), midY});
    else if (b.right() <= a.left())
        measure({b.right(), midY}, {a.left(), midY});
    if (a.bottom() <= b.top())
        measure({midX, a.bottom()}, {midX, b.top()});
    else if (b.bottom() <= a.top())
        measure({midX, b.bottom()}, {midX, a.top()});
    if (SkRect::Intersects(a, b)) {
        measure({std::min(a.left(), b.left()), midY}, {std::max(a.left(), b.left()), midY});
        measure({midX, std::min(a.top(), b.top())}, {midX, std::max(a.top(), b.top())});
    }
}

// ---------------------------------------------------------------- scale

void Editor::scaleNodes(const std::vector<NodeId>& ids, float f, SkPoint anchor)
{
    if (f <= 0)
        return;
    for (NodeId id : ids) {
        SkMatrix w = m_doc->worldTransform(id);
        const SkPoint origin = w.mapXY(0, 0);
        m_doc->scaleDeep(id, f);
        w.setTranslateX(anchor.x() + (origin.x() - anchor.x()) * f);
        w.setTranslateY(anchor.y() + (origin.y() - anchor.y()) * f);
        m_doc->setWorldTransform(id, w);
    }
}

void Editor::scaleSelection(double factor)
{
    auto ids = topLevelSelection();
    if (ids.empty() || factor <= 0 || factor == 1)
        return;
    const SkRect b = selectionBounds();
    m_doc->breakMerge();
    m_doc->begin(tr("Scale"));
    scaleNodes(ids, float(factor), {b.left(), b.top()});
    m_doc->commit();
}

// ---------------------------------------------------------------- eyedropper

void Editor::pickColor()
{
    if (m_toolName != QLatin1String("eyedropper"))
        m_toolBeforePick = m_toolName;
    m_pickForPopup = true;
    setTool(QStringLiteral("eyedropper"));
}

SkColor4f Editor::sampleColor(SkPoint page)
{
    if (m_contentDirty || !m_content) {
        m_content = m_renderer->recordPage(m_doc->currentPage());
        m_contentDirty = false;
    }
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(1, 1));
    SkCanvas* c = surface->getCanvas();
    c->clear(m_doc->at(m_doc->currentPage()).p.pageBackground);
    c->translate(-page.x(), -page.y());
    c->drawPicture(m_content);
    SkPixmap pm;
    if (!surface->peekPixels(&pm))
        return SkColors::kBlack;
    return pm.getColor4f(0, 0);
}

void Editor::finishPick(SkColor4f color, bool apply)
{
    color.fA = 1;
    const bool forPopup = m_pickForPopup;
    m_pickForPopup = false;
    setTool(m_toolBeforePick.isEmpty() || m_toolBeforePick == QLatin1String("eyedropper") ? QStringLiteral("select")
                                                                                          : m_toolBeforePick);
    if (!apply)
        return;
    if (forPopup) {
        emit colorPicked(QColor::fromRgbF(color.fR, color.fG, color.fB, 1));
        return;
    }
    auto sel = selected();
    if (sel.empty())
        return;
    m_doc->breakMerge();
    m_doc->begin(tr("Pick color"));
    for (NodeId id : sel) {
        Props& p = m_doc->edit(id);
        p.fillStyle.clear();
        auto it = std::find_if(p.fills.begin(), p.fills.end(), [](const Paint& x) { return x.type == PaintType::Solid; });
        if (it == p.fills.end()) {
            p.fills.push_back(Paint::solid(color));
        } else {
            it->color = color;
            it->colorVar.clear();
        }
    }
    m_doc->commit();
}

} // namespace drav
