#include "Editor.h"

#include "LayerModel.h"
#include "Tools.h"

#include "core/Geometry.h"
#include "core/Serializer.h"
#include "core/Text.h"
#include "render/Code.h"
#include "render/Export.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QBuffer>
#include <QMimeData>

#include <cmath>

namespace drav {

namespace {

// Sorts ids by z-order (document order within a parent, parents by first appearance).
std::vector<NodeId> zSorted(const Document& d, std::vector<NodeId> ids)
{
    std::unordered_map<NodeId, int> order;
    int i = 0;
    d.walk(d.root(), [&](const Node& n) {
        order[n.id] = i++;
        return true;
    });
    std::sort(ids.begin(), ids.end(), [&](NodeId a, NodeId b) { return order[a] < order[b]; });
    return ids;
}

QString namePath(const Document& d, NodeId id, NodeId root)
{
    QString path;
    for (NodeId c = id; c && c != root; c = d.at(c).parent)
        path.prepend(QLatin1Char('/') + d.at(c).p.name);
    return path;
}

QMap<QString, QString> parseVariantName(const QString& name)
{
    QMap<QString, QString> out;
    for (const QString& part : name.split(QLatin1Char(','))) {
        int eq = part.indexOf(QLatin1Char('='));
        if (eq > 0)
            out.insert(part.left(eq).trimmed(), part.mid(eq + 1).trimmed());
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------- history & clipboard

void Editor::undo()
{
    if (textEditing() || m_toolName == QLatin1String("vectorEdit") || m_toolName == QLatin1String("pen"))
        setTool(QStringLiteral("select"));
    m_doc->undo();
}

void Editor::redo()
{
    if (textEditing() || m_toolName == QLatin1String("vectorEdit"))
        setTool(QStringLiteral("select"));
    m_doc->redo();
}

void Editor::copy()
{
    auto sel = zSorted(*m_doc, topLevelSelection());
    if (sel.empty())
        return;
    m_internalClipboard = io::copy(*m_doc, sel);
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(io::kClipboardMime), m_internalClipboard);
    // Plain text of text layers helps pasting into other apps.
    QStringList texts;
    for (NodeId id : sel)
        m_doc->walk(id, [&](const Node& n) {
            if (n.type() == NodeType::Text)
                texts << n.p.text;
            return true;
        });
    if (!texts.isEmpty())
        mime->setText(texts.join(QLatin1Char('\n')));
    if (auto* cb = QGuiApplication::clipboard())
        cb->setMimeData(mime);
    else
        delete mime;
    setStatus(tr("Copied %n layer(s)", nullptr, int(sel.size())));
}

void Editor::copyAs(const QString& format)
{
    auto sel = zSorted(*m_doc, topLevelSelection());
    if (sel.empty())
        return;
    auto* mime = new QMimeData;
    if (format == QLatin1String("CSS")) {
        QStringList parts;
        for (NodeId id : sel)
            parts << code::css(*m_doc, id);
        mime->setText(parts.join(QStringLiteral("\n\n")));
    } else if (format == QLatin1String("SVG")) {
        const QByteArray svgData = exporter::exportNode(*m_doc, sel.front(), exporter::Format::SVG);
        mime->setData(QStringLiteral("image/svg+xml"), svgData);
        mime->setText(QString::fromUtf8(svgData));
    } else {
        const QByteArray png = exporter::exportNode(*m_doc, sel.front(), exporter::Format::PNG, 2);
        mime->setData(QStringLiteral("image/png"), png);
        mime->setImageData(QImage::fromData(png, "PNG"));
    }
    if (auto* cb = QGuiApplication::clipboard())
        cb->setMimeData(mime);
    else
        delete mime;
    setStatus(tr("Copied as %1").arg(format));
}

QString Editor::inspectCode() const
{
    const auto& sel = m_doc->selection();
    if (sel.empty())
        return {};
    QStringList parts{code::css(*m_doc, sel.front())};
    for (NodeId c : m_doc->at(sel.front()).children)
        parts << code::css(*m_doc, c);
    return parts.join(QStringLiteral("\n\n"));
}

void Editor::attachTextToPath()
{
    auto sel = selected();
    NodeId textId = kNoNode, shape = kNoNode;
    for (NodeId id : sel)
        (m_doc->at(id).type() == NodeType::Text ? textId : shape) = id;
    if (sel.size() != 2 || !textId || !shape || isContainer(m_doc->at(shape).type()))
        return;
    m_doc->breakMerge();
    m_doc->begin(tr("Text on path"));
    if (m_doc->at(textId).parent != m_doc->at(shape).parent)
        m_doc->move(textId, m_doc->at(shape).parent, m_doc->indexOf(shape) + 1);
    Props& p = m_doc->edit(textId);
    p.textPath = shape;
    p.textPathOffset = 0;
    p.textStyle.autoResize = TextAutoResize::None;
    select({textId});
    m_doc->commit();
}

void Editor::detachTextFromPath()
{
    m_doc->breakMerge();
    m_doc->begin(tr("Detach text from path"));
    for (NodeId id : selected())
        if (m_doc->at(id).p.textPath) {
            Props& p = m_doc->edit(id);
            p.textPath = kNoNode;
            p.textStyle.autoResize = TextAutoResize::WidthAndHeight;
            p.transform = SkMatrix::Translate(p.transform.getTranslateX(), p.transform.getTranslateY());
        }
    m_doc->commit();
}

void Editor::cut()
{
    copy();
    deleteSelection();
}

static NodeId pasteParent(Editor& e)
{
    Document& d = e.doc();
    auto sel = d.selection();
    if (sel.size() == 1) {
        const Node& n = d.at(sel[0]);
        if (isFrameLike(n.type()) && n.type() != NodeType::Instance && !d.isInsideInstance(n.id))
            return n.id;
        if (n.parent && !d.isInsideInstance(n.id) && d.at(n.parent).type() != NodeType::Instance)
            return n.parent;
    }
    return d.currentPage();
}

void Editor::paste()
{
    QByteArray data;
    const QMimeData* mime = QGuiApplication::clipboard() ? QGuiApplication::clipboard()->mimeData() : nullptr;
    const QString mt = QString::fromLatin1(io::kClipboardMime);
    if (mime && mime->hasFormat(mt))
        data = mime->data(mt);
    else if (mime && mime->hasImage()) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        m_doc->begin(tr("Paste image"));
        Props p = defaultProps(NodeType::Rectangle);
        p.name = tr("Image");
        Paint fill;
        fill.type = PaintType::Image;
        fill.imageHash = m_doc->addImage(png);
        p.fills = {fill};
        NodeId id = m_doc->create(p, pasteParent(*this));
        m_doc->resize(id, img.width(), img.height());
        placeNodes({id}, toPage(QPointF(m_viewport.width() / 2, m_viewport.height() / 2)));
        select({id});
        m_doc->commit();
        return;
    } else if (mime && mime->hasText() && !mime->text().isEmpty()) {
        m_doc->begin(tr("Paste text"));
        Props p = defaultProps(NodeType::Text);
        p.text = mime->text();
        NodeId id = m_doc->create(p, pasteParent(*this));
        m_doc->updateDerived();
        placeNodes({id}, toPage(QPointF(m_viewport.width() / 2, m_viewport.height() / 2)));
        select({id});
        m_doc->commit();
        return;
    } else {
        data = m_internalClipboard;
    }
    if (data.isEmpty())
        return;
    NodeId parent = pasteParent(*this);
    m_doc->begin(tr("Paste"));
    auto ids = io::paste(*m_doc, data, parent);
    m_doc->updateDerived();
    // Keep the original position when it is visible, otherwise center in view.
    SkRect b = SkRect::MakeEmpty();
    for (NodeId id : ids)
        b.join(m_doc->worldBounds(id));
    SkRect vis = SkRect::MakeLTRB(toPage({0, 0}).x(), toPage({0, 0}).y(),
        toPage(QPointF(m_viewport.width(), m_viewport.height())).x(),
        toPage(QPointF(m_viewport.width(), m_viewport.height())).y());
    if (!vis.contains(b))
        placeNodes(ids, vis.center());
    select(ids);
    m_doc->commit();
}

void Editor::pasteInPlace()
{
    QByteArray data = m_internalClipboard;
    const QMimeData* mime = QGuiApplication::clipboard() ? QGuiApplication::clipboard()->mimeData() : nullptr;
    const QString mt = QString::fromLatin1(io::kClipboardMime);
    if (mime && mime->hasFormat(mt))
        data = mime->data(mt);
    if (data.isEmpty())
        return;
    m_doc->begin(tr("Paste in place"));
    auto ids = io::paste(*m_doc, data, pasteParent(*this));
    select(ids);
    m_doc->commit();
}

void Editor::duplicate()
{
    auto sel = zSorted(*m_doc, topLevelSelection());
    if (sel.empty())
        return;
    m_doc->begin(tr("Duplicate"));
    std::vector<NodeId> out;
    for (NodeId id : sel) {
        if (m_doc->isInsideInstance(id))
            continue;
        NodeId parent = m_doc->at(id).parent;
        QByteArray clip = io::copy(*m_doc, {id});
        auto ids = io::paste(*m_doc, clip, parent, m_doc->indexOf(id) + 1);
        out.insert(out.end(), ids.begin(), ids.end());
    }
    select(out);
    m_doc->commit();
}

void Editor::deleteSelection()
{
    auto sel = topLevelSelection();
    if (sel.empty())
        return;
    m_doc->begin(tr("Delete"));
    for (NodeId id : sel) {
        if (m_doc->isInsideInstance(id))
            m_doc->edit(id).visible = false; // instance layers can only be hidden
        else
            m_doc->remove(id);
    }
    select({});
    m_doc->commit();
}

// ---------------------------------------------------------------- structure

void Editor::wrapSelection(NodeType type, const QString& label)
{
    auto sel = zSorted(*m_doc, topLevelSelection());
    std::erase_if(sel, [&](NodeId id) { return m_doc->isInsideInstance(id); });
    if (sel.empty())
        return;
    m_doc->begin(label);
    NodeId parent = m_doc->at(sel.back()).parent;
    int index = m_doc->indexOf(sel.back());
    SkRect b = SkRect::MakeEmpty();
    for (NodeId id : sel)
        b.join(m_doc->worldBounds(id));
    Props p = defaultProps(type);
    if (type == NodeType::Frame || type == NodeType::Component)
        p.fills.clear();
    if (type == NodeType::Component)
        p.name = sel.size() == 1 ? m_doc->at(sel[0]).p.name : p.name;
    NodeId wrap = m_doc->create(p, parent, index + 1);
    SkMatrix pw = m_doc->worldTransform(parent), inv;
    if (!pw.invert(&inv))
        inv.reset();
    SkPoint o = inv.mapXY(b.left(), b.top());
    m_doc->setPosition(wrap, o.x(), o.y());
    m_doc->resize(wrap, std::max(1.f, b.width()), std::max(1.f, b.height()));
    // Wrapper inherits the first child's layout participation.
    m_doc->edit(wrap).layoutChild = m_doc->at(sel[0]).p.layoutChild;
    for (NodeId id : sel) {
        reparentKeepWorld(id, wrap, -1);
        if (type != NodeType::Group && type != NodeType::BooleanOp)
            m_doc->edit(id).layoutChild.sizingH = m_doc->at(id).p.layoutChild.sizingH == LayoutSizing::Fill
                ? LayoutSizing::Fixed
                : m_doc->at(id).p.layoutChild.sizingH;
    }
    select({wrap});
    m_doc->commit();
}

void Editor::group() { wrapSelection(NodeType::Group, tr("Group")); }
void Editor::frameSelection() { wrapSelection(NodeType::Frame, tr("Frame selection")); }

void Editor::ungroup()
{
    auto sel = topLevelSelection();
    m_doc->begin(tr("Ungroup"));
    std::vector<NodeId> out;
    for (NodeId g : sel) {
        const Node& n = m_doc->at(g);
        if (!(n.type() == NodeType::Group || n.type() == NodeType::Frame || n.type() == NodeType::BooleanOp)
            || m_doc->isInsideInstance(g))
            continue;
        NodeId parent = n.parent;
        int index = m_doc->indexOf(g);
        auto kids = n.children;
        for (NodeId c : kids) {
            reparentKeepWorld(c, parent, index++);
            out.push_back(c);
        }
        m_doc->remove(g);
    }
    select(out);
    m_doc->commit();
}

void Editor::addAutoLayout()
{
    auto sel = topLevelSelection();
    if (sel.empty())
        return;
    if (sel.size() == 1 && isFrameLike(m_doc->at(sel[0]).type())) {
        if (m_doc->at(sel[0]).p.layout.mode != LayoutMode::None)
            return;
        m_doc->begin(tr("Add auto layout"));
        addAutoLayoutTo(sel[0], LayoutMode::None);
        m_doc->commit();
        return;
    }
    wrapSelection(NodeType::Frame, tr("Add auto layout"));
    if (selected().size() != 1)
        return;
    m_doc->begin(tr("Add auto layout"));
    m_doc->setMergeKey(QStringLiteral("autolayout-wrap"));
    addAutoLayoutTo(selected()[0], LayoutMode::None);
    m_doc->commit();
}

void Editor::addAutoLayoutTo(NodeId frame, LayoutMode forced)
{
    const Node& f = m_doc->at(frame);
    // Infer direction, gap and padding from the current arrangement.
    std::vector<NodeId> kids = f.children;
    SkRect u = SkRect::MakeEmpty();
    float sumW = 0, sumH = 0;
    for (NodeId c : kids) {
        const Props& cp = m_doc->at(c).p;
        u.join(cp.transform.mapRect(SkRect::MakeWH(cp.width, cp.height)));
        sumW += cp.width;
        sumH += cp.height;
    }
    const float spanX = u.width() - sumW, spanY = u.height() - sumH;
    if (forced == LayoutMode::Grid) {
        // Row-major order; the first row's length sets the column count.
        auto box = [&](NodeId c) { const Props& cp = m_doc->at(c).p; return cp.transform.mapRect(SkRect::MakeWH(cp.width, cp.height)); };
        std::sort(kids.begin(), kids.end(), [&](NodeId a, NodeId b) {
            const SkRect ra = box(a), rb = box(b);
            if (std::fabs(ra.centerY() - rb.centerY()) > std::min(ra.height(), rb.height()) / 2)
                return ra.centerY() < rb.centerY();
            return ra.left() < rb.left();
        });
        int cols = 0;
        for (NodeId c : kids)
            if (!kids.empty() && std::fabs(box(c).centerY() - box(kids.front()).centerY()) <= box(kids.front()).height() / 2)
                ++cols;
        for (int i = 0; i < (int)kids.size(); ++i)
            m_doc->move(kids[i], frame, i);
        const float w = f.p.width, h = f.p.height;
        Props& p = m_doc->edit(frame);
        p.layout.mode = LayoutMode::Grid;
        p.layout.gridColumns = std::max(1, cols ? cols : 2);
        p.layout.spacing = p.layout.counterSpacing = 10;
        p.layout.padL = kids.empty() ? 10 : std::max(0.f, std::round(u.left()));
        p.layout.padT = kids.empty() ? 10 : std::max(0.f, std::round(u.top()));
        p.layout.padR = kids.empty() ? 10 : std::max(0.f, std::round(w - u.right()));
        p.layout.padB = kids.empty() ? 10 : std::max(0.f, std::round(h - u.bottom()));
        p.layoutChild.sizingV = LayoutSizing::Hug;
        return;
    }
    const bool horiz = forced == LayoutMode::None ? (kids.size() > 1 && spanX > spanY) : forced == LayoutMode::Horizontal;
    auto key = [&](NodeId c) {
        const Props& cp = m_doc->at(c).p;
        return horiz ? cp.transform.getTranslateX() : cp.transform.getTranslateY();
    };
    std::sort(kids.begin(), kids.end(), [&](NodeId a, NodeId b) { return key(a) < key(b); });
    for (int i = 0; i < (int)kids.size(); ++i)
        m_doc->move(kids[i], frame, i);
    const float gap = kids.size() > 1 ? std::max(0.f, std::round((horiz ? spanX : spanY) / (kids.size() - 1))) : 10;
    const float w = f.p.width, h = f.p.height;
    Props& p = m_doc->edit(frame);
    p.layout.mode = horiz ? LayoutMode::Horizontal : LayoutMode::Vertical;
    p.layout.spacing = gap;
    if (kids.empty()) {
        p.layout.padL = p.layout.padR = p.layout.padT = p.layout.padB = 10;
    } else {
        p.layout.padL = std::max(0.f, std::round(u.left()));
        p.layout.padT = std::max(0.f, std::round(u.top()));
        p.layout.padR = std::max(0.f, std::round(w - u.right()));
        p.layout.padB = std::max(0.f, std::round(h - u.bottom()));
    }
    p.layoutChild.sizingH = LayoutSizing::Hug;
    p.layoutChild.sizingV = LayoutSizing::Hug;
}

void Editor::removeAutoLayout()
{
    m_doc->begin(tr("Remove auto layout"));
    for (NodeId id : selected())
        if (m_doc->at(id).p.layout.mode != LayoutMode::None) {
            Props& p = m_doc->edit(id);
            p.layout.mode = LayoutMode::None;
            if (p.layoutChild.sizingH == LayoutSizing::Hug)
                p.layoutChild.sizingH = LayoutSizing::Fixed;
            if (p.layoutChild.sizingV == LayoutSizing::Hug)
                p.layoutChild.sizingV = LayoutSizing::Fixed;
        }
    m_doc->commit();
}

// ---------------------------------------------------------------- components

void Editor::createComponent()
{
    auto sel = topLevelSelection();
    if (sel.empty())
        return;
    if (sel.size() == 1 && m_doc->at(sel[0]).type() == NodeType::Frame && !m_doc->isInsideInstance(sel[0])) {
        m_doc->begin(tr("Create component"));
        m_doc->edit(sel[0]).type = NodeType::Component;
        m_doc->commit();
        setStatus(tr("Created component"));
        return;
    }
    wrapSelection(NodeType::Component, tr("Create component"));
}

void Editor::combineAsVariants()
{
    auto sel = zSorted(*m_doc, topLevelSelection());
    for (NodeId id : sel)
        if (m_doc->at(id).type() != NodeType::Component)
            return setStatus(tr("Select main components to combine"));
    if (sel.empty())
        return;
    wrapSelection(NodeType::ComponentSet, tr("Combine as variants"));
    if (selected().size() != 1)
        return;
    NodeId set = selected()[0];
    m_doc->begin(tr("Combine as variants"));
    m_doc->setMergeKey(QStringLiteral("variants"));
    Props& sp = m_doc->edit(set);
    sp.name = m_doc->at(sel[0]).p.name.section(QLatin1Char('='), 0, 0).section(QLatin1Char(','), 0, 0);
    if (sp.name.isEmpty() || sp.name.contains(QLatin1Char('=')))
        sp.name = tr("Component set");
    Paint stroke = Paint::solid({0x97 / 255.f, 0x47 / 255.f, 1, 1});
    sp.strokes = {stroke};
    sp.stroke.dashes = {10, 5};
    sp.stroke.align = StrokeAlign::Inside;
    sp.corners = {5, 5, 5, 5};
    sp.layout.mode = LayoutMode::Horizontal;
    sp.layout.wrap = true;
    sp.layout.spacing = sp.layout.counterSpacing = 20;
    sp.layout.padL = sp.layout.padR = sp.layout.padT = sp.layout.padB = 20;
    sp.layoutChild.sizingH = LayoutSizing::Hug;
    sp.layoutChild.sizingV = LayoutSizing::Hug;
    int i = 1;
    for (NodeId c : sel) {
        Props& cp = m_doc->edit(c);
        cp.variantProps = parseVariantName(cp.name);
        if (cp.variantProps.isEmpty()) {
            cp.name = QStringLiteral("Property 1=") + (i == 1 ? QStringLiteral("Default") : QStringLiteral("Variant%1").arg(i));
            cp.variantProps = parseVariantName(cp.name);
        }
        ++i;
    }
    m_doc->commit();
}

void Editor::detachInstance()
{
    auto sel = topLevelSelection();
    m_doc->begin(tr("Detach instance"));
    std::vector<NodeId> out;
    for (NodeId id : sel) {
        if (m_doc->at(id).type() != NodeType::Instance || m_doc->isInsideInstance(id))
            continue;
        {
            Props& p = m_doc->edit(id);
            p.type = NodeType::Frame;
            p.componentId = kNoNode;
            p.overrides = 0;
            p.sourceId = kNoNode;
            p.propValues.clear();
        }
        // Descendants become plain layers; nested instances become top-level instances of their main.
        std::function<void(NodeId, bool)> rec = [&](NodeId n, bool inNested) {
            for (NodeId c : m_doc->at(n).children) {
                const Node& cn = m_doc->at(c);
                if (!inNested) {
                    Props& cp = m_doc->edit(c);
                    cp.propRefs.clear(); // the properties belonged to the detached component
                    if (cn.type() == NodeType::Instance) {
                        cp.sourceId = kNoNode;
                        rec(c, true);
                    } else {
                        cp.sourceId = kNoNode;
                        cp.overrides = 0;
                        rec(c, false);
                    }
                } else {
                    // Re-point at the nested main component's own nodes.
                    NodeId src = cn.p.sourceId;
                    if (src && m_doc->exists(src) && m_doc->at(src).p.sourceId)
                        m_doc->edit(c).sourceId = m_doc->at(src).p.sourceId;
                    rec(c, true);
                }
            }
        };
        rec(id, false);
        out.push_back(id);
    }
    m_doc->commit();
    if (!out.empty())
        setStatus(tr("Detached instance"));
}

void Editor::resetOverrides()
{
    m_doc->begin(tr("Reset overrides"));
    for (NodeId id : selected()) {
        NodeId inst = m_doc->instanceRootOf(id);
        if (!inst)
            continue;
        m_doc->walk(id, [&](const Node& n) {
            NodeId main = m_doc->mainOf(n.id);
            if (n.p.overrides && main && m_doc->exists(main)) {
                Props& p = m_doc->edit(n.id);
                const bool root = n.type() == NodeType::Instance && !p.sourceId;
                copyNonOverridden(p, m_doc->at(main).p, root ? (OvSize | OvName | OvLayout) : 0, root);
                p.overrides = 0;
            }
            return true;
        });
    }
    m_doc->commit();
}

void Editor::goToMainComponent()
{
    auto sel = selected();
    if (sel.size() != 1)
        return;
    NodeId main = m_doc->at(sel[0]).p.componentId;
    if (!main || !m_doc->isAttached(main))
        return setStatus(tr("Main component not found"));
    m_doc->setCurrentPage(m_doc->pageOf(main));
    select({main});
    zoomToSelection();
}

// Swaps an instance's main component, carrying overrides over by layer name path.
static void swapMain(Editor& e, NodeId inst, NodeId comp)
{
    Document& d = e.doc();
    QHash<QString, Props> saved;
    d.walk(inst, [&](const Node& n) {
        if (n.id != inst && n.p.overrides)
            saved.insert(namePath(d, n.id, inst), n.p);
        return true;
    });
    d.edit(inst).componentId = comp;
    d.updateDerived();
    d.walk(inst, [&](const Node& n) {
        auto it = saved.find(namePath(d, n.id, inst));
        if (it != saved.end() && n.id != inst) {
            Props& p = d.edit(n.id);
            const std::uint32_t ov = it->overrides & ~(OvName | OvSize);
            copyGroups(p, *it, ov);
            p.overrides |= ov;
        }
        return true;
    });
}

void Editor::swapComponent(qint64 componentId)
{
    NodeId comp = NodeId(componentId);
    if (!m_doc->isAttached(comp) || m_doc->at(comp).type() != NodeType::Component)
        return;
    m_doc->begin(tr("Swap instance"));
    for (NodeId id : selected())
        if (m_doc->at(id).type() == NodeType::Instance && !m_doc->isAncestor(id, comp))
            swapMain(*this, id, comp);
    m_doc->commit();
}

void Editor::setVariant(const QString& prop, const QString& value)
{
    m_doc->begin(tr("Change variant"));
    for (NodeId id : selected()) {
        const Node& n = m_doc->at(id);
        if (n.type() != NodeType::Instance || !m_doc->isAttached(n.p.componentId))
            continue;
        const Node& main = m_doc->at(n.p.componentId);
        if (m_doc->at(main.parent).type() != NodeType::ComponentSet)
            continue;
        QMap<QString, QString> want = main.p.variantProps;
        want[prop] = value;
        NodeId best = kNoNode;
        int bestScore = -1;
        for (NodeId c : m_doc->at(main.parent).children) {
            const auto& vp = m_doc->at(c).p.variantProps;
            if (vp.value(prop) != value)
                continue;
            int score = 0;
            for (auto it = want.begin(); it != want.end(); ++it)
                score += vp.value(it.key()) == it.value();
            if (score > bestScore) {
                bestScore = score;
                best = c;
            }
        }
        if (best && best != n.p.componentId)
            swapMain(*this, id, best);
    }
    m_doc->commit();
}

void Editor::placeComponent(qint64 componentId)
{
    NodeId comp = NodeId(componentId);
    if (!m_doc->isAttached(comp))
        return;
    m_doc->begin(tr("Insert instance"));
    Props p;
    p.type = NodeType::Instance;
    p.componentId = comp;
    p.name = m_doc->at(comp).p.name;
    NodeId parent = pasteParent(*this);
    if (m_doc->isAncestor(comp, parent) || parent == comp)
        parent = m_doc->currentPage();
    NodeId id = m_doc->create(p, parent);
    m_doc->updateDerived();
    placeNodes({id}, toPage(QPointF(m_viewport.width() / 2, m_viewport.height() / 2)));
    select({id});
    m_doc->commit();
}

// ---------------------------------------------------------------- vector ops

void Editor::booleanOp(const QString& op)
{
    BooleanOpType t = op == QLatin1String("subtract") ? BooleanOpType::Subtract
        : op == QLatin1String("intersect")             ? BooleanOpType::Intersect
        : op == QLatin1String("exclude")               ? BooleanOpType::Exclude
                                                       : BooleanOpType::Union;
    auto sel = selected();
    if (sel.size() == 1 && m_doc->at(sel[0]).type() == NodeType::BooleanOp) {
        m_doc->begin(tr("Boolean"));
        m_doc->edit(sel[0]).boolOp = t;
        m_doc->commit();
        return;
    }
    if (sel.size() < 2)
        return setStatus(tr("Select two or more layers"));
    wrapSelection(NodeType::BooleanOp, tr("Boolean"));
    if (selected().size() != 1)
        return;
    NodeId b = selected()[0];
    m_doc->begin(tr("Boolean"));
    m_doc->setMergeKey(QStringLiteral("boolean"));
    Props& p = m_doc->edit(b);
    p.boolOp = t;
    static const char* names[] = {"Union", "Subtract", "Intersect", "Exclude"};
    p.name = QString::fromLatin1(names[int(t)]);
    // Style comes from the bottom-most operand, like Figma.
    const Props& first = m_doc->at(m_doc->at(b).children.front()).p;
    p.fills = first.fills;
    p.strokes = first.strokes;
    p.stroke = first.stroke;
    m_doc->commit();
}

void Editor::flatten()
{
    auto sel = topLevelSelection();
    m_doc->begin(tr("Flatten"));
    std::vector<NodeId> out;
    for (NodeId id : sel) {
        const Node& n = m_doc->at(id);
        if (n.type() == NodeType::Text || isFrameLike(n.type()) || m_doc->isInsideInstance(id)) {
            out.push_back(id);
            continue;
        }
        Props p = n.p;
        if (n.type() == NodeType::Group && !n.children.empty()) {
            const Props& c = m_doc->at(n.children.front()).p;
            p.fills = c.fills;
            p.strokes = c.strokes;
            p.stroke = c.stroke;
        }
        SkPath path = fillPath(*m_doc, id);
        SkRect b = path.computeTightBounds();
        path.offset(-b.left(), -b.top());
        p.type = NodeType::Vector;
        p.vector = vectorFromPath(path);
        p.transform.preTranslate(b.left(), b.top());
        p.width = std::max(b.width(), 0.01f);
        p.height = std::max(b.height(), 0.01f);
        p.corners = {};
        NodeId parent = n.parent;
        int idx = m_doc->indexOf(id);
        m_doc->remove(id);
        out.push_back(m_doc->create(p, parent, idx));
    }
    select(out);
    m_doc->commit();
}

void Editor::outlineStroke()
{
    auto sel = topLevelSelection();
    m_doc->begin(tr("Outline stroke"));
    std::vector<NodeId> out;
    for (NodeId id : sel) {
        const Node& n = m_doc->at(id);
        if (n.p.strokes.empty() || n.type() == NodeType::Text || m_doc->isInsideInstance(id)) {
            out.push_back(id);
            continue;
        }
        Props p = n.p;
        SkPath outline = strokeOutline(p, strokePath(*m_doc, id));
        SkRect b = outline.computeTightBounds();
        outline.offset(-b.left(), -b.top());
        p.type = NodeType::Vector;
        p.vector = vectorFromPath(outline);
        p.fills = p.strokes;
        p.strokes.clear();
        p.transform.preTranslate(b.left(), b.top());
        p.width = b.width();
        p.height = b.height();
        NodeId parent = n.parent;
        int idx = m_doc->indexOf(id);
        m_doc->remove(id);
        out.push_back(m_doc->create(p, parent, idx));
    }
    select(out);
    m_doc->commit();
}

void Editor::useAsMask()
{
    auto sel = zSorted(*m_doc, topLevelSelection());
    if (sel.empty())
        return;
    if (sel.size() == 1) {
        m_doc->begin(tr("Use as mask"));
        m_doc->edit(sel[0]).isMask = !m_doc->at(sel[0]).p.isMask;
        m_doc->commit();
        return;
    }
    wrapSelection(NodeType::Group, tr("Use as mask"));
    if (selected().size() != 1)
        return;
    NodeId g = selected()[0];
    m_doc->begin(tr("Use as mask"));
    m_doc->setMergeKey(QStringLiteral("mask"));
    m_doc->edit(m_doc->at(g).children.front()).isMask = true;
    m_doc->edit(g).name = tr("Mask group");
    m_doc->commit();
}

// ---------------------------------------------------------------- arrange

static void translateWorld(Document& d, NodeId id, float dx, float dy)
{
    SkMatrix w = d.worldTransform(id);
    w.postTranslate(dx, dy);
    d.setWorldTransform(id, w);
}

void Editor::align(const QString& how)
{
    auto sel = topLevelSelection();
    if (sel.empty())
        return;
    SkRect ref;
    if (sel.size() == 1) {
        NodeId parent = m_doc->at(sel[0]).parent;
        if (m_doc->at(parent).type() == NodeType::Page)
            return;
        ref = m_doc->worldBounds(parent);
    } else {
        ref = selectionBounds();
    }
    m_doc->begin(tr("Align"));
    for (NodeId id : sel) {
        SkRect b = m_doc->worldBounds(id);
        float dx = 0, dy = 0;
        if (how == QLatin1String("left")) dx = ref.left() - b.left();
        else if (how == QLatin1String("right")) dx = ref.right() - b.right();
        else if (how == QLatin1String("hcenter")) dx = ref.centerX() - b.centerX();
        else if (how == QLatin1String("top")) dy = ref.top() - b.top();
        else if (how == QLatin1String("bottom")) dy = ref.bottom() - b.bottom();
        else if (how == QLatin1String("vcenter")) dy = ref.centerY() - b.centerY();
        if (dx || dy)
            translateWorld(*m_doc, id, snapValue(dx), snapValue(dy));
    }
    m_doc->commit();
}

void Editor::distribute(bool horizontal)
{
    auto sel = topLevelSelection();
    if (sel.size() < 3)
        return;
    std::sort(sel.begin(), sel.end(), [&](NodeId a, NodeId b) {
        SkRect ra = m_doc->worldBounds(a), rb = m_doc->worldBounds(b);
        return horizontal ? ra.left() < rb.left() : ra.top() < rb.top();
    });
    SkRect all = selectionBounds();
    float total = 0;
    for (NodeId id : sel) {
        SkRect b = m_doc->worldBounds(id);
        total += horizontal ? b.width() : b.height();
    }
    const float gap = ((horizontal ? all.width() : all.height()) - total) / (sel.size() - 1);
    m_doc->begin(tr("Distribute"));
    float cursor = horizontal ? all.left() : all.top();
    for (NodeId id : sel) {
        SkRect b = m_doc->worldBounds(id);
        float d = cursor - (horizontal ? b.left() : b.top());
        translateWorld(*m_doc, id, horizontal ? d : 0, horizontal ? 0 : d);
        cursor += (horizontal ? b.width() : b.height()) + gap;
    }
    m_doc->commit();
}

void Editor::reorder(int delta, bool toEnd)
{
    auto sel = zSorted(*m_doc, topLevelSelection());
    if (sel.empty())
        return;
    m_doc->begin(tr("Reorder"));
    if (delta > 0)
        std::reverse(sel.begin(), sel.end());
    for (NodeId id : sel) {
        if (m_doc->isInsideInstance(id))
            continue;
        NodeId parent = m_doc->at(id).parent;
        const int n = int(m_doc->at(parent).children.size());
        int i = m_doc->indexOf(id);
        int j = toEnd ? (delta > 0 ? n - 1 : 0) : std::clamp(i + delta, 0, n - 1);
        if (j != i)
            m_doc->move(id, parent, j);
    }
    m_doc->commit();
}

void Editor::bringForward() { reorder(1, false); }
void Editor::sendBackward() { reorder(-1, false); }
void Editor::bringToFront() { reorder(1, true); }
void Editor::sendToBack() { reorder(-1, true); }

void Editor::flip(bool horizontal)
{
    auto sel = topLevelSelection();
    if (sel.empty())
        return;
    m_doc->begin(tr("Flip"));
    for (NodeId id : sel) {
        Props& p = m_doc->edit(id);
        if (horizontal)
            p.transform.preConcat(SkMatrix::MakeAll(-1, 0, p.width, 0, 1, 0, 0, 0, 1));
        else
            p.transform.preConcat(SkMatrix::MakeAll(1, 0, 0, 0, -1, p.height, 0, 0, 1));
    }
    m_doc->commit();
}

void Editor::rotateSelection(double degrees)
{
    auto sel = topLevelSelection();
    if (sel.empty())
        return;
    m_doc->begin(tr("Rotate"));
    for (NodeId id : sel) {
        Props& p = m_doc->edit(id);
        p.transform.preRotate(float(degrees), p.width / 2, p.height / 2);
    }
    m_doc->commit();
}

void Editor::toggleVisible()
{
    auto sel = selected();
    if (sel.empty())
        return;
    bool anyVisible = false;
    for (NodeId id : sel)
        anyVisible |= m_doc->at(id).p.visible;
    m_doc->begin(anyVisible ? tr("Hide") : tr("Show"));
    for (NodeId id : sel)
        m_doc->edit(id).visible = !anyVisible;
    m_doc->commit();
}

void Editor::toggleLocked()
{
    auto sel = selected();
    if (sel.empty())
        return;
    bool anyUnlocked = false;
    for (NodeId id : sel)
        anyUnlocked |= !m_doc->at(id).p.locked;
    m_doc->begin(anyUnlocked ? tr("Lock") : tr("Unlock"));
    for (NodeId id : sel)
        m_doc->edit(id).locked = anyUnlocked;
    if (anyUnlocked)
        select({});
    m_doc->commit();
}

void Editor::setNodeVisible(qint64 id, bool visible)
{
    if (!m_doc->isAttached(NodeId(id)))
        return;
    m_doc->begin(visible ? tr("Show") : tr("Hide"));
    m_doc->edit(NodeId(id)).visible = visible;
    m_doc->commit();
}

void Editor::setNodeLocked(qint64 id, bool locked)
{
    if (!m_doc->isAttached(NodeId(id)))
        return;
    m_doc->begin(locked ? tr("Lock") : tr("Unlock"));
    m_doc->edit(NodeId(id)).locked = locked;
    m_doc->commit();
}

void Editor::setNodeExpanded(qint64 id, bool expanded)
{
    m_layers->setExpanded(NodeId(id), expanded);
}

void Editor::renameNode(qint64 id, const QString& name)
{
    if (!m_doc->isAttached(NodeId(id)) || name.isEmpty() || m_doc->at(NodeId(id)).p.name == name)
        return;
    m_doc->begin(tr("Rename"));
    Props& p = m_doc->edit(NodeId(id));
    p.name = name;
    if (p.type == NodeType::Component)
        p.variantProps = parseVariantName(name);
    m_doc->commit();
}

void Editor::moveNodeInTree(qint64 idv, qint64 parentv, int index)
{
    NodeId id = NodeId(idv), parent = NodeId(parentv);
    if (!m_doc->isAttached(id) || !m_doc->isAttached(parent) || id == parent || m_doc->isAncestor(id, parent))
        return;
    if (m_doc->isInsideInstance(id) || m_doc->instanceRootOf(parent))
        return;
    const NodeType pt = m_doc->at(parent).type();
    if (!isContainer(pt) || pt == NodeType::Document)
        return;
    m_doc->begin(tr("Move layer"));
    if (m_doc->at(id).parent == parent && m_doc->indexOf(id) < index)
        --index;
    reparentKeepWorld(id, parent, index);
    if (pt == NodeType::Page && m_doc->pageOf(id) != m_doc->currentPage())
        select({});
    m_doc->commit();
}

void Editor::nudge(double dx, double dy)
{
    auto sel = topLevelSelection();
    if (sel.empty())
        return;
    m_doc->begin(tr("Nudge"));
    m_doc->setMergeKey(QStringLiteral("nudge"));
    for (NodeId id : sel) {
        const Node& n = m_doc->at(id);
        const Props& pp = m_doc->at(n.parent).p;
        if (pp.layout.mode != LayoutMode::None && !n.p.layoutChild.absolute) {
            // Arrow keys reorder inside auto layout.
            const bool horiz = pp.layout.mode == LayoutMode::Horizontal;
            const double d = horiz ? dx : dy;
            if (d == 0)
                continue;
            int i = m_doc->indexOf(id);
            int j = std::clamp(i + (d > 0 ? 1 : -1), 0, int(m_doc->at(n.parent).children.size()) - 1);
            if (i != j)
                m_doc->move(id, n.parent, j);
            continue;
        }
        translateWorld(*m_doc, id, float(dx), float(dy));
    }
    m_doc->commit();
}

// ---------------------------------------------------------------- pages

QVariantList Editor::pages() const
{
    QVariantList out;
    for (NodeId p : m_doc->pages())
        if (m_doc->at(p).p.libraryPage.isEmpty()) // pages holding library imports stay hidden
            out << QVariantMap{{QStringLiteral("id"), qint64(p)}, {QStringLiteral("name"), m_doc->at(p).p.name}};
    return out;
}

void Editor::setCurrentPage(qint64 id)
{
    if (textEditing() || m_toolName == QLatin1String("vectorEdit"))
        setTool(QStringLiteral("select"));
    m_doc->setCurrentPage(NodeId(id));
}

void Editor::addPage()
{
    m_doc->begin(tr("Add page"));
    Props p;
    p.type = NodeType::Page;
    p.name = tr("Page %1").arg(m_doc->pages().size() + 1);
    NodeId id = m_doc->create(p, m_doc->root());
    m_doc->setCurrentPage(id);
    m_doc->commit();
}

void Editor::renamePage(qint64 id, const QString& name) { renameNode(id, name); }

void Editor::deletePage(qint64 id)
{
    if (m_doc->pages().size() <= 1 || !m_doc->isAttached(NodeId(id)))
        return;
    m_doc->begin(tr("Delete page"));
    if (m_doc->currentPage() == NodeId(id)) {
        auto ps = m_doc->pages();
        m_doc->setCurrentPage(ps.front() == NodeId(id) ? ps[1] : ps.front());
    }
    m_doc->remove(NodeId(id));
    m_doc->commit();
}

void Editor::duplicatePage(qint64 id)
{
    if (!m_doc->isAttached(NodeId(id)))
        return;
    m_doc->begin(tr("Duplicate page"));
    auto ids = io::paste(*m_doc, io::copy(*m_doc, {NodeId(id)}), m_doc->root(), m_doc->indexOf(NodeId(id)) + 1);
    if (!ids.empty()) {
        m_doc->edit(ids[0]).name = m_doc->at(NodeId(id)).p.name + tr(" copy");
        m_doc->setCurrentPage(ids[0]);
    }
    m_doc->commit();
}

QStringList Editor::fontFamilies() const
{
    return text::families();
}

QVariantList Editor::components() const
{
    QVariantList out;
    for (NodeId c : m_doc->allComponents()) {
        if (m_doc->isInsideInstance(c))
            continue;
        const Node& n = m_doc->at(c);
        const Node& parent = m_doc->at(n.parent);
        const bool inSet = parent.type() == NodeType::ComponentSet;
        out << QVariantMap{{QStringLiteral("id"), qint64(c)},
            {QStringLiteral("name"), n.p.name},
            {QStringLiteral("set"), inSet ? parent.p.name : QString()},
            {QStringLiteral("page"), m_doc->at(m_doc->pageOf(c)).p.name},
            {QStringLiteral("remote"), !n.p.remote.isEmpty()}};
    }
    return out;
}

} // namespace drav
