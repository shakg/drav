#include "CanvasItem.h"

#include "Editor.h"

#include <include/core/SkCanvas.h>
#include <include/core/SkColorSpace.h>
#include <include/core/SkSurface.h>
#include <include/gpu/ganesh/GrBackendSurface.h>
#include <include/gpu/ganesh/GrDirectContext.h>
#include <include/gpu/ganesh/SkSurfaceGanesh.h>
#include <include/gpu/ganesh/gl/GrGLAssembleInterface.h>
#include <include/gpu/ganesh/gl/GrGLBackendSurface.h>
#include <include/gpu/ganesh/gl/GrGLDirectContext.h>
#include <include/gpu/ganesh/gl/GrGLInterface.h>

#include <QElapsedTimer>

#include <cstring>
#include <QNativeGestureEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QQuickWindow>

namespace drav {

namespace {

constexpr unsigned kGL_RGBA8 = 0x8058;

// Skia runs in its own GL context that shares textures with Qt Quick's context. It renders into
// the item's FBO texture through a framebuffer of its own, so Qt's cached GL state is never disturbed.
class SkiaRenderer : public QQuickFramebufferObject::Renderer {
public:
    ~SkiaRenderer() override
    {
        if (!m_glContext)
            return;
        QOpenGLContext* qtContext = QOpenGLContext::currentContext();
        QSurface* surface = qtContext ? qtContext->surface() : nullptr;
        if (surface && m_glContext->makeCurrent(surface)) {
            m_surface.reset();
            if (m_context)
                m_context->releaseResourcesAndAbandonContext();
            releaseTarget();
            qtContext->makeCurrent(surface);
        }
    }

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override
    {
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setAttachment(QOpenGLFramebufferObject::NoAttachment);
        fmt.setInternalTextureFormat(kGL_RGBA8);
        return new QOpenGLFramebufferObject(size, fmt);
    }

    void synchronize(QQuickFramebufferObject* item) override
    {
        auto* c = static_cast<CanvasItem*>(item);
        m_editor = c->editor();
        if (m_editor)
            m_frame = m_editor->snapshot();
        m_dpr = item->window() ? float(item->window()->effectiveDevicePixelRatio()) : 1.f;
    }

    void render() override
    {
        QOpenGLFramebufferObject* fbo = framebufferObject();
        QOpenGLContext* qtContext = QOpenGLContext::currentContext();
        QSurface* surface = qtContext->surface();
        if (!m_glContext) {
            m_glContext = std::make_unique<QOpenGLContext>();
            m_glContext->setFormat(qtContext->format());
            m_glContext->setShareContext(qtContext);
            if (!m_glContext->create()) {
                qWarning("drav: could not create the canvas GL context");
                return;
            }
        }
        if (!m_glContext->makeCurrent(surface))
            return;
        draw(fbo);
        qtContext->makeCurrent(surface);

        // Frame pacing stats (render-thread wall time between frames).
        if (m_timer.isValid() && m_editor) {
            const double ms = m_timer.nsecsElapsed() / 1e6;
            QMetaObject::invokeMethod(m_editor, [e = m_editor, ms] { e->reportFrameTime(ms); }, Qt::QueuedConnection);
        }
        m_timer.restart();
    }

private:
    void releaseTarget()
    {
        auto* f = m_glContext->functions();
        if (m_fbo)
            f->glDeleteFramebuffers(1, &m_fbo);
        if (m_stencil)
            f->glDeleteRenderbuffers(1, &m_stencil);
        m_fbo = m_stencil = 0;
    }

    void draw(QOpenGLFramebufferObject* fbo)
    {
        if (!m_context) {
            auto iface = GrGLMakeAssembledInterface(nullptr, [](void*, const char name[]) -> GrGLFuncPtr {
                // glXGetProcAddress returns stubs for unknown names; Skia only needs EGL for
                // optional extension queries, so never hand out EGL entry points.
                if (std::strncmp(name, "egl", 3) == 0)
                    return nullptr;
                return reinterpret_cast<GrGLFuncPtr>(QOpenGLContext::currentContext()->getProcAddress(name));
            });
            m_context = GrDirectContexts::MakeGL(iface);
            if (!m_context) {
                qWarning("drav: could not create Skia GL context");
                return;
            }
        }
        if (!m_surface || m_texture != fbo->texture() || m_size != fbo->size()) {
            m_surface.reset();
            releaseTarget();
            auto* f = m_glContext->functions();
            f->glGenFramebuffers(1, &m_fbo);
            f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
            f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->texture(), 0);
            f->glGenRenderbuffers(1, &m_stencil);
            f->glBindRenderbuffer(GL_RENDERBUFFER, m_stencil);
            f->glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, fbo->width(), fbo->height());
            f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_stencil);
            m_context->resetContext();
            GrGLFramebufferInfo info;
            info.fFBOID = m_fbo;
            info.fFormat = kGL_RGBA8;
            auto rt = GrBackendRenderTargets::MakeGL(fbo->width(), fbo->height(), 0, 8, info);
            m_surface = SkSurfaces::WrapBackendRenderTarget(m_context.get(), rt, kTopLeft_GrSurfaceOrigin,
                kRGBA_8888_SkColorType, SkColorSpace::MakeSRGB(), nullptr);
            m_texture = fbo->texture();
            m_size = fbo->size();
            if (!m_surface) {
                qWarning("drav: could not wrap the canvas framebuffer for Skia");
                return;
            }
        }
        SkCanvas* c = m_surface->getCanvas();
        c->resetMatrix();
        c->clear(m_frame.background);
        c->save();
        c->scale(m_dpr, m_dpr);
        c->concat(m_frame.view);
        if (m_frame.content)
            c->drawPicture(m_frame.content);
        c->restore();
        if (m_frame.overlay) {
            c->save();
            c->scale(m_dpr, m_dpr);
            c->drawPicture(m_frame.overlay);
            c->restore();
        }
        // Finish before Qt samples the shared texture from its own context.
        m_context->flushAndSubmit(GrSyncCpu::kYes);
    }

    std::unique_ptr<QOpenGLContext> m_glContext;
    sk_sp<GrDirectContext> m_context;
    sk_sp<SkSurface> m_surface;
    GLuint m_fbo = 0, m_stencil = 0, m_texture = 0;
    QSize m_size;
    FrameSnapshot m_frame;
    float m_dpr = 1;
    Editor* m_editor = nullptr;
    QElapsedTimer m_timer;
};

InputEvent makeEvent(Editor* ed, QPointF pos, Qt::MouseButtons buttons, Qt::MouseButton button, Qt::KeyboardModifiers mods)
{
    InputEvent e;
    e.screen = pos;
    e.page = ed->toPage(pos);
    e.buttons = buttons;
    e.button = button;
    e.mods = mods;
    return e;
}

} // namespace

CanvasItem::CanvasItem(QQuickItem* parent)
    : QQuickFramebufferObject(parent)
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setFlag(ItemAcceptsInputMethod, true);
    setFlag(ItemIsFocusScope, true);
    setActiveFocusOnTab(true);
    setMirrorVertically(false);
}

QQuickFramebufferObject::Renderer* CanvasItem::createRenderer() const
{
    return new SkiaRenderer;
}

void CanvasItem::setEditor(Editor* e)
{
    if (e == m_editor)
        return;
    if (m_editor)
        disconnect(m_editor, nullptr, this, nullptr);
    m_editor = e;
    if (m_editor) {
        connect(m_editor, &Editor::updateRequested, this, &QQuickItem::update);
        connect(m_editor, &Editor::cursorChanged, this, &CanvasItem::updateCursor);
        m_editor->setViewportSize(size());
    }
    emit editorChanged();
    update();
}

void CanvasItem::updateCursor()
{
    if (m_editor)
        setCursor(QCursor(Qt::CursorShape(m_editor->cursorShape())));
}

void CanvasItem::geometryChange(const QRectF& n, const QRectF& o)
{
    QQuickFramebufferObject::geometryChange(n, o);
    if (m_editor)
        m_editor->setViewportSize(n.size());
}

void CanvasItem::mousePressEvent(QMouseEvent* e)
{
    forceActiveFocus(Qt::MouseFocusReason);
    if (!m_editor)
        return;
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPan = e->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::RightButton) {
        // Right-click selects what's under the cursor (if not selected) before the menu opens.
        SkPoint p = m_editor->toPage(e->position());
        NodeId hit = m_editor->selectableAt(p, false);
        const auto& sel = m_editor->doc().selection();
        if (hit && std::find(sel.begin(), sel.end(), hit) == sel.end())
            m_editor->select({hit});
        emit contextMenuRequested(e->position().x(), e->position().y());
        return;
    }
    m_editor->mousePress(makeEvent(m_editor, e->position(), e->buttons(), e->button(), e->modifiers()));
}

void CanvasItem::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_editor)
        return;
    if (m_panning) {
        m_editor->panBy(e->position() - m_lastPan);
        m_lastPan = e->position();
        return;
    }
    m_editor->mouseMove(makeEvent(m_editor, e->position(), e->buttons(), Qt::NoButton, e->modifiers()));
}

void CanvasItem::mouseReleaseEvent(QMouseEvent* e)
{
    if (!m_editor)
        return;
    if (m_panning && e->button() == Qt::MiddleButton) {
        m_panning = false;
        updateCursor();
        return;
    }
    if (e->button() == Qt::RightButton)
        return;
    m_editor->mouseRelease(makeEvent(m_editor, e->position(), e->buttons(), e->button(), e->modifiers()));
}

void CanvasItem::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (!m_editor || e->button() != Qt::LeftButton)
        return;
    m_editor->mouseDoubleClick(makeEvent(m_editor, e->position(), e->buttons(), e->button(), e->modifiers()));
}

void CanvasItem::hoverMoveEvent(QHoverEvent* e)
{
    if (m_editor)
        m_editor->hover(makeEvent(m_editor, e->position(), Qt::NoButton, Qt::NoButton, e->modifiers()));
}

void CanvasItem::hoverLeaveEvent(QHoverEvent*)
{
    if (m_editor)
        m_editor->leave();
}

void CanvasItem::wheelEvent(QWheelEvent* e)
{
    if (!m_editor)
        return;
    const bool zoom = e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
    QPointF delta = e->pixelDelta().isNull() ? QPointF(e->angleDelta()) / 120.0 * 50.0 : QPointF(e->pixelDelta());
    if (zoom) {
        const double steps = (e->pixelDelta().isNull() ? e->angleDelta().y() / 120.0 : e->pixelDelta().y() / 50.0);
        m_editor->zoomAt(e->position(), std::pow(1.2, steps));
    } else {
        if (e->modifiers() & Qt::ShiftModifier && delta.x() == 0)
            delta = QPointF(delta.y(), 0);
        m_editor->panBy(delta);
    }
    e->accept();
}

bool CanvasItem::event(QEvent* e)
{
    if (e->type() == QEvent::NativeGesture && m_editor) {
        auto* g = static_cast<QNativeGestureEvent*>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            m_editor->zoomAt(g->position(), 1.0 + g->value());
            return true;
        }
    }
    if (e->type() == QEvent::ShortcutOverride && m_editor) {
        // Keep keys while typing on canvas so window shortcuts don't steal them.
        auto* k = static_cast<QKeyEvent*>(e);
        if (m_editor->textEditing() || !(k->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
            e->accept();
            return true;
        }
    }
    return QQuickFramebufferObject::event(e);
}

void CanvasItem::keyPressEvent(QKeyEvent* e)
{
    if (m_editor && m_editor->keyPress(e))
        e->accept();
    else
        e->ignore();
}

void CanvasItem::keyReleaseEvent(QKeyEvent* e)
{
    if (m_editor && m_editor->keyRelease(e))
        e->accept();
    else
        e->ignore();
}

void CanvasItem::inputMethodEvent(QInputMethodEvent* e)
{
    if (m_editor && m_editor->inputMethod(e))
        e->accept();
}

QVariant CanvasItem::inputMethodQuery(Qt::InputMethodQuery q) const
{
    if (q == Qt::ImEnabled)
        return m_editor && m_editor->textEditing();
    return QQuickFramebufferObject::inputMethodQuery(q);
}

} // namespace drav
