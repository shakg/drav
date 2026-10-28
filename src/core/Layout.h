#pragma once

#include "Node.h"

#include <unordered_set>

namespace drav {

class Document;

// Auto layout, text auto-size and group fitting. Runs inside a document transaction as derived state.
class LayoutEngine {
public:
    explicit LayoutEngine(Document& doc)
        : m_doc(doc)
    {
    }

    // Post-order update of the subtree rooted at `id`, visiting only nodes in `affected`.
    void update(NodeId id, const std::unordered_set<NodeId>& affected);

    // Positions and sizes the children of an auto layout frame; resizes the frame if it hugs.
    void layoutFrame(NodeId id);

private:
    void intrinsic(NodeId id);
    void afterResize(NodeId id);
    void place(NodeId child, float x, float y);
    void layoutGrid(NodeId id);

    Document& m_doc;
};

// Axis-aligned size of a child's box in its parent's space.
SkSize layoutSize(const Props& p);

} // namespace drav
