#include "Editor.h"
#include "LayerModel.h"

#include "core/Serializer.h"

#include <QKeyEvent>
#include <QTemporaryDir>
#include <QtTest>

using namespace drav;

// Drives the editor the way the canvas item does: screen-space mouse events and key presses.
class TestEditor : public QObject {
    Q_OBJECT

    std::unique_ptr<Editor> ed;

    InputEvent ev(QPointF screen, Qt::MouseButtons buttons, Qt::MouseButton button = Qt::NoButton,
        Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        InputEvent e;
        e.screen = screen;
        e.page = ed->toPage(screen);
        e.buttons = buttons;
        e.button = button;
        e.mods = mods;
        return e;
    }
    void drag(QPointF a, QPointF b, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        ed->mousePress(ev(a, Qt::LeftButton, Qt::LeftButton, mods));
        for (int i = 1; i <= 8; ++i)
            ed->mouseMove(ev(a + (b - a) * (i / 8.0), Qt::LeftButton, Qt::NoButton, mods));
        ed->mouseRelease(ev(b, Qt::NoButton, Qt::LeftButton, mods));
    }
    void click(QPointF p, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        ed->mousePress(ev(p, Qt::LeftButton, Qt::LeftButton, mods));
        ed->mouseRelease(ev(p, Qt::NoButton, Qt::LeftButton, mods));
    }
    void key(int k, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString& text = {})
    {
        QKeyEvent e(QEvent::KeyPress, k, mods, text);
        ed->keyPress(&e);
    }
    void type(const QString& s)
    {
        for (QChar c : s)
            key(0, Qt::NoModifier, QString(c));
    }
    Document& doc() { return ed->doc(); }
    const Node& only() { return doc().at(doc().selection().at(0)); }

private slots:
    void init()
    {
        ed = std::make_unique<Editor>();
        ed->setViewportSize({1000, 800});
        ed->newDocument();
        // Identity view: page == screen.
        ed->panBy(-QPointF(ed->toScreen({0, 0})));
        QCOMPARE(ed->toPage({10, 20}).x(), 10.f);
    }

    void drawRectMoveResizeUndo()
    {
        ed->setTool("rect");
        drag({100, 100}, {300, 200});
        QCOMPARE(ed->toolName(), QStringLiteral("select"));
        QCOMPARE(doc().selection().size(), size_t(1));
        QCOMPARE(only().type(), NodeType::Rectangle);
        QCOMPARE(only().p.width, 200.f);
        QCOMPARE(only().p.height, 100.f);

        drag({200, 150}, {250, 170});
        QCOMPARE(doc().worldBounds(only().id).left(), 150.f);
        QCOMPARE(doc().worldBounds(only().id).top(), 120.f);

        // Bottom-right handle at (350, 220).
        drag({350, 220}, {400, 260});
        QCOMPARE(only().p.width, 250.f);
        QCOMPARE(only().p.height, 140.f);

        ed->undo();
        QCOMPARE(only().p.width, 200.f);
        ed->undo();
        QCOMPARE(doc().worldBounds(only().id).left(), 100.f);
        ed->undo();
        QVERIFY(doc().at(doc().currentPage()).children.empty());
    }

    void shiftDrawsSquareAndClickCreatesDefault()
    {
        ed->setTool("ellipse");
        drag({0, 0}, {80, 40}, Qt::ShiftModifier);
        QCOMPARE(only().p.width, 80.f);
        QCOMPARE(only().p.height, 80.f);
        ed->setTool("rect");
        click({500, 500});
        QCOMPARE(only().p.width, 100.f);
    }

    void marqueeGroupAndUngroup()
    {
        ed->setTool("rect");
        drag({100, 100}, {150, 150});
        ed->setTool("rect");
        drag({200, 100}, {250, 150});
        click({600, 600});
        QVERIFY(doc().selection().empty());
        drag({50, 50}, {300, 300});
        QCOMPARE(doc().selection().size(), size_t(2));
        key(Qt::Key_G, Qt::ControlModifier);
        QCOMPARE(only().type(), NodeType::Group);
        QCOMPARE(only().p.width, 150.f);
        key(Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(doc().selection().size(), size_t(2));
        QCOMPARE(doc().at(doc().currentPage()).children.size(), size_t(2));
    }

    void drawIntoFrameReparents()
    {
        ed->setTool("frame");
        drag({100, 100}, {500, 400});
        NodeId frame = only().id;
        ed->setTool("rect");
        drag({150, 150}, {200, 200});
        QCOMPARE(only().parent, frame);
        // Drag the rect out of the frame: it moves to the page.
        drag({175, 175}, {700, 600});
        QCOMPARE(only().parent, doc().currentPage());
        QCOMPARE(doc().worldBounds(only().id).left(), 675.f);
    }

    void typeTextAndUndo()
    {
        ed->setTool("text");
        click({100, 100});
        QVERIFY(ed->textEditing());
        type("Hello");
        key(Qt::Key_Backspace);
        type("o!");
        NodeId t = doc().selection().at(0);
        QCOMPARE(doc().at(t).p.text, QStringLiteral("Hello!"));
        key(Qt::Key_Escape);
        QVERIFY(!ed->textEditing());
        QVERIFY(doc().at(t).p.width > 20);
        // Typing is one undo step together with the creation.
        ed->undo();
        QVERIFY(!doc().isAttached(t));
    }

    void emptyTextIsRemoved()
    {
        ed->setTool("text");
        click({100, 100});
        key(Qt::Key_Escape);
        QVERIFY(doc().at(doc().currentPage()).children.empty());
    }

    void penToolBuildsPath()
    {
        ed->setTool("pen");
        click({100, 100});
        drag({200, 100}, {250, 150}); // smooth point
        click({200, 200});
        click({100, 100}); // close
        QCOMPARE(ed->toolName(), QStringLiteral("select"));
        const Node& v = only();
        QCOMPARE(v.type(), NodeType::Vector);
        QCOMPARE(v.p.vector.contours.size(), size_t(1));
        QCOMPARE(v.p.vector.contours[0].pts.size(), size_t(3));
        QVERIFY(v.p.vector.contours[0].closed);
        QCOMPARE(v.p.vector.contours[0].pts[1].mode, AnchorMode::Mirrored);
        QCOMPARE(doc().worldBounds(v.id).left(), 100.f);
    }

    void copyPasteDuplicateDelete()
    {
        ed->setTool("rect");
        drag({10, 10}, {60, 60});
        ed->copy();
        ed->paste();
        QCOMPARE(doc().at(doc().currentPage()).children.size(), size_t(2));
        key(Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(doc().at(doc().currentPage()).children.size(), size_t(3));
        key(Qt::Key_Delete);
        QCOMPARE(doc().at(doc().currentPage()).children.size(), size_t(2));
    }

    void autoLayoutShortcutAndProps()
    {
        ed->setTool("rect");
        drag({0, 0}, {40, 40});
        ed->setTool("rect");
        drag({60, 0}, {100, 40});
        drag({-10, -10}, {200, 100}); // marquee both
        key(Qt::Key_A, Qt::ShiftModifier);
        const Node& f = only();
        QCOMPARE(f.p.layout.mode, LayoutMode::Horizontal);
        QCOMPARE(f.p.layout.spacing, 20.f);
        ed->setProp("spacing", 5);
        QCOMPARE(only().p.width, 85.f);
        ed->setProp("layoutMode", "VERTICAL");
        QCOMPARE(only().p.height, 85.f);
        QVariantMap p = ed->props();
        QCOMPARE(p.value("layoutMode").toString(), QStringLiteral("VERTICAL"));
        QCOMPARE(p.value("sizingH").toString(), QStringLiteral("HUG"));
    }

    void componentInstanceAndOverride()
    {
        ed->setTool("rect");
        drag({0, 0}, {50, 50});
        ed->setProp("fills", QVariantList{QVariantMap{{"type", "SOLID"}, {"color", QColor(Qt::red)}}});
        key(Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
        NodeId comp = only().id;
        QCOMPARE(only().type(), NodeType::Component);
        ed->placeComponent(qint64(comp));
        NodeId inst = only().id;
        QCOMPARE(only().type(), NodeType::Instance);
        QCOMPARE(ed->components().size(), 1);
        // Edit main: instance follows.
        ed->selectNode(qint64(doc().at(comp).children[0]), false);
        ed->setProp("opacity", 0.5);
        QCOMPARE(doc().at(doc().at(inst).children[0]).p.opacity, 0.5f);
        QCOMPARE(ed->props().value("opacity").toFloat(), 0.5f);
        ed->selectNode(qint64(inst), false);
        QCOMPARE(ed->props().value("isInstance").toBool(), true);
        key(Qt::Key_B, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(doc().at(inst).type(), NodeType::Frame);
    }

    void booleanAndFlatten()
    {
        ed->setTool("rect");
        drag({0, 0}, {100, 100});
        ed->setTool("ellipse");
        drag({50, 50}, {150, 150});
        drag({-10, -10}, {200, 200});
        ed->booleanOp("subtract");
        QCOMPARE(only().type(), NodeType::BooleanOp);
        key(Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(only().type(), NodeType::Vector);
        QCOMPARE(only().p.width, 100.f);
    }

    void layersModelTracksTree()
    {
        ed->setTool("frame");
        drag({0, 0}, {300, 300});
        ed->setTool("rect");
        drag({10, 10}, {50, 50});
        auto* m = qobject_cast<LayerModel*>(ed->layersObject());
        QCOMPARE(m->rowCount(), 2); // frame expanded because its child is selected
        QCOMPARE(m->data(m->index(1), LayerModel::DepthRole).toInt(), 1);
        QCOMPARE(m->data(m->index(1), LayerModel::SelectedRole).toBool(), true);
    }

    void saveAndOpen()
    {
        ed->loadDemo();
        QTemporaryDir dir;
        const QUrl url = QUrl::fromLocalFile(dir.filePath("demo.drav"));
        QVERIFY(ed->saveAs(url));
        QVERIFY(!ed->isDirty());
        const QByteArray before = io::save(doc());
        ed->newDocument();
        QVERIFY(ed->open(url));
        QCOMPARE(io::save(doc()).size(), before.size());
    }

    void svgImport()
    {
        QTemporaryDir dir;
        QFile f(dir.filePath("icon.svg"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\">"
            "<defs><linearGradient id=\"g\"><stop offset=\"0\" stop-color=\"#f00\"/><stop offset=\"1\" stop-color=\"blue\"/></linearGradient></defs>"
            "<g transform=\"translate(2,2)\"><rect width=\"10\" height=\"10\" rx=\"2\" fill=\"url(#g)\"/>"
            "<circle cx=\"15\" cy=\"15\" r=\"4\" fill=\"none\" stroke=\"#000\" stroke-width=\"2\"/></g>"
            "<path d=\"M0 0 L24 24\" stroke=\"red\"/></svg>");
        f.close();
        QVERIFY(ed->importFile(QUrl::fromLocalFile(f.fileName())));
        const Node& frame = only();
        QCOMPARE(frame.type(), NodeType::Frame);
        QCOMPARE(frame.p.width, 24.f);
        QCOMPARE(frame.children.size(), size_t(2));
        const Node& g = doc().at(frame.children[0]);
        QCOMPARE(g.type(), NodeType::Group);
        QCOMPARE(doc().at(g.children[0]).p.fills[0].type, PaintType::LinearGradient);
    }
};

QTEST_MAIN(TestEditor)
#include "tst_editor.moc"
