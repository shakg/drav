#pragma once

#include "Library.h"
#include "Node.h"

#include <QByteArray>
#include <QHash>
#include <QObject>

#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace drav {

// Owns the node tree, the undo history and derived-state updates (auto layout, instance sync).
//
// All mutations happen inside a transaction:
//     doc.begin("Move");  doc.edit(id).transform = ...;  doc.commit();
// edit() snapshots a node's props on first touch; commit() runs derived updates and pushes the
// transaction onto the undo stack. During interactive drags call updateDerived() between edits.
class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject* parent = nullptr);
    ~Document() override;

    void clear(); // empty document with one page

    NodeId root() const { return m_root; }
    const Node& at(NodeId id) const { return *m_nodes.at(id); }
    const Node* find(NodeId id) const;
    bool exists(NodeId id) const { return find(id) != nullptr; }
    bool isAttached(NodeId id) const;
    std::vector<NodeId> pages() const { return at(m_root).children; }

    NodeId currentPage() const { return m_currentPage; }
    void setCurrentPage(NodeId page);

    // ---- transactions ----
    void begin(const QString& label);
    void commit();
    // Successive commits with the same non-empty key fold into one undo step (typing, nudging, scrubbing).
    void setMergeKey(const QString& key);
    void breakMerge() { m_lastMergeKey.clear(); }
    void cancel(); // revert everything since begin()
    bool inTransaction() const { return m_depth > 0; }

    Props& edit(NodeId id);
    NodeId create(NodeType type, NodeId parent, int index = -1);
    NodeId create(const Props& props, NodeId parent, int index = -1);
    void move(NodeId id, NodeId newParent, int index = -1);
    void remove(NodeId id);
    // Deep copy (new ids) placed under newParent. Returns map old->new for the copied subtree.
    NodeId duplicate(NodeId id, NodeId newParent, int index = -1,
        std::unordered_map<NodeId, NodeId>* mapping = nullptr);

    void updateDerived();

    // ---- undo ----
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    QString undoLabel() const;
    QString redoLabel() const;
    void undo();
    void redo();
    bool isDirty() const { return m_savedUndoDepth != (int)m_undo.size() || m_dirtyNoUndo; }
    void markSaved();
    void markDirty() { m_dirtyNoUndo = true; emit undoStateChanged(); }

    // ---- selection (not undoable itself, but restored by undo) ----
    const std::vector<NodeId>& selection() const { return m_selection; }
    void setSelection(std::vector<NodeId> ids);

    // ---- queries ----
    SkMatrix worldTransform(NodeId id) const; // local -> page space
    SkRect worldBounds(NodeId id) const;      // AABB in page space of the node's box
    SkRect visualBounds(NodeId id) const;     // AABB in page space including descendants/strokes
    NodeId pageOf(NodeId id) const;
    bool isAncestor(NodeId ancestor, NodeId id) const;
    int indexOf(NodeId id) const;
    NodeId instanceRootOf(NodeId id) const; // nearest Instance ancestor-or-self
    NodeId componentOf(NodeId id) const;    // nearest Component ancestor-or-self
    NodeId mainOf(NodeId id) const;         // main-component counterpart (sourceId or componentId)
    bool isInsideInstance(NodeId id) const;
    std::vector<NodeId> instancesOf(NodeId component) const;
    // Component property definitions visible from a component, component set or instance
    // (a variant also sees its set's definitions).
    std::vector<const ComponentProp*> propDefs(NodeId id) const;
    // Nearest ancestor (instance, component or set) that defines the property for this layer.
    NodeId propOwner(NodeId id, const QString& propId) const;
    // Value of a property at its owner: an instance's value, else the definition's default.
    QString propValue(NodeId owner, const QString& propId) const;
    // Current value of a property-driven field of a layer, in the property's string form.
    static QString fieldValue(const Props& p, const QString& field);
    std::vector<NodeId> allComponents() const;
    template <typename F> void walk(NodeId id, F&& f) const
    {
        if (!f(at(id)))
            return;
        for (NodeId c : at(id).children)
            walk(c, f);
    }

    // ---- geometry helpers that keep invariants ----
    // Resizes the node's box, applying children constraints / group scaling / vector scaling.
    void resize(NodeId id, float w, float h);
    void setPosition(NodeId id, float x, float y); // parent-space translation
    void setWorldTransform(NodeId id, const SkMatrix& world);
    void fitGroup(NodeId id);
    // Scales a node and its subtree by `f` (sizes, positions of children, strokes, radii, effects,
    // text sizes, layout spacing, vectors); the node's own position is left to the caller.
    void scaleDeep(NodeId id, float f);

    // ---- styles and variables ----
    const Library& library() const;
    ColorProfile colorProfile() const { return at(m_root).p.colorProfile; }
    std::shared_ptr<const Library> libraryPtr() const { return at(m_root).p.library; }
    // Inside a transaction: a fresh copy of the library to modify (undo keeps the old one).
    Library& editLibrary();

    // ---- images ----
    QString addImage(const QByteArray& encoded); // returns content hash
    const QHash<QString, QByteArray>& images() const { return m_images; }

    std::uint64_t counter() const { return m_counter; }

    // Allow serializer/clipboard to create nodes with explicit ids.
    NodeId allocId() { return ++m_nextId; }
    void reserveIds(NodeId upTo) { m_nextId = std::max(m_nextId, upTo); }

    // Raw access for the loader (no undo). Use only while building a document.
    Node& rawNode(NodeId id) { return *m_nodes.at(id); }
    NodeId rawCreate(NodeId id, const Props& p);
    void rawLink(NodeId id, NodeId parent);
    void clearForLoad(); // drops all nodes and history; follow with rawCreate/rawLink/setRoot/finishLoad
    void finishLoad();   // fixes attached flags, versions; clears history
    void setRoot(NodeId id) { m_root = id; }

signals:
    void changed(bool structure);
    void selectionChanged();
    void undoStateChanged();
    void pageChanged();

private:
    struct StructOp {
        NodeId id;
        NodeId oldParent;
        int oldIndex;
        NodeId newParent;
        int newIndex;
    };
    struct Txn {
        QString label;
        QString mergeKey;
        std::unordered_map<NodeId, Props> before;
        std::unordered_map<NodeId, Props> after;
        std::unordered_set<NodeId> created;
        std::vector<StructOp> ops;
        std::vector<NodeId> selBefore, selAfter;
        NodeId pageBefore = kNoNode, pageAfter = kNoNode;
    };

    void stamp(NodeId id);
    void link(NodeId id, NodeId parent, int index);
    void unlink(NodeId id);
    void setAttached(NodeId id, bool attached);
    void applyProps(const std::unordered_map<NodeId, Props>& props);
    void notify();

    // derived
    void syncInstances();
    void syncInstance(NodeId inst);
    void syncChildren(NodeId instNode, NodeId mainNode);
    NodeId cloneFromMain(NodeId mainNode, NodeId parent, int index);
    void markOverrides();
    void resolveBindings();
    void applyComponentProps();
    void applyComponentProps(NodeId id);
    bool applyPropRefs(NodeId id); // returns true if a nested instance was swapped
    void computePropRefs(NodeId id, Props& next) const;
    void runLayout();
    void syncTextPaths();

    std::unordered_map<NodeId, std::unique_ptr<Node>> m_nodes;
    NodeId m_root = kNoNode;
    NodeId m_currentPage = kNoNode;
    NodeId m_nextId = 0;
    std::uint64_t m_counter = 0;

    int m_depth = 0;
    std::unique_ptr<Txn> m_txn;
    std::vector<std::unique_ptr<Txn>> m_undo, m_redo;
    int m_savedUndoDepth = 0;
    bool m_dirtyNoUndo = false;

    bool m_deriving = false;
    bool m_structureChanged = false;
    std::unordered_set<NodeId> m_dirty;       // touched since last updateDerived
    std::unordered_set<NodeId> m_userEdited;  // touched by user edits since last updateDerived

    QString m_lastMergeKey;

    std::vector<NodeId> m_selection;
    QHash<QString, QByteArray> m_images;
};

} // namespace drav
