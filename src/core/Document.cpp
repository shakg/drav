#include "Document.h"

#include "Layout.h"

#include <QCryptographicHash>

#include <algorithm>
#include <cassert>

namespace drav {

Document::Document(QObject* parent)
    : QObject(parent)
{
    clear();
}

Document::~Document() = default;

void Document::clearForLoad()
{
    m_nodes.clear();
    m_undo.clear();
    m_redo.clear();
    m_txn.reset();
    m_depth = 0;
    m_selection.clear();
    m_images.clear();
    m_dirty.clear();
    m_userEdited.clear();
    m_nextId = 0;
    m_root = kNoNode;
}

void Document::clear()
{
    m_nodes.clear();
    m_undo.clear();
    m_redo.clear();
    m_txn.reset();
    m_depth = 0;
    m_selection.clear();
    m_images.clear();
    m_dirty.clear();
    m_userEdited.clear();
    m_nextId = 0;

    Props rp;
    rp.type = NodeType::Document;
    rp.name = QStringLiteral("Document");
    m_root = rawCreate(allocId(), rp);
    Props pp;
    pp.type = NodeType::Page;
    pp.name = QStringLiteral("Page 1");
    NodeId page = rawCreate(allocId(), pp);
    rawLink(page, m_root);
    finishLoad();
    m_currentPage = page;
    emit pageChanged();
    emit changed(true);
}

const Node* Document::find(NodeId id) const
{
    auto it = m_nodes.find(id);
    return it == m_nodes.end() ? nullptr : it->second.get();
}

bool Document::isAttached(NodeId id) const
{
    const Node* n = find(id);
    return n && n->attached;
}

void Document::setCurrentPage(NodeId page)
{
    if (page == m_currentPage || !exists(page) || at(page).type() != NodeType::Page)
        return;
    m_currentPage = page;
    m_selection.clear();
    emit pageChanged();
    emit selectionChanged();
    emit changed(true);
}

// ---------------------------------------------------------------- transactions

void Document::begin(const QString& label)
{
    if (m_depth++ == 0) {
        m_txn = std::make_unique<Txn>();
        m_txn->label = label;
        m_txn->selBefore = m_selection;
        m_txn->pageBefore = m_currentPage;
    }
}

void Document::commit()
{
    assert(m_depth > 0);
    if (--m_depth > 0)
        return;
    updateDerived();
    auto txn = std::move(m_txn);
    for (auto& [id, before] : txn->before) {
        const Node* n = find(id);
        if (n && !(n->p == before))
            txn->after.emplace(id, n->p);
    }
    // Drop props entries that did not change.
    for (auto it = txn->before.begin(); it != txn->before.end();) {
        if (!txn->after.count(it->first))
            it = txn->before.erase(it);
        else
            ++it;
    }
    txn->selAfter = m_selection;
    txn->pageAfter = m_currentPage;
    const QString mergeKey = txn->mergeKey;
    const bool empty = txn->before.empty() && txn->ops.empty();
    const bool merge = !empty && !txn->mergeKey.isEmpty() && txn->mergeKey == m_lastMergeKey && !m_undo.empty()
        && m_savedUndoDepth != (int)m_undo.size();
    if (merge) {
        Txn& prev = *m_undo.back();
        for (auto& [id, before] : txn->before)
            if (!prev.before.count(id) && !prev.created.count(id))
                prev.before.emplace(id, std::move(before));
        prev.created.insert(txn->created.begin(), txn->created.end());
        prev.ops.insert(prev.ops.end(), txn->ops.begin(), txn->ops.end());
        prev.after.clear();
        for (auto& [id, before] : prev.before)
            prev.after[id] = at(id).p;
        prev.selAfter = txn->selAfter;
        prev.pageAfter = txn->pageAfter;
        m_redo.clear();
        emit undoStateChanged();
    } else if (!empty) {
        if (m_savedUndoDepth > (int)m_undo.size())
            m_savedUndoDepth = -1; // saved state no longer reachable
        m_undo.push_back(std::move(txn));
        m_redo.clear();
        if (m_undo.size() > 500) {
            m_undo.erase(m_undo.begin());
            --m_savedUndoDepth;
        }
        emit undoStateChanged();
    }
    if (!empty)
        m_lastMergeKey = mergeKey;
    notify();
}

void Document::setMergeKey(const QString& key)
{
    if (m_txn)
        m_txn->mergeKey = key;
}

void Document::cancel()
{
    assert(m_depth > 0);
    m_depth = 0;
    auto txn = std::move(m_txn);
    for (auto it = txn->ops.rbegin(); it != txn->ops.rend(); ++it) {
        unlink(it->id);
        if (it->oldParent != kNoNode)
            link(it->id, it->oldParent, it->oldIndex);
    }
    applyProps(txn->before);
    m_selection = txn->selBefore;
    m_dirty.clear();
    m_userEdited.clear();
    m_structureChanged = true;
    notify();
    emit selectionChanged();
}

void Document::notify()
{
    emit changed(m_structureChanged);
    m_structureChanged = false;
}

void Document::stamp(NodeId id)
{
    ++m_counter;
    Node* n = m_nodes.at(id).get();
    n->version = m_counter;
    while (n) {
        n->subtreeVersion = m_counter;
        n = n->parent ? m_nodes.at(n->parent).get() : nullptr;
    }
    m_dirty.insert(id);
}

Props& Document::edit(NodeId id)
{
    assert(m_depth > 0 && "edit() outside transaction");
    Node& n = *m_nodes.at(id);
    if (!m_txn->created.count(id) && !m_txn->before.count(id))
        m_txn->before.emplace(id, n.p);
    stamp(id);
    if (!m_deriving)
        m_userEdited.insert(id);
    return n.p;
}

NodeId Document::rawCreate(NodeId id, const Props& p)
{
    auto n = std::make_unique<Node>();
    n->id = id;
    n->p = p;
    m_nodes.emplace(id, std::move(n));
    reserveIds(id);
    return id;
}

void Document::rawLink(NodeId id, NodeId parent)
{
    Node& n = *m_nodes.at(id);
    n.parent = parent;
    m_nodes.at(parent)->children.push_back(id);
}

void Document::finishLoad()
{
    for (auto& [id, n] : m_nodes)
        n->attached = false;
    setAttached(m_root, true);
    ++m_counter;
    for (auto& [id, n] : m_nodes)
        n->version = n->subtreeVersion = m_counter;
    m_undo.clear();
    m_redo.clear();
    m_savedUndoDepth = 0;
    m_dirtyNoUndo = false;
    auto ps = pages();
    m_currentPage = ps.empty() ? kNoNode : ps.front();
    m_structureChanged = true;
    emit pageChanged();
    emit selectionChanged();
    emit undoStateChanged();
    notify();
}

NodeId Document::create(NodeType type, NodeId parent, int index)
{
    Props p;
    p.type = type;
    return create(p, parent, index);
}

NodeId Document::create(const Props& props, NodeId parent, int index)
{
    assert(m_depth > 0);
    NodeId id = rawCreate(allocId(), props);
    m_txn->created.insert(id);
    stamp(id);
    if (!m_deriving)
        m_userEdited.insert(id);
    move(id, parent, index);
    return id;
}

void Document::link(NodeId id, NodeId parent, int index)
{
    Node& n = *m_nodes.at(id);
    Node& p = *m_nodes.at(parent);
    if (index < 0 || index > (int)p.children.size())
        index = (int)p.children.size();
    p.children.insert(p.children.begin() + index, id);
    n.parent = parent;
    setAttached(id, p.attached);
    stamp(parent);
    stamp(id);
    m_structureChanged = true;
}

void Document::unlink(NodeId id)
{
    Node& n = *m_nodes.at(id);
    if (n.parent == kNoNode)
        return;
    Node& p = *m_nodes.at(n.parent);
    std::erase(p.children, id);
    stamp(n.parent);
    m_dirty.insert(n.parent);
    n.parent = kNoNode;
    setAttached(id, false);
    m_structureChanged = true;
}

void Document::setAttached(NodeId id, bool attached)
{
    Node& n = *m_nodes.at(id);
    n.attached = attached;
    for (NodeId c : n.children)
        setAttached(c, attached);
}

void Document::move(NodeId id, NodeId newParent, int index)
{
    assert(m_depth > 0);
    Node& n = *m_nodes.at(id);
    StructOp op{id, n.parent, indexOf(id), newParent, index};
    if (op.oldParent == newParent && newParent != kNoNode) {
        // Moving within the same parent: index is interpreted after removal.
        if (index == op.oldIndex)
            return;
    }
    unlink(id);
    if (newParent != kNoNode) {
        link(id, newParent, index);
        op.newIndex = indexOf(id);
    }
    m_txn->ops.push_back(op);
}

void Document::remove(NodeId id)
{
    // Removing selected nodes drops them from the selection.
    std::vector<NodeId> sel;
    for (NodeId s : m_selection)
        if (s != id && !isAncestor(id, s))
            sel.push_back(s);
    if (sel.size() != m_selection.size())
        setSelection(sel);
    move(id, kNoNode);
}

NodeId Document::duplicate(NodeId id, NodeId newParent, int index, std::unordered_map<NodeId, NodeId>* mapping)
{
    std::unordered_map<NodeId, NodeId> local;
    auto& map = mapping ? *mapping : local;
    std::function<NodeId(NodeId, NodeId, int)> copy = [&](NodeId src, NodeId parent, int idx) {
        NodeId c = create(at(src).p, parent, idx);
        map[src] = c;
        auto kids = at(src).children; // copy: tree may change while iterating
        for (NodeId k : kids)
            copy(k, c, -1);
        return c;
    };
    NodeId r = copy(id, newParent, index);
    return r;
}

void Document::applyProps(const std::unordered_map<NodeId, Props>& props)
{
    for (auto& [id, p] : props) {
        m_nodes.at(id)->p = p;
        stamp(id);
    }
}

QString Document::undoLabel() const { return m_undo.empty() ? QString() : m_undo.back()->label; }
QString Document::redoLabel() const { return m_redo.empty() ? QString() : m_redo.back()->label; }

void Document::undo()
{
    if (m_undo.empty() || m_depth > 0)
        return;
    m_lastMergeKey.clear();
    auto txn = std::move(m_undo.back());
    m_undo.pop_back();
    for (auto it = txn->ops.rbegin(); it != txn->ops.rend(); ++it) {
        unlink(it->id);
        if (it->oldParent != kNoNode)
            link(it->id, it->oldParent, it->oldIndex);
    }
    applyProps(txn->before);
    m_dirty.clear();
    m_userEdited.clear();
    m_selection = txn->selBefore;
    std::erase_if(m_selection, [&](NodeId s) { return !isAttached(s); });
    if (txn->pageBefore != m_currentPage && isAttached(txn->pageBefore)) {
        m_currentPage = txn->pageBefore;
        emit pageChanged();
    }
    m_redo.push_back(std::move(txn));
    m_structureChanged = true;
    notify();
    emit selectionChanged();
    emit undoStateChanged();
}

void Document::redo()
{
    if (m_redo.empty() || m_depth > 0)
        return;
    m_lastMergeKey.clear();
    auto txn = std::move(m_redo.back());
    m_redo.pop_back();
    for (auto& op : txn->ops) {
        unlink(op.id);
        if (op.newParent != kNoNode)
            link(op.id, op.newParent, op.newIndex);
    }
    applyProps(txn->after);
    m_dirty.clear();
    m_userEdited.clear();
    m_selection = txn->selAfter;
    std::erase_if(m_selection, [&](NodeId s) { return !isAttached(s); });
    if (txn->pageAfter != m_currentPage && isAttached(txn->pageAfter)) {
        m_currentPage = txn->pageAfter;
        emit pageChanged();
    }
    m_undo.push_back(std::move(txn));
    m_structureChanged = true;
    notify();
    emit selectionChanged();
    emit undoStateChanged();
}

void Document::markSaved()
{
    m_savedUndoDepth = (int)m_undo.size();
    m_dirtyNoUndo = false;
    emit undoStateChanged();
}

void Document::setSelection(std::vector<NodeId> ids)
{
    std::erase_if(ids, [&](NodeId s) { return !isAttached(s); });
    if (ids == m_selection)
        return;
    m_selection = std::move(ids);
    emit selectionChanged();
}

// ---------------------------------------------------------------- queries

SkMatrix Document::worldTransform(NodeId id) const
{
    SkMatrix m = SkMatrix::I();
    const Node* n = find(id);
    while (n && n->type() != NodeType::Page && n->type() != NodeType::Document) {
        m.postConcat(n->p.transform);
        n = n->parent ? find(n->parent) : nullptr;
    }
    return m;
}

SkRect Document::worldBounds(NodeId id) const
{
    return worldTransform(id).mapRect(at(id).localRect());
}

SkRect Document::visualBounds(NodeId id) const
{
    const Node& n = at(id);
    SkRect r = worldBounds(id);
    if (!n.p.strokes.empty() && n.p.stroke.align != StrokeAlign::Inside) {
        float out = n.p.stroke.align == StrokeAlign::Center ? n.p.stroke.weight / 2 : n.p.stroke.weight;
        r.outset(out, out);
    }
    for (const Effect& e : n.p.effects) {
        if (!e.visible)
            continue;
        if (e.type == EffectType::DropShadow) {
            SkRect s = r.makeOffset(e.offset.x(), e.offset.y());
            s.outset(e.radius + e.spread, e.radius + e.spread);
            r.join(s);
        } else if (e.type == EffectType::LayerBlur) {
            r.outset(e.radius * 2, e.radius * 2);
        }
    }
    const bool clips = isFrameLike(n.type()) && n.p.clipsContent;
    if (!clips)
        for (NodeId c : n.children)
            if (at(c).p.visible)
                r.join(visualBounds(c));
    return r;
}

NodeId Document::pageOf(NodeId id) const
{
    const Node* n = find(id);
    while (n && n->type() != NodeType::Page)
        n = n->parent ? find(n->parent) : nullptr;
    return n ? n->id : kNoNode;
}

bool Document::isAncestor(NodeId ancestor, NodeId id) const
{
    const Node* n = find(id);
    while (n && n->parent) {
        if (n->parent == ancestor)
            return true;
        n = find(n->parent);
    }
    return false;
}

int Document::indexOf(NodeId id) const
{
    const Node& n = at(id);
    if (!n.parent)
        return -1;
    const auto& ch = at(n.parent).children;
    auto it = std::find(ch.begin(), ch.end(), id);
    return it == ch.end() ? -1 : int(it - ch.begin());
}

NodeId Document::instanceRootOf(NodeId id) const
{
    const Node* n = find(id);
    while (n && n->type() != NodeType::Page) {
        if (n->type() == NodeType::Instance)
            return n->id;
        n = n->parent ? find(n->parent) : nullptr;
    }
    return kNoNode;
}

NodeId Document::componentOf(NodeId id) const
{
    const Node* n = find(id);
    while (n && n->type() != NodeType::Page) {
        if (n->type() == NodeType::Component)
            return n->id;
        n = n->parent ? find(n->parent) : nullptr;
    }
    return kNoNode;
}

NodeId Document::mainOf(NodeId id) const
{
    const Node& n = at(id);
    if (n.p.sourceId)
        return n.p.sourceId;
    if (n.type() == NodeType::Instance)
        return n.p.componentId;
    return kNoNode;
}

bool Document::isInsideInstance(NodeId id) const
{
    const Node& n = at(id);
    return n.parent && instanceRootOf(n.parent) != kNoNode;
}

std::vector<NodeId> Document::instancesOf(NodeId component) const
{
    std::vector<NodeId> out;
    for (auto& [id, n] : m_nodes)
        if (n->attached && n->type() == NodeType::Instance && n->p.componentId == component)
            out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<NodeId> Document::allComponents() const
{
    std::vector<NodeId> out;
    for (auto& [id, n] : m_nodes)
        if (n->attached && n->type() == NodeType::Component)
            out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------- geometry

void Document::setPosition(NodeId id, float x, float y)
{
    Props& p = edit(id);
    p.transform.setTranslateX(x);
    p.transform.setTranslateY(y);
}

void Document::setWorldTransform(NodeId id, const SkMatrix& world)
{
    SkMatrix parentWorld = worldTransform(at(id).parent);
    SkMatrix inv;
    if (!parentWorld.invert(&inv))
        return;
    edit(id).transform = SkMatrix::Concat(inv, world);
}

static void scaleProps(Props& p, float sx, float sy)
{
    // Scale geometry-carrying data of a node whose box changes by (sx, sy).
    if (p.type == NodeType::Vector || p.type == NodeType::Line)
        p.vector.transform(SkMatrix::Scale(sx, sy));
}

void Document::resize(NodeId id, float w, float h)
{
    w = std::max(w, 0.01f);
    h = std::max(h, 0.01f);
    const Node& n = at(id);
    const float ow = n.p.width, oh = n.p.height;
    if (ow == w && oh == h)
        return;
    const NodeType t = n.type();
    {
        Props& p = edit(id);
        p.width = w;
        p.height = h;
        if (t == NodeType::Line)
            p.height = 0;
        scaleProps(p, ow > 0 ? w / ow : 1, oh > 0 ? h / oh : 1);
    }
    const float sx = ow > 0 ? w / ow : 1, sy = oh > 0 ? h / oh : 1;
    auto kids = at(id).children;

    if (t == NodeType::Group || t == NodeType::BooleanOp) {
        // Groups scale their content.
        for (NodeId c : kids) {
            const Props& cp = at(c).p;
            SkMatrix m = cp.transform;
            // Scale the child's box in group space; keep rotation by scaling the origin and size.
            float nx = m.getTranslateX() * sx, ny = m.getTranslateY() * sy;
            // Map local size scale through rotation: approximate with axis-aligned scale for rotated children.
            SkVector ax = {m.getScaleX(), m.getSkewY()};
            SkVector ay = {m.getSkewX(), m.getScaleY()};
            float csx = SkVector::Length(ax.x() * sx, ax.y() * sy);
            float csy = SkVector::Length(ay.x() * sx, ay.y() * sy);
            resize(c, cp.width * csx, cp.height * csy);
            setPosition(c, nx, ny);
        }
        return;
    }
    if (!isFrameLike(t))
        return;
    const bool autoLayout = n.p.layout.mode != LayoutMode::None;
    for (NodeId c : kids) {
        const Props& cp = at(c).p;
        if (autoLayout && !cp.layoutChild.absolute)
            continue; // layout pass positions these
        SkRect b = cp.transform.mapRect(SkRect::MakeWH(cp.width, cp.height));
        float x = b.left(), y = b.top(), bw = b.width(), bh = b.height();
        float nx = x, nw = bw, ny = y, nh = bh;
        switch (cp.constraintH) {
        case ConstraintH::Left: break;
        case ConstraintH::Right: nx = x + (w - ow); break;
        case ConstraintH::LeftRight: nw = bw + (w - ow); break;
        case ConstraintH::Center: nx = x + (w - ow) / 2; break;
        case ConstraintH::Scale: nx = x * sx; nw = bw * sx; break;
        }
        switch (cp.constraintV) {
        case ConstraintV::Top: break;
        case ConstraintV::Bottom: ny = y + (h - oh); break;
        case ConstraintV::TopBottom: nh = bh + (h - oh); break;
        case ConstraintV::Center: ny = y + (h - oh) / 2; break;
        case ConstraintV::Scale: ny = y * sy; nh = bh * sy; break;
        }
        if (nw != bw || nh != bh) {
            float fx = bw > 0 ? nw / bw : 1, fy = bh > 0 ? nh / bh : 1;
            resize(c, std::max(0.01f, cp.width * fx), std::max(0.01f, cp.height * fy));
        }
        // Re-anchor by the AABB so rotated children move correctly.
        const Props& cp2 = at(c).p;
        SkRect b2 = cp2.transform.mapRect(SkRect::MakeWH(cp2.width, cp2.height));
        Props& e = edit(c);
        e.transform.postTranslate(nx - b2.left(), ny - b2.top());
    }
}

void Document::fitGroup(NodeId id)
{
    const Node& g = at(id);
    if (g.children.empty())
        return;
    SkRect u = SkRect::MakeEmpty();
    bool first = true;
    for (NodeId c : g.children) {
        const Props& cp = at(c).p;
        SkRect b = cp.transform.mapRect(SkRect::MakeWH(cp.width, cp.height));
        if (first) {
            u = b;
            first = false;
        } else {
            u.join(b);
        }
    }
    if (u.left() == 0 && u.top() == 0 && u.width() == g.p.width && u.height() == g.p.height)
        return;
    auto kids = g.children;
    if (u.left() != 0 || u.top() != 0)
        for (NodeId c : kids)
            edit(c).transform.postTranslate(-u.left(), -u.top());
    Props& p = edit(id);
    // Groups carry no rotation of their own: shift in parent space through the group transform.
    SkPoint o = p.transform.mapXY(u.left(), u.top());
    p.transform.setTranslateX(o.x());
    p.transform.setTranslateY(o.y());
    p.width = std::max(u.width(), 0.01f);
    p.height = std::max(u.height(), 0.01f);
}

QString Document::addImage(const QByteArray& encoded)
{
    QString hash = QString::fromLatin1(QCryptographicHash::hash(encoded, QCryptographicHash::Sha1).toHex());
    if (!m_images.contains(hash)) {
        m_images.insert(hash, encoded);
        m_dirtyNoUndo = true;
    }
    return hash;
}

// ---------------------------------------------------------------- derived state

void Document::updateDerived()
{
    if (m_deriving || (m_dirty.empty() && m_userEdited.empty()))
        return;
    assert(m_depth > 0);
    m_deriving = true;
    markOverrides();
    syncInstances();
    runLayout();
    m_dirty.clear();
    m_userEdited.clear();
    m_deriving = false;
}

void Document::markOverrides()
{
    for (NodeId id : m_userEdited) {
        const Node* n = find(id);
        if (!n || !n->attached)
            continue;
        NodeId main = mainOf(id);
        if (!main || !exists(main))
            continue;
        if (n->type() != NodeType::Instance && !isInsideInstance(id))
            continue;
        // A new instance hasn't been synced from its main yet; differences aren't user overrides.
        if (n->type() == NodeType::Instance && !n->p.sourceId && m_txn && m_txn->created.count(id))
            continue;
        std::uint32_t mask = diffOverrideMask(at(main).p, n->p);
        if (n->type() == NodeType::Instance && n->p.componentId != at(main).p.componentId && n->p.sourceId)
            mask |= OvComponent;
        if ((n->p.overrides | mask) != n->p.overrides) {
            // Flag write without re-entering override tracking.
            Props& p = edit(id);
            p.overrides |= mask;
        }
    }
}

void Document::syncInstances()
{
    // Repeat until nothing changes: syncing an instance that sits inside a main component
    // dirties that component, whose own instances then need syncing as well.
    std::unordered_set<NodeId> all = m_dirty;
    for (int round = 0; round < 16 && !m_dirty.empty(); ++round) {
        std::unordered_set<NodeId> pending;
        pending.swap(m_dirty);
        std::unordered_set<NodeId> comps;
        std::vector<NodeId> insts;
        for (NodeId id : pending) {
            const Node* n = find(id);
            if (!n || !n->attached)
                continue;
            if (n->type() == NodeType::Instance)
                insts.push_back(id);
            for (const Node* a = n; a && a->type() != NodeType::Page; a = a->parent ? find(a->parent) : nullptr)
                if (a->type() == NodeType::Component)
                    comps.insert(a->id);
        }
        for (NodeId c : comps)
            for (NodeId i : instancesOf(c))
                insts.push_back(i);
        std::sort(insts.begin(), insts.end());
        insts.erase(std::unique(insts.begin(), insts.end()), insts.end());
        for (NodeId i : insts)
            if (isAttached(i))
                syncInstance(i);
        all.insert(m_dirty.begin(), m_dirty.end());
    }
    m_dirty = std::move(all);
}

void Document::syncInstance(NodeId inst)
{
    const Node& in = at(inst);
    NodeId comp = in.p.componentId;
    if (!comp || !exists(comp) || !at(comp).attached)
        return; // missing main: instance keeps last state
    const Props& cp = at(comp).p;
    {
        Props next = in.p;
        // A fresh instance also takes the main's resizing behavior.
        if (in.children.empty() && !in.p.overrides) {
            next.layoutChild.sizingH = cp.layoutChild.sizingH;
            next.layoutChild.sizingV = cp.layoutChild.sizingV;
        }
        copyNonOverridden(next, cp, in.p.overrides, !in.p.sourceId);
        next.type = NodeType::Instance;
        next.componentId = comp;
        if (!(next == in.p))
            edit(inst) = next;
    }
    syncChildren(inst, comp);
}

void Document::syncChildren(NodeId instNode, NodeId mainNode)
{
    const auto mainKids = at(mainNode).children;
    std::unordered_map<NodeId, NodeId> bySource;
    for (NodeId c : at(instNode).children)
        if (at(c).p.sourceId)
            bySource[at(c).p.sourceId] = c;

    std::vector<NodeId> want;
    for (NodeId mk : mainKids) {
        auto it = bySource.find(mk);
        NodeId c;
        if (it == bySource.end()) {
            c = cloneFromMain(mk, instNode, -1);
        } else {
            c = it->second;
            bySource.erase(it);
            const Props& mp = at(mk).p;
            const Props& cur = at(c).p;
            Props next = cur;
            std::uint32_t ov = cur.overrides;
            copyNonOverridden(next, mp, ov);
            next.transform = mp.transform;
            next.sourceId = mk;
            next.overrides = ov;
            if (ov & OvComponent)
                next.componentId = cur.componentId;
            if (!(next == cur))
                edit(c) = next;
            if (at(c).type() == NodeType::Instance && (ov & OvComponent))
                syncInstance(c);
            else
                syncChildren(c, mk);
        }
        want.push_back(c);
    }
    for (auto& [src, c] : bySource)
        move(c, kNoNode);
    // Reorder to match main.
    for (int i = 0; i < (int)want.size(); ++i)
        if (indexOf(want[i]) != i)
            move(want[i], instNode, i);
}

NodeId Document::cloneFromMain(NodeId mainNode, NodeId parent, int index)
{
    Props p = at(mainNode).p;
    p.sourceId = mainNode;
    p.overrides = 0;
    if (p.type == NodeType::Component)
        p.type = NodeType::Frame;
    NodeId c = create(p, parent, index);
    for (NodeId k : at(mainNode).children)
        cloneFromMain(k, c, -1);
    return c;
}

void Document::runLayout()
{
    std::unordered_set<NodeId> affected;
    for (NodeId id : m_dirty) {
        for (const Node* n = find(id); n && n->type() != NodeType::Page && n->type() != NodeType::Document;
             n = n->parent ? find(n->parent) : nullptr) {
            if (!affected.insert(n->id).second)
                break;
        }
    }
    if (affected.empty())
        return;
    LayoutEngine engine(*this);
    for (NodeId page : pages())
        for (NodeId top : at(page).children)
            if (affected.count(top))
                engine.update(top, affected);
}

} // namespace drav
