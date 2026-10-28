#pragma once

#include "Editor.h"


namespace drav {

class Tool {
public:
    explicit Tool(Editor& e)
        : ed(e)
    {
    }
    virtual ~Tool() = default;
    virtual void activate() {}
    virtual void deactivate() {}
    virtual void press(const InputEvent&) {}
    virtual void move(const InputEvent&) {}
    virtual void release(const InputEvent&) {}
    virtual void doubleClick(const InputEvent&) {}
    virtual bool key(QKeyEvent*) { return false; }
    virtual bool inputMethod(QInputMethodEvent*) { return false; }
    virtual void drawOverlay(SkCanvas*) {}
    virtual Qt::CursorShape cursor() const { return Qt::ArrowCursor; }

protected:
    Editor& ed;
    Document& doc() { return ed.doc(); }
};

// Move, resize, rotate, marquee and reparent by drag.
class SelectTool : public Tool {
public:
    using Tool::Tool;
    void press(const InputEvent& e) override;
    void move(const InputEvent& e) override;
    void release(const InputEvent& e) override;
    void doubleClick(const InputEvent& e) override;
    void deactivate() override;
    Qt::CursorShape cursor() const override { return m_cursor; }

private:
    enum class Mode { None, Pending, Move, Resize, Rotate, Marquee };
    enum Handle { HNone = -1, HTL, HT, HTR, HR, HBR, HB, HBL, HL };

    int handleAt(QPointF screen, bool* rotate) const;
    void beginMove(const InputEvent& e);
    void doMove(const InputEvent& e);
    void doResize(const InputEvent& e);
    void doRotate(const InputEvent& e);
    void doMarquee(const InputEvent& e);
    void updateHoverCursor(const InputEvent& e);

    Mode m_mode = Mode::None;
    InputEvent m_start;
    NodeId m_pressHit = kNoNode;
    bool m_pressWasSelected = false;
    int m_handle = HNone;
    std::vector<NodeId> m_nodes;
    std::vector<SkMatrix> m_startWorld;
    std::vector<SkSize> m_startSize;
    SkRect m_startBounds{};
    std::vector<NodeId> m_marqueeBase;
    Qt::CursorShape m_cursor = Qt::ArrowCursor;
    bool m_duplicated = false;
};

class HandTool : public Tool {
public:
    using Tool::Tool;
    void press(const InputEvent& e) override;
    void move(const InputEvent& e) override;
    void release(const InputEvent&) override { m_drag = false; }
    Qt::CursorShape cursor() const override { return m_drag ? Qt::ClosedHandCursor : Qt::OpenHandCursor; }

private:
    bool m_drag = false;
    QPointF m_last;
};

// Rectangle, ellipse, frame, polygon, star, line, arrow.
class ShapeTool : public Tool {
public:
    ShapeTool(Editor& e, NodeType type, bool arrow = false)
        : Tool(e)
        , m_type(type)
        , m_arrow(arrow)
    {
    }
    void press(const InputEvent& e) override;
    void move(const InputEvent& e) override;
    void release(const InputEvent& e) override;
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

private:
    void update(const InputEvent& e);
    NodeType m_type;
    bool m_arrow;
    NodeId m_node = kNoNode;
    SkPoint m_origin{};
    bool m_dragging = false;
};

class TextTool : public Tool {
public:
    using Tool::Tool;
    void press(const InputEvent& e) override;
    void move(const InputEvent& e) override;
    void release(const InputEvent& e) override;
    Qt::CursorShape cursor() const override { return Qt::IBeamCursor; }
    void drawOverlay(SkCanvas* c) override;

private:
    bool m_dragging = false;
    SkPoint m_origin{}, m_current{};
};

// On-canvas text editing of one text node.
class TextEditTool : public Tool {
public:
    using Tool::Tool;
    void begin(NodeId id, SkPoint pagePos, bool selectAll, bool created = false);
    void activate() override;
    void deactivate() override;
    void press(const InputEvent& e) override;
    void move(const InputEvent& e) override;
    void release(const InputEvent& e) override;
    void doubleClick(const InputEvent& e) override;
    bool key(QKeyEvent* e) override;
    bool inputMethod(QInputMethodEvent* e) override;
    void drawOverlay(SkCanvas* c) override;
    Qt::CursorShape cursor() const override { return Qt::IBeamCursor; }
    NodeId node() const { return m_node; }
    int selStart() const { return std::min(m_anchor, m_caret); }
    int selEnd() const { return std::max(m_anchor, m_caret); }

private:
    int indexAt(SkPoint page);
    void insert(const QString& s);
    void eraseSelection();
    bool hasSelection() const { return m_anchor != m_caret; }
    int lineMove(int caret, int dir);
    SkRect caretRect(int index);
    SkMatrix localToScreen();
    int toDisplay(int i) const;
    int fromDisplay(int i) const;
    QString text() const;

    NodeId m_node = kNoNode;
    int m_caret = 0;
    int m_anchor = 0;
    bool m_dragging = false;
    bool m_created = false; // node was created by the text tool for this session
};

// Scale (K): drag a selection handle to scale layers with their strokes, effects and text.
class ScaleTool : public Tool {
public:
    using Tool::Tool;
    void press(const InputEvent& e) override;
    void move(const InputEvent& e) override;
    void release(const InputEvent& e) override;
    void deactivate() override;
    void drawOverlay(SkCanvas* c) override;
    Qt::CursorShape cursor() const override { return m_cursor; }

private:
    int handleAt(QPointF screen) const;

    bool m_active = false;
    int m_handle = -1; // 0..7 clockwise from top-left
    std::vector<NodeId> m_ids;
    SkRect m_bounds{};
    float m_factor = 1;
    Qt::CursorShape m_cursor = Qt::CrossCursor;
};

// Crop an image fill: drag to move the image inside the layer, drag its corners to scale it.
class CropTool : public Tool {
public:
    using Tool::Tool;
    void begin(NodeId id, int paintIndex);
    void press(const InputEvent& e) override;
    void move(const InputEvent& e) override;
    void release(const InputEvent& e) override;
    bool key(QKeyEvent* e) override;
    void deactivate() override;
    void drawOverlay(SkCanvas* c) override;
    Qt::CursorShape cursor() const override { return m_cursor; }

private:
    SkRect imageRect(const SkRect& crop) const; // image bounds in layer space for a crop
    SkMatrix toScreen() const;
    int cornerAt(QPointF screen) const;

    NodeId m_node = kNoNode;
    int m_paint = -1;
    bool m_dragging = false;
    int m_corner = -1; // -1 = move
    SkPoint m_startLocal{};
    SkRect m_startImage{};
    Qt::CursorShape m_cursor = Qt::SizeAllCursor;
};

// Eyedropper (I): samples the canvas color under the pointer.
class EyedropperTool : public Tool {
public:
    using Tool::Tool;
    void move(const InputEvent& e) override;
    void press(const InputEvent& e) override;
    bool key(QKeyEvent* e) override;
    void drawOverlay(SkCanvas* c) override;
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

private:
    QPointF m_screen{-100, -100};
    SkColor4f m_color = SkColors::kTransparent;
};

// Bezier pen. Also used to extend paths while editing a vector.
class PenTool : public Tool {
public:
    using Tool::Tool;
    void activate() override;
    void deactivate() override;
    void press(const InputEvent& e) override;
    void move(const InputEvent& e) override;
    void release(const InputEvent& e) override;
    bool key(QKeyEvent* e) override;
    void drawOverlay(SkCanvas* c) override;
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

private:
    void finish(bool close);
    void writeBack();
    SkPoint constrain(SkPoint p, const InputEvent& e) const;

    NodeId m_node = kNoNode;
    Contour m_contour; // page space while drawing
    bool m_dragging = false;
    SkPoint m_hover{};
    bool m_hoverValid = false;
};

class PencilTool : public Tool {
public:
    using Tool::Tool;
    void press(const InputEvent& e) override;
    void move(const InputEvent& e) override;
    void release(const InputEvent& e) override;
    void drawOverlay(SkCanvas* c) override;
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

private:
    std::vector<SkPoint> m_points;
    bool m_drawing = false;
};

// Anchor/handle editing of a vector node.
// Edits a vector as a network: move vertices and handles, add points on segments (click), draw
// branches with the pen (P) and fill or clear closed areas with the paint bucket (B).
class VectorEditTool : public Tool {
public:
    using Tool::Tool;
    void begin(NodeId id);
    void deactivate() override;
    void press(const InputEvent& e) override;
    void move(const InputEvent& e) override;
    void release(const InputEvent& e) override;
    void doubleClick(const InputEvent& e) override;
    bool key(QKeyEvent* e) override;
    void drawOverlay(SkCanvas* c) override;
    Qt::CursorShape cursor() const override { return m_mode == Mode::Edit ? Qt::ArrowCursor : Qt::CrossCursor; }

    enum class Mode { Edit, Pen, Bucket };
    Mode mode() const { return m_mode; }
    void setMode(Mode m);
    const std::vector<int>& selectedVertices() const { return m_selected; }

private:
    struct Ref {
        int vertex = -1;   // a vertex, or
        int seg = -1;      // the control point of segment `seg` at end `end` (0 = a, 1 = b)
        int end = 0;
        bool valid() const { return vertex >= 0 || seg >= 0; }
    };
    Ref hit(QPointF screen, const VectorData& v) const;
    bool hitSegment(SkPoint local, const VectorData& v, int* seg, float* t) const;
    SkMatrix localToScreen() const;
    VectorData network() const; // the node's vector as a network
    void write(const VectorData& v);
    void normalize();
    void status();

    NodeId m_node = kNoNode;
    Mode m_mode = Mode::Edit;
    std::vector<int> m_selected; // vertices
    Ref m_drag;
    SkPoint m_dragStart{};
    VectorData m_startData;
    bool m_dragging = false;
    // Pen
    int m_penFrom = -1;          // vertex the next segment starts from
    SkPoint m_penOut{};          // outgoing control for the next segment (after a drag)
    bool m_penHasOut = false;
    int m_penVertex = -1;        // vertex being dragged out into a curve
    int m_penSeg = -1;           // segment ending at that vertex
    SkPoint m_hover{};
};

} // namespace drav
