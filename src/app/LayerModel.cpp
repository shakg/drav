#include "LayerModel.h"

#include "Editor.h"

namespace drav {

LayerModel::LayerModel(Editor* editor)
    : QAbstractListModel(editor)
    , m_editor(editor)
{
}

int LayerModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> LayerModel::roleNames() const
{
    return {
        {IdRole, "nodeId"},
        {NameRole, "name"},
        {TypeRole, "type"},
        {DepthRole, "depth"},
        {HasChildrenRole, "hasChildren"},
        {ExpandedRole, "expanded"},
        {VisibleRole, "layerVisible"},
        {LockedRole, "locked"},
        {SelectedRole, "selected"},
        {ParentSelectedRole, "parentSelected"},
        {ComponentRole, "component"},
        {MaskRole, "mask"},
        {AutoLayoutRole, "autoLayout"},
    };
}

QVariant LayerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_rows.size()))
        return {};
    const Row& r = m_rows[index.row()];
    switch (role) {
    case IdRole: return qint64(r.id);
    case NameRole: return r.name;
    case TypeRole: return QString::fromLatin1(nodeTypeName(r.type));
    case DepthRole: return r.depth;
    case HasChildrenRole: return r.hasChildren;
    case ExpandedRole: return r.expanded;
    case VisibleRole: return r.visible;
    case LockedRole: return r.locked;
    case SelectedRole: return r.selected;
    case ParentSelectedRole: return r.parentSelected;
    case ComponentRole: return r.component;
    case MaskRole: return r.mask;
    case AutoLayoutRole: return r.autoLayout;
    default: return {};
    }
}

void LayerModel::build(std::vector<Row>& out) const
{
    const Document& d = m_editor->doc();
    std::unordered_set<NodeId> sel(d.selection().begin(), d.selection().end());
    // Ancestors of the selection are shown expanded.
    std::unordered_set<NodeId> autoOpen;
    for (NodeId s : sel)
        for (NodeId p = d.isAttached(s) ? d.at(s).parent : kNoNode; p; p = d.at(p).parent)
            autoOpen.insert(p);
    std::function<void(NodeId, int, bool, bool)> rec = [&](NodeId id, int depth, bool parentSel, bool inInstance) {
        const Node& n = d.at(id);
        const bool expanded = m_expanded.count(id) || autoOpen.count(id);
        const bool isSel = sel.count(id);
        int comp = 0;
        if (n.type() == NodeType::Component || n.type() == NodeType::ComponentSet)
            comp = 1;
        else if (n.type() == NodeType::Instance || inInstance)
            comp = 2;
        out.push_back({id, n.p.name, n.type(), depth, !n.children.empty(), expanded, n.p.visible, n.p.locked, isSel,
            parentSel, comp, n.p.isMask, n.p.layout.mode != LayoutMode::None});
        if (expanded)
            for (auto it = n.children.rbegin(); it != n.children.rend(); ++it)
                rec(*it, depth + 1, parentSel || isSel, inInstance || n.type() == NodeType::Instance);
    };
    const auto& top = d.at(d.currentPage()).children;
    for (auto it = top.rbegin(); it != top.rend(); ++it)
        rec(*it, 0, false, false);
}

void LayerModel::sync()
{
    std::vector<Row> next;
    build(next);
    // Diff by common prefix/suffix of node ids so the view keeps its scroll position.
    size_t pre = 0;
    while (pre < m_rows.size() && pre < next.size() && m_rows[pre].id == next[pre].id)
        ++pre;
    size_t suf = 0;
    while (suf < m_rows.size() - pre && suf < next.size() - pre
        && m_rows[m_rows.size() - 1 - suf].id == next[next.size() - 1 - suf].id)
        ++suf;
    const int oldMid = int(m_rows.size() - pre - suf), newMid = int(next.size() - pre - suf);
    if (oldMid > 0) {
        beginRemoveRows({}, int(pre), int(pre) + oldMid - 1);
        m_rows.erase(m_rows.begin() + pre, m_rows.begin() + pre + oldMid);
        endRemoveRows();
    }
    if (newMid > 0) {
        beginInsertRows({}, int(pre), int(pre) + newMid - 1);
        m_rows.insert(m_rows.begin() + pre, next.begin() + pre, next.begin() + pre + newMid);
        endInsertRows();
    }
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (!(m_rows[i] == next[i])) {
            m_rows[i] = next[i];
            emit dataChanged(index(int(i)), index(int(i)));
        }
    }
}

void LayerModel::setExpanded(NodeId id, bool expanded)
{
    if (expanded)
        m_expanded.insert(id);
    else
        m_expanded.erase(id);
    sync();
}

int LayerModel::rowOf(qint64 id) const
{
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].id == NodeId(id))
            return int(i);
    return -1;
}

QVariantMap LayerModel::dropTarget(int row, double pos) const
{
    const Document& d = m_editor->doc();
    if (row < 0 || row >= int(m_rows.size()))
        return {{QStringLiteral("parent"), qint64(d.currentPage())}, {QStringLiteral("index"), 0}};
    const Row& r = m_rows[row];
    const Node& n = d.at(r.id);
    const bool container = isFrameLike(n.type()) || n.type() == NodeType::Group || n.type() == NodeType::BooleanOp;
    if (container && n.type() != NodeType::Instance && pos > 0.3 && pos < 0.7) {
        // Into: becomes the topmost child.
        return {{QStringLiteral("parent"), qint64(r.id)}, {QStringLiteral("index"), int(n.children.size())},
            {QStringLiteral("into"), true}};
    }
    // The list is top-first, so "above" in the list means a higher document index.
    const int idx = d.indexOf(r.id);
    const int index = pos < 0.5 ? idx + 1 : idx;
    return {{QStringLiteral("parent"), qint64(n.parent)}, {QStringLiteral("index"), index}, {QStringLiteral("into"), false}};
}

} // namespace drav
