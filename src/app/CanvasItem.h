#pragma once

#include "Editor.h"

#include <QQuickFramebufferObject>
#include <QtQml/qqmlregistration.h>

namespace drav {

// Infinite canvas. Draws the editor's frame snapshots with Skia (Ganesh/GL) into the item's FBO
// on Qt Quick's render thread and forwards input to the Editor.
class CanvasItem : public QQuickFramebufferObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(drav::Editor* editor READ editor WRITE setEditor NOTIFY editorChanged)
public:
    explicit CanvasItem(QQuickItem* parent = nullptr);

    Renderer* createRenderer() const override;
    Editor* editor() const { return m_editor; }
    void setEditor(Editor* e);

signals:
    void editorChanged();
    void contextMenuRequested(double x, double y);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void hoverMoveEvent(QHoverEvent* e) override;
    void hoverLeaveEvent(QHoverEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void inputMethodEvent(QInputMethodEvent* e) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery q) const override;
    bool event(QEvent* e) override;
    void geometryChange(const QRectF& n, const QRectF& o) override;

private:
    void updateCursor();
    bool m_panning = false;
    QPointF m_lastPan;
    Editor* m_editor = nullptr;
};

} // namespace drav
