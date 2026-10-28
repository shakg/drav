#pragma once

#include "core/Node.h"

#include <QAbstractListModel>

#include <unordered_set>

namespace drav {

class Editor;

// Flattened layer tree of the current page, topmost layer first (like Figma's layer list).
class LayerModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TypeRole,
        DepthRole,
        HasChildrenRole,
        ExpandedRole,
        VisibleRole,
        LockedRole,
        SelectedRole,
        ParentSelectedRole,
        ComponentRole, // 0 none, 1 component/set, 2 instance or inside one
        MaskRole,
        AutoLayoutRole,
    };

    explicit LayerModel(Editor* editor);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void sync();
    void setExpanded(NodeId id, bool expanded);

    // Where a drop on `row` at vertical fraction `pos` (0..1) lands: {parent, index} in document order.
    Q_INVOKABLE QVariantMap dropTarget(int row, double pos) const;
    Q_INVOKABLE int rowOf(qint64 id) const;

private:
    struct Row {
        NodeId id;
        QString name;
        NodeType type;
        int depth;
        bool hasChildren, expanded, visible, locked, selected, parentSelected;
        int component;
        bool mask;
        int autoLayout; // 0 none, 1 horizontal, 2 vertical
        bool operator==(const Row&) const = default;
    };
    void build(std::vector<Row>& out) const;

    Editor* m_editor;
    std::vector<Row> m_rows;
    std::unordered_set<NodeId> m_expanded;
};

} // namespace drav
