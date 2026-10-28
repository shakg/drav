#include "Editor.h"

#include "LayerModel.h"
#include "SvgImport.h"
#include "Tools.h"

#include "core/Geometry.h"
#include "core/Serializer.h"
#include "core/Text.h"
#include "render/Export.h"

#include <include/core/SkCanvas.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPathUtils.h>
#include <include/core/SkPictureRecorder.h>
#include <include/core/SkRRect.h>

#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QSaveFile>
#include <QTimer>

#include <cmath>

namespace drav {

namespace {
constexpr SkColor kBlue = SkColorSetRGB(0x0D, 0x99, 0xFF);
constexpr SkColor kPurple = SkColorSetRGB(0x97, 0x47, 0xFF);
constexpr SkColor kGuide = SkColorSetRGB(0xF2, 0x48, 0x22);

bool isComponentish(const Document& d, NodeId id)
{
    const NodeType t = d.at(id).type();
    return t == NodeType::Component || t == NodeType::ComponentSet || t == NodeType::Instance
        || d.instanceRootOf(id) != kNoNode;
}
} // namespace

Editor::Editor(QObject* parent)
    : QObject(parent)
    , m_doc(std::make_unique<Document>())
    , m_renderer(std::make_unique<Renderer>(*m_doc))
{
    text::init();
    Renderer::Options ro;
    ro.showGrids = true;
    m_renderer->setOptions(ro);
    m_layers = new LayerModel(this);

    m_tools["select"] = std::make_unique<SelectTool>(*this);
    m_tools["hand"] = std::make_unique<HandTool>(*this);
    m_tools["frame"] = std::make_unique<ShapeTool>(*this, NodeType::Frame);
    m_tools["section"] = std::make_unique<ShapeTool>(*this, NodeType::Section);
    m_tools["rect"] = std::make_unique<ShapeTool>(*this, NodeType::Rectangle);
    m_tools["ellipse"] = std::make_unique<ShapeTool>(*this, NodeType::Ellipse);
    m_tools["polygon"] = std::make_unique<ShapeTool>(*this, NodeType::Polygon);
    m_tools["star"] = std::make_unique<ShapeTool>(*this, NodeType::Star);
    m_tools["line"] = std::make_unique<ShapeTool>(*this, NodeType::Line);
    m_tools["arrow"] = std::make_unique<ShapeTool>(*this, NodeType::Line, true);
    m_tools["text"] = std::make_unique<TextTool>(*this);
    m_tools["textEdit"] = std::make_unique<TextEditTool>(*this);
    m_tools["pen"] = std::make_unique<PenTool>(*this);
    m_tools["pencil"] = std::make_unique<PencilTool>(*this);
    m_tools["vectorEdit"] = std::make_unique<VectorEditTool>(*this);
    m_tools["scale"] = std::make_unique<ScaleTool>(*this);
    m_tools["eyedropper"] = std::make_unique<EyedropperTool>(*this);
    m_tools["crop"] = std::make_unique<CropTool>(*this);
    setTool(QStringLiteral("select"));

    connect(m_doc.get(), &Document::changed, this, &Editor::onDocChanged);
    connect(m_doc.get(), &Document::selectionChanged, this, &Editor::onSelectionChanged);
    connect(m_doc.get(), &Document::undoStateChanged, this, &Editor::undoChanged);
    connect(m_doc.get(), &Document::pageChanged, this, [this] {
        m_contentDirty = true;
        m_layers->sync();
        emit pagesChanged();
        requestUpdate();
    });
    m_layers->sync();

    newSession();
    auto* autosave = new QTimer(this);
    autosave->setInterval(30000);
    connect(autosave, &QTimer::timeout, this, [this] {
        if (m_doc->isDirty())
            autosaveNow();
    });
    autosave->start();
}

Editor::~Editor()
{
    // A clean exit with nothing unsaved needs no recovery file.
    if (!m_doc->isDirty())
        QFile::remove(recoveryPath());
}

QObject* Editor::layersObject() const { return m_layers; }

// ---------------------------------------------------------------- view

SkMatrix Editor::view() const
{
    SkMatrix m = SkMatrix::Translate(float(m_pan.x()), float(m_pan.y()));
    m.preScale(float(m_zoom), float(m_zoom));
    return m;
}

SkPoint Editor::toPage(QPointF s) const
{
    return {float((s.x() - m_pan.x()) / m_zoom), float((s.y() - m_pan.y()) / m_zoom)};
}

QPointF Editor::toScreen(SkPoint p) const { return {p.x() * m_zoom + m_pan.x(), p.y() * m_zoom + m_pan.y()}; }

void Editor::setViewportSize(QSizeF s)
{
    if (s == m_viewport)
        return;
    m_viewport = s;
    requestUpdate();
}

void Editor::panBy(QPointF d)
{
    m_pan += d;
    emit viewChanged();
    requestUpdate();
}

void Editor::zoomAt(QPointF screen, double factor)
{
    const double z = std::clamp(m_zoom * factor, 0.02, 256.0);
    m_pan = screen - (screen - m_pan) * (z / m_zoom);
    m_zoom = z;
    emit viewChanged();
    requestUpdate();
}

void Editor::zoomTo(double z)
{
    QPointF c(m_viewport.width() / 2, m_viewport.height() / 2);
    zoomAt(c, z / m_zoom);
}

void Editor::zoomIn()
{
    zoomTo(m_zoom * 2);
}

void Editor::zoomOut()
{
    zoomTo(m_zoom / 2);
}

static void fitRect(SkRect r, double& zoom, QPointF& pan, QSizeF vp)
{
    if (r.isEmpty())
        return;
    const double margin = 48;
    double z = std::min((vp.width() - margin * 2) / r.width(), (vp.height() - margin * 2) / r.height());
    z = std::clamp(z, 0.02, 256.0);
    zoom = z;
    pan = QPointF(vp.width() / 2 - r.centerX() * z, vp.height() / 2 - r.centerY() * z);
}

void Editor::zoomToFit()
{
    SkRect r = SkRect::MakeEmpty();
    for (NodeId c : m_doc->at(m_doc->currentPage()).children)
        r.join(m_doc->visualBounds(c));
    if (r.isEmpty()) {
        m_zoom = 1;
        m_pan = QPointF(m_viewport.width() / 2, m_viewport.height() / 2);
    } else {
        fitRect(r, m_zoom, m_pan, m_viewport);
    }
    emit viewChanged();
    requestUpdate();
}

void Editor::zoomToSelection()
{
    if (m_doc->selection().empty())
        return zoomToFit();
    fitRect(selectionBounds(), m_zoom, m_pan, m_viewport);
    emit viewChanged();
    requestUpdate();
}

void Editor::setOutlines(bool on)
{
    auto o = m_renderer->options();
    o.outlines = on;
    m_renderer->setOptions(o);
    m_contentDirty = true;
    emit viewChanged();
    requestUpdate();
}

void Editor::setShowGrids(bool on)
{
    auto o = m_renderer->options();
    o.showGrids = on;
    m_renderer->setOptions(o);
    m_contentDirty = true;
    emit viewChanged();
    requestUpdate();
}

// ---------------------------------------------------------------- frames

FrameSnapshot Editor::snapshot()
{
    FrameSnapshot s;
    if (m_contentDirty || !m_content) {
        m_content = m_renderer->recordPage(m_doc->currentPage());
        m_contentDirty = false;
    }
    s.content = m_content;
    s.view = view();
    s.background = m_doc->at(m_doc->currentPage()).p.pageBackground;
    SkPictureRecorder rec;
    SkCanvas* c = rec.beginRecording(SkRect::MakeWH(float(m_viewport.width()), float(m_viewport.height())));
    drawOverlay(c);
    s.overlay = rec.finishRecordingAsPicture();
    return s;
}

void Editor::reportFrameTime(double ms)
{
    m_frameAccum += ms;
    if (++m_frameCount >= 30) {
        m_fps = m_frameAccum > 0 ? 1000.0 * m_frameCount / m_frameAccum : 0;
        m_frameAccum = 0;
        m_frameCount = 0;
        emit fpsChanged();
    }
}

void Editor::onDocChanged(bool structure)
{
    m_contentDirty = true;
    if (m_doc->libraryPtr() != m_lastLibrary) {
        m_lastLibrary = m_doc->libraryPtr();
        emit libraryChanged();
        emit librariesChanged();
    }
    m_layers->sync();
    if (structure) {
        emit componentsChanged();
        emit pagesChanged();
    }
    emit propsChanged();
    emit undoChanged();
    requestUpdate();
}

void Editor::onSelectionChanged()
{
    m_layers->sync();
    emit selectionChanged();
    emit propsChanged();
    requestUpdate();
}

// ---------------------------------------------------------------- input

void Editor::mousePress(const InputEvent& e)
{
    // Rulers create guides; existing guides drag with the move tool.
    bool vertical = false;
    const bool ruler = m_rulers && inRuler(e.screen, &vertical);
    const int guide = m_toolName == QLatin1String("select") ? guideAt(e.screen) : -1;
    if (e.button == Qt::LeftButton && (ruler || guide >= 0)) {
        m_guideDrag = {};
        m_guideDrag.active = true;
        m_guideDrag.index = ruler ? -1 : guide;
        // Dragging from the top ruler makes a horizontal guide, from the left ruler a vertical one.
        m_guideDrag.vertical = ruler ? vertical : m_doc->at(m_doc->currentPage()).p.guides[guide].vertical;
        m_guideDrag.pos = m_guideDrag.vertical ? e.page.x() : e.page.y();
        m_guideDrag.remove = ruler;
        requestUpdate();
        return;
    }
    m_tool->press(e);
    emit cursorChanged();
}

void Editor::mouseMove(const InputEvent& e)
{
    m_alt = e.mods & Qt::AltModifier;
    if (m_guideDrag.active) {
        bool v;
        m_guideDrag.pos = snapValue(m_guideDrag.vertical ? e.page.x() : e.page.y());
        m_guideDrag.remove = inRuler(e.screen, &v);
        requestUpdate();
        return;
    }
    m_tool->move(e);
    emit cursorChanged();
}

void Editor::mouseRelease(const InputEvent& e)
{
    if (m_guideDrag.active) {
        const GuideDrag g = m_guideDrag;
        m_guideDrag = {};
        const NodeId page = m_doc->currentPage();
        m_doc->breakMerge();
        m_doc->begin(g.index < 0 ? tr("Add guide") : g.remove ? tr("Remove guide") : tr("Move guide"));
        auto& guides = m_doc->edit(page).guides;
        if (g.index >= 0 && g.index < int(guides.size())) {
            if (g.remove)
                guides.erase(guides.begin() + g.index);
            else
                guides[g.index].pos = g.pos;
        } else if (!g.remove) {
            guides.push_back({g.vertical, g.pos});
        }
        m_doc->commit();
        requestUpdate();
        return;
    }
    m_tool->release(e);
    if (textEditing())
        emit propsChanged(); // the character selection drives the text inspector
    emit cursorChanged();
    requestUpdate();
}

void Editor::mouseDoubleClick(const InputEvent& e)
{
    m_tool->doubleClick(e);
    requestUpdate();
}

void Editor::hover(const InputEvent& e)
{
    const bool alt = e.mods & Qt::AltModifier;
    if (alt != m_alt) {
        m_alt = alt;
        requestUpdate();
    }
    const int g = m_toolName == QLatin1String("select") ? guideAt(e.screen) : -1;
    if (g != m_guideHover) {
        m_guideHover = g;
        emit cursorChanged();
    }
    m_tool->move(e);
    emit cursorChanged();
}

void Editor::leave()
{
    setHovered(kNoNode);
}

bool Editor::inputMethod(QInputMethodEvent* e)
{
    return m_tool->inputMethod(e);
}

bool Editor::keyRelease(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Alt) {
        m_alt = false;
        requestUpdate();
    }
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat() && !m_returnTool.isEmpty()) {
        const QString back = m_returnTool;
        m_returnTool.clear();
        setTool(back);
        return true;
    }
    return false;
}

bool Editor::keyPress(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Alt && !m_alt) {
        m_alt = true;
        requestUpdate();
    }
    if (m_tool->key(e)) {
        if (textEditing())
            emit propsChanged();
        return true;
    }
    if (textEditing())
        return false;
    const auto mods = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool ctrl = mods & (Qt::ControlModifier | Qt::MetaModifier);
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt = mods & Qt::AltModifier;
    const int k = e->key();

    if (k == Qt::Key_Space && !e->isAutoRepeat() && m_toolName != QLatin1String("hand")) {
        m_returnTool = m_toolName;
        setTool(QStringLiteral("hand"));
        return true;
    }
    if (ctrl) {
        switch (k) {
        case Qt::Key_Z: shift ? redo() : undo(); return true;
        case Qt::Key_C: copy(); return true;
        case Qt::Key_X: cut(); return true;
        case Qt::Key_V: shift ? pasteInPlace() : paste(); return true;
        case Qt::Key_D: duplicate(); return true;
        case Qt::Key_A:
            if (alt)
                selectSame(QStringLiteral("matching"));
            else
                selectAll();
            return true;
        case Qt::Key_G:
            if (alt)
                frameSelection();
            else if (shift)
                ungroup();
            else
                group();
            return true;
        case Qt::Key_K:
            if (alt) {
                createComponent();
                return true;
            }
            return false;
        case Qt::Key_B:
            if (alt) {
                detachInstance();
                return true;
            }
            return false;
        case Qt::Key_E: flatten(); return true;
        case Qt::Key_H: if (shift) { toggleVisible(); return true; } return false;
        case Qt::Key_L: if (shift) { toggleLocked(); return true; } return false;
        case Qt::Key_M: if (alt) { useAsMask(); return true; } return false;
        case Qt::Key_O: if (shift) { outlineStroke(); return true; } return false;
        case Qt::Key_Y: setOutlines(!outlines()); return true;
        case Qt::Key_R:
            if (selectionCount() == 1)
                emit requestRename(qint64(selected().front()));
            else if (selectionCount() > 1)
                emit requestBulkRename();
            return true;
        case Qt::Key_F: emit requestFind(); return true;
        case Qt::Key_BracketRight: shift ? bringToFront() : bringForward(); return true;
        case Qt::Key_BracketLeft: shift ? sendToBack() : sendBackward(); return true;
        case Qt::Key_Equal:
        case Qt::Key_Plus: zoomIn(); return true;
        case Qt::Key_Minus: zoomOut(); return true;
        case Qt::Key_0: zoomTo(1); return true;
        default: return false;
        }
    }
    if (alt) {
        if (shift && k == Qt::Key_H) { distribute(true); return true; }
        if (shift && k == Qt::Key_V) { distribute(false); return true; }
        switch (k) {
        case Qt::Key_A: align(QStringLiteral("left")); return true;
        case Qt::Key_D: align(QStringLiteral("right")); return true;
        case Qt::Key_W: align(QStringLiteral("top")); return true;
        case Qt::Key_S: align(QStringLiteral("bottom")); return true;
        case Qt::Key_H: align(QStringLiteral("hcenter")); return true;
        case Qt::Key_V: align(QStringLiteral("vcenter")); return true;
        default: break;
        }
    }
    const float step = shift ? 10 : 1;
    switch (k) {
    case Qt::Key_Left: nudge(-step, 0); return true;
    case Qt::Key_Right: nudge(step, 0); return true;
    case Qt::Key_Up: nudge(0, -step); return true;
    case Qt::Key_Down: nudge(0, step); return true;
    case Qt::Key_Delete:
    case Qt::Key_Backspace: deleteSelection(); return true;
    case Qt::Key_Escape:
        if (m_toolName != QLatin1String("select"))
            setTool(QStringLiteral("select"));
        else if (!m_doc->selection().empty())
            selectParent();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        if (shift) {
            selectParent();
            return true;
        }
        auto sel = selected();
        if (sel.size() == 1) {
            const Node& n = m_doc->at(sel[0]);
            if (n.type() == NodeType::Text) {
                enterTextEdit(n.id, {0, 0}, true);
                return true;
            }
            if (n.type() == NodeType::Vector) {
                enterVectorEdit(n.id);
                return true;
            }
        }
        selectChildren();
        return true;
    }
    case Qt::Key_Tab:
    case Qt::Key_Backtab: {
        auto sel = selected();
        if (sel.size() != 1)
            return true;
        const Node& n = m_doc->at(sel[0]);
        const auto& sib = m_doc->at(n.parent).children;
        int i = m_doc->indexOf(n.id);
        int dir = (k == Qt::Key_Backtab || shift) ? 1 : -1; // layer list shows top first
        int j = (i + dir + int(sib.size())) % int(sib.size());
        select({sib[j]});
        return true;
    }
    default: break;
    }
    if (shift) {
        switch (k) {
        case Qt::Key_ParenRight:
        case Qt::Key_0: zoomTo(1); return true;
        case Qt::Key_Exclam:
        case Qt::Key_1: zoomToFit(); return true;
        case Qt::Key_At:
        case Qt::Key_2: zoomToSelection(); return true;
        case Qt::Key_A: addAutoLayout(); return true;
        case Qt::Key_H: flip(true); return true;
        case Qt::Key_V: flip(false); return true;
        case Qt::Key_L: setTool(QStringLiteral("arrow")); return true;
        case Qt::Key_P: setTool(QStringLiteral("pencil")); return true;
        case Qt::Key_R: setRulers(!m_rulers); return true;
        case Qt::Key_S: setTool(QStringLiteral("section")); return true;
        default: break;
        }
    }
    if (!mods || mods == Qt::KeypadModifier) {
        switch (k) {
        case Qt::Key_V: setTool(QStringLiteral("select")); return true;
        case Qt::Key_H: setTool(QStringLiteral("hand")); return true;
        case Qt::Key_F:
        case Qt::Key_A: setTool(QStringLiteral("frame")); return true;
        case Qt::Key_R: setTool(QStringLiteral("rect")); return true;
        case Qt::Key_O: setTool(QStringLiteral("ellipse")); return true;
        case Qt::Key_L: setTool(QStringLiteral("line")); return true;
        case Qt::Key_P: setTool(QStringLiteral("pen")); return true;
        case Qt::Key_T: setTool(QStringLiteral("text")); return true;
        case Qt::Key_K: setTool(QStringLiteral("scale")); return true;
        case Qt::Key_I: pickColor(); m_pickForPopup = false; return true;
        default: break;
        }
        if (k >= Qt::Key_0 && k <= Qt::Key_9 && !m_doc->selection().empty()) {
            const int d = k - Qt::Key_0;
            setProp(QStringLiteral("opacity"), d == 0 ? 1.0 : d / 10.0);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- tools

QString Editor::toolName() const { return m_toolName; }

void Editor::setTool(const QString& name)
{
    auto it = m_tools.find(name);
    if (it == m_tools.end() || it->second.get() == m_tool)
        return;
    if (m_tool)
        m_tool->deactivate();
    m_tool = it->second.get();
    m_toolName = name;
    m_tool->activate();
    guides.clear();
    marquee.setEmpty();
    emit toolChanged();
    emit cursorChanged();
    requestUpdate();
}

void Editor::enterTextEdit(NodeId id, SkPoint pagePos, bool selectAll, bool created)
{
    auto* t = static_cast<TextEditTool*>(m_tools["textEdit"].get());
    if (m_tool == t)
        setTool(QStringLiteral("select"));
    setTool(QStringLiteral("textEdit"));
    t->begin(id, pagePos, selectAll, created);
}

bool Editor::selectionQuad(SkPoint quad[4]) const
{
    const auto& sel = m_doc->selection();
    if (sel.empty())
        return false;
    const SkMatrix v = view();
    if (sel.size() == 1) {
        SkMatrix m = SkMatrix::Concat(v, m_doc->worldTransform(sel[0]));
        SkRect r = m_doc->at(sel[0]).localRect();
        quad[0] = m.mapXY(r.left(), r.top());
        quad[1] = m.mapXY(r.right(), r.top());
        quad[2] = m.mapXY(r.right(), r.bottom());
        quad[3] = m.mapXY(r.left(), r.bottom());
    } else {
        v.mapRect(selectionBounds()).toQuad(quad);
    }
    return true;
}

void Editor::enterVectorEdit(NodeId id)
{
    auto* t = static_cast<VectorEditTool*>(m_tools["vectorEdit"].get());
    setTool(QStringLiteral("vectorEdit"));
    t->begin(id);
}

bool Editor::textEditing() const { return m_toolName == QLatin1String("textEdit"); }

int Editor::cursorShape() const
{
    const auto& guides = m_doc->at(m_doc->currentPage()).p.guides;
    const int g = m_guideDrag.active ? m_guideDrag.index : m_guideHover;
    if (m_guideDrag.active && g < 0)
        return int(m_guideDrag.vertical ? Qt::SplitHCursor : Qt::SplitVCursor);
    if (g >= 0 && g < int(guides.size()))
        return int(guides[g].vertical ? Qt::SplitHCursor : Qt::SplitVCursor);
    return int(m_tool->cursor());
}

// ---------------------------------------------------------------- hit testing

namespace {

NodeId hitRec(const Document& d, NodeId id, SkPoint pt, float tol)
{
    const Node& n = d.at(id);
    if (!n.p.visible || n.p.locked)
        return kNoNode;
    SkMatrix inv;
    if (!n.p.transform.invert(&inv))
        return kNoNode;
    const SkPoint lp = inv.mapPoint(pt);
    SkRect box = n.localRect();
    SkRect tolBox = box.makeOutset(tol, tol);
    const NodeType t = n.type();
    const bool frame = isFrameLike(t);
    if (frame && n.p.clipsContent && !tolBox.contains(lp.x(), lp.y()))
        return kNoNode;
    if (t != NodeType::BooleanOp && !n.children.empty()) {
        for (auto it = n.children.rbegin(); it != n.children.rend(); ++it)
            if (NodeId h = hitRec(d, *it, lp, tol))
                return h;
    }
    switch (t) {
    case NodeType::Group: return kNoNode;
    case NodeType::Text: return tolBox.contains(lp.x(), lp.y()) ? id : kNoNode;
    default: break;
    }
    if (frame)
        return box.contains(lp.x(), lp.y()) ? id : kNoNode;
    if (!tolBox.contains(lp.x(), lp.y()))
        return kNoNode;
    SkPath path = fillPath(d, id);
    const bool open = t == NodeType::Line || !path.isLastContourClosed();
    if (!open && path.contains(lp.x(), lp.y()))
        return id;
    SkPaint sp;
    sp.setStyle(SkPaint::kStroke_Style);
    sp.setStrokeWidth(std::max(n.p.strokes.empty() ? 0.f : n.p.stroke.weight, 0.f) + tol * 2);
    SkPath outline;
    skpathutils::FillPathWithPaint(t == NodeType::Vector ? strokePath(d, id) : path, sp, &outline);
    return outline.contains(lp.x(), lp.y()) ? id : kNoNode;
}

} // namespace

NodeId Editor::hitDeepest(SkPoint page, float tolerance) const
{
    const float tol = tolerance < 0 ? float(3 / m_zoom) : tolerance;
    const auto& kids = m_doc->at(m_doc->currentPage()).children;
    for (auto it = kids.rbegin(); it != kids.rend(); ++it)
        if (NodeId h = hitRec(*m_doc, *it, page, tol))
            return h;
    return kNoNode;
}

bool Editor::isTopLevelContainer(NodeId id) const
{
    const Node& n = m_doc->at(id);
    if (n.type() == NodeType::ComponentSet || n.type() == NodeType::Section)
        return true;
    // Frames directly on the page or in a section behave as top-level.
    const NodeType pt = m_doc->at(n.parent).type();
    if (pt != NodeType::Page && pt != NodeType::Section)
        return false;
    return n.type() == NodeType::Frame || n.type() == NodeType::Component;
}

NodeId Editor::selectableAt(SkPoint page, bool deep) const
{
    NodeId d = hitDeepest(page);
    if (!d)
        return kNoNode;
    if (deep)
        return d;
    std::vector<NodeId> chain; // top-level first
    for (NodeId c = d; c && m_doc->at(c).type() != NodeType::Page; c = m_doc->at(c).parent)
        chain.insert(chain.begin(), c);
    // Siblings of the current selection stay reachable once the user has drilled in.
    std::unordered_set<NodeId> selParents;
    for (NodeId s : m_doc->selection())
        selParents.insert(m_doc->at(s).parent);
    for (NodeId c : chain)
        if (selParents.count(m_doc->at(c).parent) && m_doc->at(m_doc->at(c).parent).type() != NodeType::Page)
            return c;
    for (size_t i = 0; i < chain.size(); ++i) {
        NodeId c = chain[i];
        if (isTopLevelContainer(c)) {
            if (i + 1 < chain.size())
                continue;
            // Background of a top-level frame: select it only when it's empty.
            return m_doc->at(c).children.empty() ? c : kNoNode;
        }
        return c;
    }
    return kNoNode;
}

NodeId Editor::frameLabelAt(QPointF screen) const
{
    const auto frames = labelledFrames();
    for (auto it = frames.rbegin(); it != frames.rend(); ++it)
        if (frameLabelRect(*it).contains(float(screen.x()), float(screen.y())))
            return *it;
    return kNoNode;
}

NodeId Editor::containerAt(SkPoint page, const std::vector<NodeId>& exclude) const
{
    std::function<NodeId(NodeId, SkPoint)> rec = [&](NodeId id, SkPoint pt) -> NodeId {
        const Node& n = m_doc->at(id);
        if (!n.p.visible)
            return kNoNode;
        for (NodeId x : exclude)
            if (x == id)
                return kNoNode;
        SkMatrix inv;
        if (!n.p.transform.invert(&inv))
            return kNoNode;
        SkPoint lp = inv.mapPoint(pt);
        const NodeType t = n.type();
        if (t == NodeType::Instance)
            return kNoNode;
        const bool inside = n.localRect().contains(lp.x(), lp.y());
        if (isFrameLike(t) && !inside)
            return kNoNode;
        if (isFrameLike(t) || t == NodeType::Group) {
            for (auto it = n.children.rbegin(); it != n.children.rend(); ++it)
                if (NodeId h = rec(*it, lp))
                    return h;
        }
        return isFrameLike(t) && inside && !n.p.locked ? id : kNoNode;
    };
    const auto& kids = m_doc->at(m_doc->currentPage()).children;
    for (auto it = kids.rbegin(); it != kids.rend(); ++it)
        if (NodeId h = rec(*it, page))
            return h;
    return m_doc->currentPage();
}

NodeId Editor::parentForNewNode(SkPoint page) const { return containerAt(page, {}); }

void Editor::setHovered(NodeId id)
{
    if (id == m_hover)
        return;
    m_hover = id;
    requestUpdate();
}

// ---------------------------------------------------------------- selection

void Editor::select(std::vector<NodeId> ids)
{
    m_doc->setSelection(std::move(ids));
}

SkRect Editor::selectionBounds() const
{
    SkRect r = SkRect::MakeEmpty();
    bool first = true;
    for (NodeId id : m_doc->selection()) {
        SkRect b = m_doc->worldBounds(id);
        if (first) {
            r = b;
            first = false;
        } else {
            r.join(b);
        }
    }
    return r;
}

std::vector<NodeId> Editor::topLevelSelection() const
{
    std::vector<NodeId> out;
    const auto& sel = m_doc->selection();
    for (NodeId id : sel) {
        bool nested = false;
        for (NodeId o : sel)
            if (o != id && m_doc->isAncestor(o, id))
                nested = true;
        if (!nested)
            out.push_back(id);
    }
    return out;
}

void Editor::selectAll()
{
    auto sel = selected();
    NodeId parent = sel.empty() ? m_doc->currentPage() : m_doc->at(sel.front()).parent;
    std::vector<NodeId> ids;
    for (NodeId c : m_doc->at(parent).children)
        if (m_doc->at(c).p.visible && !m_doc->at(c).p.locked)
            ids.push_back(c);
    select(ids);
}

void Editor::selectNone() { select({}); }

void Editor::selectChildren()
{
    std::vector<NodeId> ids;
    for (NodeId s : selected())
        for (NodeId c : m_doc->at(s).children)
            ids.push_back(c);
    if (!ids.empty())
        select(ids);
}

void Editor::selectParent()
{
    std::vector<NodeId> ids;
    for (NodeId s : selected()) {
        NodeId p = m_doc->at(s).parent;
        if (p && m_doc->at(p).type() != NodeType::Page && std::find(ids.begin(), ids.end(), p) == ids.end())
            ids.push_back(p);
    }
    select(ids);
}

void Editor::selectNode(qint64 id, bool additive)
{
    if (!m_doc->isAttached(NodeId(id)))
        return;
    if (textEditing() || m_toolName == QLatin1String("vectorEdit"))
        setTool(QStringLiteral("select"));
    if (!additive) {
        select({NodeId(id)});
        return;
    }
    auto sel = selected();
    auto it = std::find(sel.begin(), sel.end(), NodeId(id));
    if (it != sel.end())
        sel.erase(it);
    else
        sel.push_back(NodeId(id));
    select(sel);
}

void Editor::selectRange(qint64 id)
{
    auto sel = selected();
    if (sel.empty())
        return selectNode(id, false);
    NodeId a = sel.back(), b = NodeId(id);
    if (m_doc->at(a).parent != m_doc->at(b).parent)
        return selectNode(id, true);
    const auto& kids = m_doc->at(m_doc->at(a).parent).children;
    int i = m_doc->indexOf(a), j = m_doc->indexOf(b);
    if (i > j)
        std::swap(i, j);
    for (int k = i; k <= j; ++k)
        if (std::find(sel.begin(), sel.end(), kids[k]) == sel.end())
            sel.push_back(kids[k]);
    select(sel);
}

// ---------------------------------------------------------------- overlay

std::vector<NodeId> Editor::labelledFrames() const
{
    // Top-level frames, sections, and frames directly inside sections.
    std::vector<NodeId> out;
    for (NodeId id : m_doc->at(m_doc->currentPage()).children) {
        const Node& n = m_doc->at(id);
        if (!isFrameLike(n.type()) || !n.p.visible)
            continue;
        out.push_back(id);
        if (n.type() == NodeType::Section)
            for (NodeId c : n.children)
                if (isFrameLike(m_doc->at(c).type()) && m_doc->at(c).p.visible)
                    out.push_back(c);
    }
    return out;
}

SkRect Editor::frameLabelRect(NodeId id) const
{
    const SkFont font = text::uiFont(11, m_doc->at(id).type() == NodeType::Section);
    const SkRect b = view().mapRect(m_doc->worldBounds(id));
    const QByteArray name = m_doc->at(id).p.name.toUtf8();
    const float w = font.measureText(name.constData(), name.size(), SkTextEncoding::kUTF8);
    if (m_doc->at(id).type() == NodeType::Section) // pill inside the top-left corner
        return SkRect::MakeXYWH(b.left() + 6, b.top() + 6, std::min(w + 14, std::max(b.width() - 12, 20.f)), 20);
    return SkRect::MakeXYWH(b.left(), b.top() - 20, std::max(std::min(w, b.width()), 20.f), 18);
}

void Editor::drawFrameLabels(SkCanvas* c)
{
    SkPaint tp;
    tp.setAntiAlias(true);
    const auto& sel = m_doc->selection();
    for (NodeId id : labelledFrames()) {
        const Node& n = m_doc->at(id);
        SkRect b = view().mapRect(m_doc->worldBounds(id));
        if (b.right() < 0 || b.left() > m_viewport.width() || b.bottom() < 0 || b.top() > m_viewport.height() + 20)
            continue;
        const bool isSel = std::find(sel.begin(), sel.end(), id) != sel.end() || m_hover == id;
        const bool comp = n.type() == NodeType::Component || n.type() == NodeType::ComponentSet
            || n.type() == NodeType::Instance;
        const QByteArray utf8 = n.p.name.toUtf8();
        const SkRect label = frameLabelRect(id);
        c->save();
        c->clipRect(label);
        if (n.type() == NodeType::Section) {
            SkPaint bg;
            bg.setAntiAlias(true);
            bg.setColor(isSel ? kBlue : SkColorSetARGB(0xE6, 0x55, 0x55, 0x55));
            c->drawRRect(SkRRect::MakeRectXY(label, 4, 4), bg);
            tp.setColor(SK_ColorWHITE);
            c->drawSimpleText(utf8.constData(), utf8.size(), SkTextEncoding::kUTF8, label.left() + 7, label.top() + 14,
                text::uiFont(11, true), tp);
        } else {
            tp.setColor(comp ? kPurple : isSel ? kBlue : SkColorSetRGB(0xA0, 0xA0, 0xA0));
            c->drawSimpleText(utf8.constData(), utf8.size(), SkTextEncoding::kUTF8, b.left(), b.top() - 6, text::uiFont(11), tp);
        }
        c->restore();
    }
}

void Editor::drawSelection(SkCanvas* c)
{
    const auto& sel = m_doc->selection();
    if (sel.empty())
        return;
    const SkMatrix v = view();
    SkPaint line;
    line.setAntiAlias(true);
    line.setStyle(SkPaint::kStroke_Style);
    line.setStrokeWidth(1);
    bool anyComp = false;
    for (NodeId id : sel) {
        const bool comp = isComponentish(*m_doc, id);
        anyComp |= comp;
        line.setColor(comp ? kPurple : kBlue);
        SkMatrix m = SkMatrix::Concat(v, m_doc->worldTransform(id));
        const Node& n = m_doc->at(id);
        SkPath p = (n.type() == NodeType::Text || isFrameLike(n.type()) || n.type() == NodeType::Group)
            ? SkPath::Rect(n.localRect())
            : n.type() == NodeType::Vector ? strokePath(*m_doc, id)
                                           : fillPath(*m_doc, id);
        c->drawPath(p.makeTransform(m), line);
    }
    if (!showSizeLabel)
        return;
    // Selection frame and handles.
    SkPoint quad[4];
    selectionQuad(quad);
    const SkColor color = anyComp ? kPurple : kBlue;
    line.setColor(color);
    SkPath box;
    box.addPoly(quad, 4, true);
    c->drawPath(box, line);
    SkPaint hf, hs;
    hf.setColor(SK_ColorWHITE);
    hf.setAntiAlias(true);
    hs = line;
    const bool isLine = sel.size() == 1 && m_doc->at(sel[0]).type() == NodeType::Line;
    for (int i = 0; i < 4; ++i) {
        if (isLine && (i == 2 || i == 3))
            continue;
        SkRect h = SkRect::MakeXYWH(quad[i].x() - 4, quad[i].y() - 4, 8, 8);
        c->drawRect(h, hf);
        c->drawRect(h, hs);
    }
    // Size label under the bottom-most edge.
    SkRect aabb;
    aabb.setBounds(quad, 4);
    float w = 0, h = 0;
    if (sel.size() == 1) {
        w = m_doc->at(sel[0]).p.width;
        h = m_doc->at(sel[0]).p.height;
    } else {
        SkRect b = selectionBounds();
        w = b.width();
        h = b.height();
    }
    auto fmt = [](float v) {
        return std::fabs(v - std::round(v)) < 0.01f ? QString::number(int(std::round(v))) : QString::number(v, 'f', 1);
    };
    QString label = fmt(w) + QString::fromUtf8(" × ") + fmt(h);
    if (sel.size() == 1) {
        const Node& n = m_doc->at(sel[0]);
        if (n.p.layout.mode != LayoutMode::None) {
            if (n.p.layoutChild.sizingH == LayoutSizing::Hug && n.p.layoutChild.sizingV == LayoutSizing::Hug)
                label += QStringLiteral("  Hug");
        }
    }
    const QByteArray utf8 = label.toUtf8();
    const SkFont font = text::uiFont(11, true);
    const float tw = font.measureText(utf8.constData(), utf8.size(), SkTextEncoding::kUTF8);
    SkRect pill = SkRect::MakeXYWH(aabb.centerX() - tw / 2 - 5, aabb.bottom() + 8, tw + 10, 17);
    SkPaint pp;
    pp.setAntiAlias(true);
    pp.setColor(color);
    c->drawRRect(SkRRect::MakeRectXY(pill, 3, 3), pp);
    pp.setColor(SK_ColorWHITE);
    c->drawSimpleText(utf8.constData(), utf8.size(), SkTextEncoding::kUTF8, pill.left() + 5, pill.top() + 12.5f, font, pp);
}

void Editor::drawOverlay(SkCanvas* c)
{
    const SkMatrix v = view();
    drawPageGuides(c);
    drawFrameLabels(c);

    const auto& sel = m_doc->selection();
    if (m_hover && m_doc->isAttached(m_hover) && std::find(sel.begin(), sel.end(), m_hover) == sel.end()) {
        SkPaint hp;
        hp.setAntiAlias(true);
        hp.setStyle(SkPaint::kStroke_Style);
        hp.setStrokeWidth(1.5f);
        hp.setColor(isComponentish(*m_doc, m_hover) ? kPurple : kBlue);
        const Node& n = m_doc->at(m_hover);
        SkMatrix m = SkMatrix::Concat(v, m_doc->worldTransform(m_hover));
        SkPath p = (n.type() == NodeType::Text || isFrameLike(n.type()) || n.type() == NodeType::Group)
            ? SkPath::Rect(n.localRect())
            : n.type() == NodeType::Vector ? strokePath(*m_doc, m_hover)
                                           : fillPath(*m_doc, m_hover);
        c->drawPath(p.makeTransform(m), hp);
    }
    if (dropTarget && m_doc->isAttached(dropTarget) && m_doc->at(dropTarget).type() != NodeType::Page) {
        SkPaint dp;
        dp.setAntiAlias(true);
        dp.setStyle(SkPaint::kStroke_Style);
        dp.setStrokeWidth(2);
        dp.setColor(kBlue);
        SkMatrix m = SkMatrix::Concat(v, m_doc->worldTransform(dropTarget));
        c->drawPath(SkPath::Rect(m_doc->at(dropTarget).localRect()).makeTransform(m), dp);
    }

    if (!textEditing() && m_toolName != QLatin1String("vectorEdit") && m_toolName != QLatin1String("pen"))
        drawSelection(c);

    if (!marquee.isEmpty()) {
        SkRect r = v.mapRect(marquee);
        SkPaint mp;
        mp.setColor(SkColorSetARGB(25, 0x0D, 0x99, 0xFF));
        c->drawRect(r, mp);
        mp.setStyle(SkPaint::kStroke_Style);
        mp.setColor(kBlue);
        mp.setStrokeWidth(1);
        c->drawRect(r, mp);
    }
    if (!guides.empty()) {
        SkPaint gp;
        gp.setAntiAlias(true);
        gp.setColor(kGuide);
        gp.setStrokeWidth(1);
        for (const SnapGuide& g : guides) {
            if (g.vertical) {
                QPointF a = toScreen({g.pos, g.from}), b = toScreen({g.pos, g.to});
                c->drawLine(std::round(a.x()) + 0.5f, a.y(), std::round(b.x()) + 0.5f, b.y(), gp);
            } else {
                QPointF a = toScreen({g.from, g.pos}), b = toScreen({g.to, g.pos});
                c->drawLine(a.x(), std::round(a.y()) + 0.5f, b.x(), std::round(b.y()) + 0.5f, gp);
            }
        }
    }
    m_tool->drawOverlay(c);
    if (m_alt)
        drawMeasurements(c);
    if (m_rulers)
        drawRulers(c);
}

// ---------------------------------------------------------------- creation helpers

Props Editor::defaultProps(NodeType t) const
{
    Props p;
    p.type = t;
    static const QHash<int, QString> names = {
        {int(NodeType::Frame), QStringLiteral("Frame")},
        {int(NodeType::Group), QStringLiteral("Group")},
        {int(NodeType::Rectangle), QStringLiteral("Rectangle")},
        {int(NodeType::Ellipse), QStringLiteral("Ellipse")},
        {int(NodeType::Polygon), QStringLiteral("Polygon")},
        {int(NodeType::Star), QStringLiteral("Star")},
        {int(NodeType::Line), QStringLiteral("Line")},
        {int(NodeType::Vector), QStringLiteral("Vector")},
        {int(NodeType::Text), QStringLiteral("Text")},
        {int(NodeType::Component), QStringLiteral("Component")},
        {int(NodeType::BooleanOp), QStringLiteral("Boolean")},
        {int(NodeType::Section), QStringLiteral("Section")},
    };
    // Number new layers per type like "Rectangle 3".
    int count = 0;
    m_doc->walk(m_doc->currentPage(), [&](const Node& n) {
        count += n.type() == t;
        return true;
    });
    p.name = names.value(int(t), QStringLiteral("Layer")) + QLatin1Char(' ') + QString::number(count + 1);
    const SkColor4f gray = {0xD9 / 255.f, 0xD9 / 255.f, 0xD9 / 255.f, 1};
    switch (t) {
    case NodeType::Frame:
        p.fills = {Paint::solid(SkColors::kWhite)};
        break;
    case NodeType::Section:
        p.fills = {Paint::solid({0.27f, 0.27f, 0.27f, 1})};
        p.corners = {2, 2, 2, 2};
        p.clipsContent = false;
        break;
    case NodeType::Rectangle:
    case NodeType::Ellipse:
    case NodeType::Polygon:
    case NodeType::Star:
        p.fills = {Paint::solid(gray)};
        break;
    case NodeType::Line:
    case NodeType::Vector:
        p.strokes = {Paint::solid(SkColors::kBlack)};
        p.stroke.align = StrokeAlign::Center;
        break;
    case NodeType::Text:
        p.fills = {Paint::solid(SkColors::kBlack)};
        p.textStyle.size = 14;
        p.textStyle.autoResize = TextAutoResize::WidthAndHeight;
        p.name = QStringLiteral("Text");
        break;
    default: break;
    }
    if (t == NodeType::Polygon)
        p.pointCount = 3;
    return p;
}

void Editor::reparentKeepWorld(NodeId id, NodeId newParent, int index)
{
    SkMatrix world = m_doc->worldTransform(id);
    m_doc->move(id, newParent, index);
    m_doc->setWorldTransform(id, world);
}

void Editor::setStatus(const QString& s)
{
    m_status = s;
    emit statusChanged();
}

// ---------------------------------------------------------------- files

QString Editor::fileName() const
{
    return m_fileUrl.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(m_fileUrl.toLocalFile()).completeBaseName();
}

void Editor::newDocument()
{
    if (!m_doc->isDirty())
        QFile::remove(recoveryPath());
    newSession();
    setTool(QStringLiteral("select"));
    m_doc->clear();
    m_renderer->clear();
    m_fileUrl.clear();
    m_contentDirty = true;
    m_zoom = 1;
    m_pan = QPointF(m_viewport.width() / 2 - 400, m_viewport.height() / 2 - 300);
    emit fileChanged();
    emit viewChanged();
    emit componentsChanged();
    emit pagesChanged();
    m_layers->sync();
    requestUpdate();
}

bool Editor::open(const QUrl& url)
{
    QFile f(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(tr("Cannot open %1").arg(f.fileName()));
        return false;
    }
    QByteArray data = f.readAll();
    if (!data.startsWith("DRAV")) {
        newDocument();
        return importFile(url);
    }
    setTool(QStringLiteral("select"));
    m_renderer->clear();
    QString err;
    if (!io::load(*m_doc, data, &err)) {
        setStatus(err);
        return false;
    }
    if (!m_doc->isDirty())
        QFile::remove(recoveryPath());
    newSession();
    m_fileUrl = QUrl::fromLocalFile(f.fileName());
    addRecent(f.fileName());
    m_contentDirty = true;
    emit fileChanged();
    emit componentsChanged();
    m_layers->sync();
    zoomToFit();
    setStatus(tr("Opened %1").arg(fileName()));
    return true;
}

bool Editor::save()
{
    if (m_fileUrl.isEmpty())
        return false;
    return saveAs(m_fileUrl);
}

bool Editor::saveAs(const QUrl& url)
{
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!path.endsWith(QLatin1String(".drav")))
        path += QStringLiteral(".drav");
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        setStatus(tr("Cannot write %1").arg(path));
        return false;
    }
    f.write(io::save(*m_doc));
    if (!f.commit()) {
        setStatus(tr("Saving failed"));
        return false;
    }
    m_doc->markSaved();
    m_fileUrl = QUrl::fromLocalFile(path);
    QFile::remove(recoveryPath());
    QFile::remove(recoveryPath() + QStringLiteral(".json"));
    addRecent(path);
    // Keep an automatic version at most every 30 minutes.
    const QVariantList vs = versions();
    if (vs.isEmpty() || vs.front().toMap().value(QStringLiteral("time")).toDateTime().secsTo(QDateTime::currentDateTime()) > 1800)
        saveVersion(tr("Autosave"));
    emit fileChanged();
    setStatus(tr("Saved %1").arg(fileName()));
    return true;
}

bool Editor::importFile(const QUrl& url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = f.readAll();
    const QString ext = QFileInfo(path).suffix().toLower();
    SkPoint center = toPage(QPointF(m_viewport.width() / 2, m_viewport.height() / 2));
    m_doc->begin(tr("Import"));
    std::vector<NodeId> created;
    if (ext == QLatin1String("svg")) {
        NodeId id = svg::import(*m_doc, data, m_doc->currentPage(), QFileInfo(path).completeBaseName());
        if (id)
            created.push_back(id);
    } else {
        QImageReader reader(path);
        QSize size = reader.size();
        if (size.isValid()) {
            QString hash = m_doc->addImage(data);
            Props p = defaultProps(NodeType::Rectangle);
            p.name = QFileInfo(path).completeBaseName();
            Paint img;
            img.type = PaintType::Image;
            img.imageHash = hash;
            p.fills = {img};
            NodeId id = m_doc->create(p, m_doc->currentPage());
            m_doc->resize(id, size.width(), size.height());
            created.push_back(id);
        }
    }
    if (created.empty()) {
        m_doc->cancel();
        setStatus(tr("Unsupported file: %1").arg(path));
        return false;
    }
    placeNodes(created, center);
    select(created);
    m_doc->commit();
    return true;
}

QString Editor::addImageFile(const QUrl& url)
{
    QFile f(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return m_doc->addImage(f.readAll());
}

void Editor::placeNodes(const std::vector<NodeId>& ids, SkPoint center)
{
    SkRect b = SkRect::MakeEmpty();
    for (NodeId id : ids)
        b.join(m_doc->worldBounds(id));
    const float dx = snapValue(center.x() - b.centerX()), dy = snapValue(center.y() - b.centerY());
    for (NodeId id : ids) {
        SkMatrix w = m_doc->worldTransform(id);
        w.postTranslate(dx, dy);
        m_doc->setWorldTransform(id, w);
    }
}

QString Editor::suggestedExportName(const QString& format) const
{
    QString base = fileName();
    auto sel = selected();
    if (sel.size() == 1)
        base = m_doc->at(sel[0]).p.name;
    return base + QLatin1Char('.') + exporter::extension(exporter::formatFromName(format));
}

bool Editor::exportSelection(const QUrl& url, const QString& format, double scale)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const auto fmt = exporter::formatFromName(format);
    auto sel = topLevelSelection();
    if (sel.empty())
        sel = {m_doc->currentPage()};
    QByteArray bytes;
    if (fmt == exporter::Format::PDF)
        bytes = exporter::exportPdf(*m_doc, sel);
    if (fmt != exporter::Format::PDF && sel.size() > 1) {
        QFileInfo fi(path);
        int ok = 0;
        for (NodeId id : sel) {
            QString name = m_doc->at(id).p.name;
            name.replace(QLatin1Char('/'), QLatin1Char('-'));
            QFile f(fi.absolutePath() + QLatin1Char('/') + name + QLatin1Char('.') + exporter::extension(fmt));
            QByteArray b = exporter::exportNode(*m_doc, id, fmt, float(scale));
            if (!b.isEmpty() && f.open(QIODevice::WriteOnly) && f.write(b) == b.size())
                ++ok;
        }
        setStatus(tr("Exported %1 layers").arg(ok));
        return ok > 0;
    }
    if (bytes.isEmpty())
        bytes = exporter::exportNode(*m_doc, sel.front(), fmt, float(scale));
    QSaveFile f(path);
    if (bytes.isEmpty() || !f.open(QIODevice::WriteOnly)) {
        setStatus(tr("Export failed"));
        return false;
    }
    f.write(bytes);
    const bool ok = f.commit();
    setStatus(ok ? tr("Exported %1").arg(QFileInfo(path).fileName()) : tr("Export failed"));
    return ok;
}

} // namespace drav
