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
    Qt::CursorShape cursor() const override { return Qt::ArrowCursor; }

private:
    struct Ref {
        int contour = -1, index = -1;
        int part = 0; // 0 anchor, 1 in-handle, 2 out-handle
        bool valid() const { return contour >= 0; }
        bool operator==(const Ref&) const = default;
    };
    Ref hit(QPointF screen) const;
    bool hitSegment(SkPoint local, int* contour, int* index, float* t) const;
    SkMatrix localToScreen() const;
    void normalize();

    NodeId m_node = kNoNode;
    std::vector<Ref> m_selected;
    Ref m_drag;
    SkPoint m_dragStart{};
    VectorData m_startData;
    bool m_dragging = false;
    QPointF m_marqueeStart;
    bool m_marquee = false;
};

} // namespace drav
