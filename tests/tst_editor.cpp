#include "Editor.h"
#include "LayerModel.h"

#include "core/Serializer.h"
#include "core/Text.h"

#include <QKeyEvent>
#include <QTemporaryDir>
#include <QtTest>
#include <QImage>
#include <QStandardPaths>

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
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    }

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

    void stylesFromSelectionAndDetach()
    {
        ed->setTool("rect");
        drag({100, 100}, {200, 200});
        NodeId a = only().id;
        ed->setProp("fills", QVariantList{QVariantMap{{"type", "SOLID"}, {"color", QColor(Qt::red)}}});
        QString sid = ed->createStyle("fills", "Brand/Red");
        QVERIFY(!sid.isEmpty());
        QCOMPARE(doc().at(a).p.fillStyle, sid);
        QCOMPARE(ed->library().value("styles").toList().size(), 1);

        ed->setTool("rect");
        drag({300, 100}, {400, 200});
        NodeId b = only().id;
        ed->applyStyle("fills", sid);
        QCOMPARE(doc().at(b).p.fills.at(0).color, (SkColor4f{1, 0, 0, 1}));

        ed->editStyle(sid, {{"paints", QVariantList{QVariantMap{{"type", "SOLID"}, {"color", QColor(Qt::blue)}}}}});
        QCOMPARE(doc().at(a).p.fills.at(0).color, (SkColor4f{0, 0, 1, 1}));
        QCOMPARE(doc().at(b).p.fills.at(0).color, (SkColor4f{0, 0, 1, 1}));

        // Editing the fill directly detaches the style.
        ed->setProp("fills", QVariantList{QVariantMap{{"type", "SOLID"}, {"color", QColor(Qt::green)}}});
        QVERIFY(doc().at(b).p.fillStyle.isEmpty());
        ed->editStyle(sid, {{"paints", QVariantList{QVariantMap{{"type", "SOLID"}, {"color", QColor(Qt::black)}}}}});
        QCOMPARE(doc().at(b).p.fills.at(0).color, (SkColor4f{0, 1, 0, 1}));

        // Deleting the style keeps the values on its users.
        ed->deleteStyle(sid);
        QVERIFY(doc().at(a).p.fillStyle.isEmpty());
        QCOMPARE(doc().at(a).p.fills.at(0).color, (SkColor4f{0, 0, 0, 1}));
        ed->undo();
        QCOMPARE(doc().at(a).p.fillStyle, sid);
    }

    void variablesBindModeAndUnbind()
    {
        QString coll = ed->addCollection("Theme");
        QVariantMap lib = ed->library();
        const QString light = lib["collections"].toList()[0].toMap()["modes"].toList()[0].toMap()["id"].toString();
        QString dark = ed->addMode(coll, "Dark");
        QString gap = ed->addVariable(coll, "FLOAT", "space/md");
        ed->setVariableValue(gap, light, 8);
        ed->setVariableValue(gap, dark, 24);
        QString ink = ed->addVariable(coll, "COLOR", "ink");
        ed->setVariableValue(ink, light, QColor(Qt::black));
        ed->setVariableValue(ink, dark, QColor(Qt::white));

        ed->setTool("frame");
        drag({100, 100}, {400, 300});
        NodeId frame = only().id;
        ed->addAutoLayout();
        ed->bindVariable("spacing", gap);
        QCOMPARE(doc().at(frame).p.layout.spacing, 8.f);
        ed->bindVariable("fills/0", ink);
        QCOMPARE(doc().at(frame).p.fills.at(0).color, SkColors::kBlack);

        ed->setVariableMode(coll, dark);
        QCOMPARE(doc().at(frame).p.layout.spacing, 24.f);
        QCOMPARE(doc().at(frame).p.fills.at(0).color, SkColors::kWhite);
        QCOMPARE(ed->resolveVariable(gap).toFloat(), 24.f);

        // Alias, then deleting the target leaves the alias with a literal value.
        QString gap2 = ed->addVariable(coll, "FLOAT", "space/alias");
        ed->setVariableAlias(gap2, dark, gap);
        ed->setVariableAlias(gap, dark, gap2); // would loop: refused
        QVERIFY(doc().library().variable(gap)->values.value(dark).alias.isEmpty());
        ed->deleteVariable(gap);
        QCOMPARE(doc().library().variable(gap2)->values.value(dark).number, 24.f);
        QVERIFY(doc().at(frame).p.bindings.isEmpty());
        QCOMPARE(doc().at(frame).p.layout.spacing, 24.f);

        // A direct edit detaches the bound color.
        QVariantList fills = ed->props().value("fills").toList();
        QVariantMap f = fills[0].toMap();
        QCOMPARE(f["colorVar"].toString(), ink);
        f["color"] = QColor(Qt::red);
        ed->setProp("fills", QVariantList{f});
        QVERIFY(doc().at(frame).p.fills.at(0).colorVar.isEmpty());
        QCOMPARE(doc().at(frame).p.fills.at(0).color, (SkColor4f{1, 0, 0, 1}));
    }

    void componentPropertiesWorkflow()
    {
        ed->setTool("rect");
        drag({0, 0}, {50, 50});
        key(Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
        NodeId comp = only().id;
        NodeId shape = doc().at(comp).children[0];
        ed->selectNode(qint64(shape), false);
        QVERIFY(ed->props().contains("linkableProps"));
        QString vis = ed->createLinkedProp("visible", "Show shape");
        QVERIFY(!vis.isEmpty());
        QCOMPARE(doc().at(comp).p.componentProps.size(), size_t(1));
        QCOMPARE(doc().at(shape).p.propRefs.value("visible"), vis);

        ed->placeComponent(qint64(comp));
        NodeId inst = only().id;
        QVariantList ip = ed->props().value("instanceProps").toList();
        QCOMPARE(ip.size(), 1);
        QCOMPARE(ip[0].toMap().value("value").toString(), QStringLiteral("true"));
        ed->setInstanceProp(vis, false);
        QVERIFY(!doc().at(doc().at(inst).children[0]).p.visible);
        QVERIFY(doc().at(shape).p.visible);
        ed->undo();
        QVERIFY(doc().at(doc().at(inst).children[0]).p.visible);

        ed->selectNode(qint64(comp), false);
        ed->deleteComponentProp(vis);
        QVERIFY(doc().at(shape).p.propRefs.isEmpty());
        QVERIFY(doc().at(comp).p.componentProps.empty());
    }

    void richTextEditing()
    {
        ed->setTool("text");
        click({100, 100});
        type("Hello world");
        // Select "world" with Shift+Left and make it bold through the inspector.
        for (int i = 0; i < 5; ++i)
            key(Qt::Key_Left, Qt::ShiftModifier);
        ed->setProp("fontWeight", 700);
        NodeId t = doc().selection().at(0);
        QCOMPARE(doc().at(t).p.textRuns.size(), size_t(1));
        QCOMPARE(doc().at(t).p.textRuns[0].start, 6);
        QCOMPARE(ed->props().value("fontWeight").toInt(), 700);
        key(Qt::Key_I, Qt::ControlModifier);
        QVERIFY(text::styleAt(doc().at(t).p, 8).italic);
        QVERIFY(!text::styleAt(doc().at(t).p, 2).italic);

        // Lists: Enter continues, Enter on an empty item ends the list.
        key(Qt::Key_End);
        ed->setProp("listType", "BULLET");
        key(Qt::Key_Return);
        QCOMPARE(doc().at(t).p.paragraphs.size(), size_t(2));
        QCOMPARE(doc().at(t).p.paragraphs[1].list, ListType::Bullet);
        key(Qt::Key_Tab);
        QCOMPARE(doc().at(t).p.paragraphs[1].indent, 1);
        key(Qt::Key_Return);
        QVERIFY(doc().at(t).p.paragraphs.size() < 2 || doc().at(t).p.paragraphs[1].list == ListType::None);

        // Leaving edit mode and setting the weight on the layer clears per-character weights.
        key(Qt::Key_Escape);
        ed->setProp("fontWeight", 300);
        for (const TextRun& r : doc().at(t).p.textRuns)
            QVERIFY(!(r.mask & RunWeight));
        QVERIFY(text::styleAt(doc().at(t).p, 8).italic);
    }

    void demoDuplicateKeepsPropValues()
    {
        ed->loadDemo();
        NodeId dark = kNoNode;
        doc().walk(doc().currentPage(), [&](const Node& n) {
            if (n.p.name == QStringLiteral("Card (dark)"))
                dark = n.id;
            return !dark;
        });
        QVERIFY(dark);
        QStringList labels;
        doc().walk(dark, [&](const Node& n) {
            if (n.type() == NodeType::Instance)
                labels << QString::number(n.p.propValues.size()) + ":" + doc().at(n.children.front()).p.text;
            return true;
        });
        QCOMPARE(labels, (QStringList{"0:Get started", "1:Learn more"}));
    }

    void rulerGuidesAndSnapping()
    {
        ed->setRulers(true);
        // Drag from the left ruler: vertical guide at x = 303.
        drag({5, 300}, {303, 300});
        const auto& guides = doc().at(doc().currentPage()).p.guides;
        QCOMPARE(guides.size(), size_t(1));
        QVERIFY(guides[0].vertical);
        QCOMPARE(guides[0].pos, 303.f);

        // A moved rectangle snaps its left edge to the guide.
        ed->setTool("rect");
        drag({100, 100}, {200, 200});
        drag({150, 150}, {350, 150});
        QCOMPARE(doc().worldBounds(only().id).left(), 303.f);

        // Dragging the guide back onto the ruler removes it; undo brings it back.
        drag({303, 500}, {5, 500});
        QCOMPARE(doc().at(doc().currentPage()).p.guides.size(), size_t(0));
        ed->undo();
        QCOMPARE(doc().at(doc().currentPage()).p.guides.size(), size_t(1));
    }

    void scaleSelectionAndTool()
    {
        ed->setTool("frame");
        drag({100, 100}, {200, 150});
        NodeId frame = only().id;
        ed->setProp("strokes", QVariantList{QVariantMap{{"type", "SOLID"}, {"color", QColor(Qt::black)}}});
        ed->setProp("strokeWeight", 2);
        ed->setProp("cornerRadius", 4);
        ed->scaleSelection(2);
        const Props& p = doc().at(frame).p;
        QCOMPARE(p.width, 200.f);
        QCOMPARE(p.height, 100.f);
        QCOMPARE(p.stroke.weight, 4.f);
        QCOMPARE(p.corners.tl, 8.f);
        QCOMPARE(doc().worldBounds(frame).left(), 100.f);

        // Scale tool: drag the bottom-right handle (300, 200) to (400, 250) = 150%.
        ed->setTool("scale");
        drag({300, 200}, {400, 250});
        QCOMPARE(doc().at(frame).p.width, 300.f);
        QCOMPARE(doc().at(frame).p.stroke.weight, 6.f);
        ed->undo();
        QCOMPARE(doc().at(frame).p.width, 200.f);
    }

    void eyedropperFillsSelection()
    {
        ed->setTool("rect");
        drag({100, 100}, {200, 200});
        ed->setProp("fills", QVariantList{QVariantMap{{"type", "SOLID"}, {"color", QColor(Qt::red)}}});
        ed->setTool("rect");
        drag({300, 100}, {400, 200});
        NodeId b = only().id;
        key(Qt::Key_I);
        QCOMPARE(ed->toolName(), QStringLiteral("eyedropper"));
        click({150, 150});
        QCOMPARE(ed->toolName(), QStringLiteral("select"));
        const SkColor4f c = doc().at(b).p.fills.at(0).color;
        QVERIFY(c.fR > 0.99f && c.fG < 0.01f && c.fB < 0.01f);
    }

    void selectSameFindReplaceRename()
    {
        for (int i = 0; i < 3; ++i) {
            ed->setTool("rect");
            drag({100.0 + i * 150, 100}, {200.0 + i * 150, 200});
            if (i < 2)
                ed->setProp("fills", QVariantList{QVariantMap{{"type", "SOLID"}, {"color", QColor(Qt::red)}}});
        }
        ed->selectNode(qint64(doc().at(doc().currentPage()).children[0]), false);
        ed->selectSame("fill");
        QCOMPARE(doc().selection().size(), size_t(2));

        ed->renameLayers("Card $n", "", "", 1);
        QStringList names;
        for (NodeId id : doc().at(doc().currentPage()).children)
            names << doc().at(id).p.name;
        QVERIFY(names.contains("Card 1") && names.contains("Card 2"));

        ed->setTool("text");
        click({100, 400});
        type("Buy now. Buy later.");
        key(Qt::Key_Escape);
        QCOMPARE(ed->findLayers("buy", false, false).size(), 1);
        QCOMPARE(ed->replaceText("Buy", "Get", true, false), 2);
        QCOMPARE(only().p.text, QStringLiteral("Get now. Get later."));
        QCOMPARE(ed->replaceText("Card", "Tile", true, true), 2);

        auto* layers = static_cast<LayerModel*>(ed->layersObject());
        layers->setFilter("tile");
        QCOMPARE(layers->rowCount(), 2);
        layers->setFilter("");
    }

    void autosaveRecoveryVersionsRecent()
    {
        ed->setTool("rect");
        drag({100, 100}, {200, 200});
        ed->autosaveNow();
        {
            // A later session finds the unsaved work and recovers it.
            Editor other;
            other.newDocument();
            const QVariantList files = other.recoveryFiles();
            QCOMPARE(files.size(), 1);
            QVERIFY(other.recover(files[0].toMap().value("path").toString()));
            QCOMPARE(other.doc().at(other.doc().currentPage()).children.size(), size_t(1));
            QVERIFY(other.isDirty());
            other.discardChanges();
        }
        ed->discardChanges();

        QTemporaryDir tmp;
        const QString path = tmp.filePath("v.drav");
        QVERIFY(ed->saveAs(QUrl::fromLocalFile(path)));
        QCOMPARE(ed->recentFiles().value(0), QFileInfo(path).absoluteFilePath());
        QCOMPARE(ed->versions().size(), 1); // automatic version on first save
        QVERIFY(ed->saveVersion("Before more shapes"));
        ed->setTool("rect");
        drag({300, 100}, {400, 200});
        QCOMPARE(doc().at(doc().currentPage()).children.size(), size_t(2));
        const QVariantList vs = ed->versions();
        QCOMPARE(vs[0].toMap().value("name").toString(), QStringLiteral("Before more shapes"));
        QVERIFY(ed->restoreVersion(vs[0].toMap().value("path").toString()));
        QCOMPARE(doc().at(doc().currentPage()).children.size(), size_t(1));
        QVERIFY(ed->isDirty());
        QCOMPARE(ed->versions().value(0).toMap().value("name").toString(), QStringLiteral("Before restore"));
        ed->discardChanges();
    }

    void inspectCss()
    {
        ed->setTool("frame");
        drag({100, 100}, {300, 200});
        ed->addAutoLayout();
        ed->setProp("cornerRadius", 8);
        ed->setProp("fills", QVariantList{QVariantMap{{"type", "SOLID"}, {"color", QColor(Qt::red)}}});
        const QString css = ed->inspectCode();
        QVERIFY(css.contains("display: flex;"));
        QVERIFY(css.contains("border-radius: 8px;"));
        QVERIFY(css.contains("background: #FF0000;"));
        QString coll = ed->addCollection("Theme");
        QString v = ed->addVariable(coll, "COLOR", "Brand/Primary");
        ed->bindVariable("fills/0", v);
        QVERIFY(ed->inspectCode().contains("var(--brand-primary, #FFFFFF)"));
    }

    void imageAdjustAndCrop()
    {
        // A 4x2 image: left half red, right half blue.
        QImage img(4, 2, QImage::Format_ARGB32);
        for (int x = 0; x < 4; ++x)
            for (int y = 0; y < 2; ++y)
                img.setPixelColor(x, y, x < 2 ? Qt::red : Qt::blue);
        QTemporaryDir tmp;
        const QString path = tmp.filePath("img.png");
        img.save(path);
        QVERIFY(ed->importFile(QUrl::fromLocalFile(path)));
        NodeId id = only().id;
        ed->setProp("w", 100);
        ed->setProp("h", 100);
        // Fill framing of a 2:1 image in a square shows the middle half.
        ed->enterCrop();
        QCOMPARE(ed->toolName(), QStringLiteral("crop"));
        const Paint& f = doc().at(id).p.fills.at(0);
        QCOMPARE(f.scaleMode, ImageScaleMode::Crop);
        QVERIFY(std::abs(f.crop.left() - 0.25f) < 1e-4f && std::abs(f.crop.width() - 0.5f) < 1e-4f);
        key(Qt::Key_Escape);

        // Adjustments survive save/load and change the rendered color.
        QVariantList fills = ed->props().value("fills").toList();
        QVariantMap m = fills[0].toMap();
        m["saturation"] = -1.0;
        ed->setProp("fills", QVariantList{m});
        QCOMPARE(doc().at(id).p.fills[0].saturation, -1.f);
        Document d2;
        QVERIFY(io::load(d2, io::save(doc())));
        QCOMPARE(d2.at(id).p.fills[0].saturation, -1.f);
        QCOMPARE(d2.at(id).p.fills[0].crop, doc().at(id).p.fills[0].crop);
        const SkPoint centerLeft = doc().worldTransform(id).mapXY(10, 50);
        const SkColor4f c = ed->sampleColor(centerLeft);
        QVERIFY(std::abs(c.fR - c.fG) < 0.05f && std::abs(c.fG - c.fB) < 0.05f); // grayscale
    }

    void sectionsAdoptAndLabel()
    {
        ed->setTool("frame");
        drag({100, 100}, {200, 200});
        ed->setTool("frame");
        drag({300, 100}, {400, 200});
        ed->setTool("rect");
        drag({120, 120}, {150, 150}); // inside the first frame
        key(Qt::Key_S, Qt::ShiftModifier);
        QCOMPARE(ed->toolName(), QStringLiteral("section"));
        drag({50, 50}, {450, 250});
        const Node& sec = only();
        QCOMPARE(sec.type(), NodeType::Section);
        QCOMPARE(sec.children.size(), size_t(2));
        QCOMPARE(doc().at(doc().currentPage()).children.size(), size_t(1));
        // Frames inside keep their place on the canvas.
        QCOMPARE(doc().worldBounds(sec.children[0]).left(), 100.f);
        // The section's name pill selects it; clicking a frame inside selects its content, like top level.
        click({50, 50}); // deselect
        click({60, 60});
        QCOMPARE(only().type(), NodeType::Section);
        click({130, 130});
        QCOMPARE(only().type(), NodeType::Rectangle);
        Document d2;
        QVERIFY(io::load(d2, io::save(doc())));
        QCOMPARE(d2.at(sec.id).type(), NodeType::Section);
    }

    void linkedLibraries()
    {
        QTemporaryDir tmp;
        const QString libPath = tmp.filePath("ds.drav");
        NodeId srcIcon, srcButton;
        QString styleId, varId;
        {
            // The library: a color style, a variable, an icon component used inside a button component.
            Editor lib;
            lib.newDocument();
            Document& d = lib.doc();
            d.begin("lib");
            Style st;
            st.id = Library::newId();
            st.name = "Brand";
            st.paints = {Paint::solid(SkColors::kBlue)};
            d.editLibrary().styles.push_back(st);
            styleId = st.id;
            QString coll = Library::newId(), mode = Library::newId();
            d.editLibrary().collections.push_back({coll, "Tokens", {{mode, "Default"}}});
            Variable v;
            v.id = varId = Library::newId();
            v.name = "radius";
            v.collection = coll;
            v.type = VarType::Number;
            VarValue r;
            r.number = 12;
            v.values = {{mode, r}};
            d.editLibrary().variables.push_back(v);
            srcIcon = d.create(NodeType::Component, d.currentPage());
            d.edit(srcIcon).name = "Icon";
            d.create(NodeType::Ellipse, srcIcon);
            srcButton = d.create(NodeType::Component, d.currentPage());
            d.edit(srcButton).name = "Button";
            d.edit(srcButton).fillStyle = st.id;
            d.edit(srcButton).bindings.insert("cornerRadius", v.id);
            Props ip;
            ip.type = NodeType::Instance;
            ip.componentId = srcIcon;
            d.create(ip, srcButton);
            d.commit();
            QVERIFY(lib.saveAs(QUrl::fromLocalFile(libPath)));
        }

        QVERIFY(ed->addLibrary(QUrl::fromLocalFile(libPath)));
        QCOMPARE(doc().library().style(styleId)->remote.isEmpty(), false);
        QVERIFY(doc().library().variable(varId));
        QVariantList libs = ed->libraries();
        QCOMPARE(libs.size(), 1);
        QCOMPARE(libs[0].toMap()["components"].toList().size(), 2);
        const QString libId = libs[0].toMap()["id"].toString();

        ed->placeLibraryComponent(libId, qint64(srcButton));
        const Node& inst = only();
        QCOMPARE(inst.type(), NodeType::Instance);
        QCOMPARE(inst.p.fills.at(0).color, SkColors::kBlue);
        QCOMPARE(inst.p.corners.tl, 12.f);
        QCOMPARE(inst.children.size(), size_t(1)); // the nested icon instance
        QCOMPARE(ed->pages().size(), 1);           // the library page stays hidden
        const NodeId instId = inst.id;

        // Edit the library file: the style turns red, the button gets a second child.
        {
            Editor lib;
            QVERIFY(lib.open(QUrl::fromLocalFile(libPath)));
            Document& d = lib.doc();
            d.begin("edit");
            d.editLibrary().style(styleId)->paints = {Paint::solid(SkColors::kRed)};
            d.create(NodeType::Rectangle, srcButton);
            d.commit();
            QVERIFY(lib.save());
        }
        ed->refreshLibraries();
        QVERIFY(ed->libraries()[0].toMap()["updateAvailable"].toBool());
        ed->updateLibrary(libId);
        QVERIFY(!ed->libraries()[0].toMap()["updateAvailable"].toBool());
        QCOMPARE(doc().at(instId).p.fills.at(0).color, SkColors::kRed);
        QCOMPARE(doc().at(instId).children.size(), size_t(2));

        // Library assets are read-only here.
        ed->editStyle(styleId, {{"name", "Renamed"}});
        QCOMPARE(doc().library().style(styleId)->name, QStringLiteral("Brand"));

        ed->removeLibrary(libId);
        QVERIFY(doc().library().style(styleId)->remote.isEmpty());
        QCOMPARE(ed->pages().size(), 2); // imported components now on a visible page
    }

    void vectorNetworkEditing()
    {
        // Open path A-B-C with the pen.
        ed->setTool("pen");
        click({100, 100});
        click({200, 100});
        click({200, 200});
        key(Qt::Key_Escape);
        NodeId id = only().id;
        QCOMPARE(only().type(), NodeType::Vector);
        ed->setProp("fills", QVariantList{QVariantMap{{"type", "SOLID"}, {"color", QColor(Qt::red)}}});
        key(Qt::Key_Return); // edit points
        QCOMPARE(ed->toolName(), QStringLiteral("vectorEdit"));

        // Pen inside the vector: from C back to A closes the triangle and fills it, then branch A -> D.
        key(Qt::Key_P);
        click({200, 200});
        click({100, 100});
        click({100, 200});
        const VectorData& v = doc().at(id).p.vector;
        QVERIFY(v.isNetwork());
        QCOMPARE(v.vertices.size(), size_t(4));
        QCOMPARE(v.segments.size(), size_t(4));
        int a = -1;
        for (int i = 0; i < int(v.vertices.size()); ++i)
            if (doc().worldTransform(id).mapPoint(v.vertices[i].p) == SkPoint{100, 100})
                a = i;
        QVERIFY(a >= 0);
        QCOMPARE(v.segmentsAt(a).size(), size_t(3));
        QCOMPARE(v.regions.size(), size_t(1));
        QCOMPARE(ed->hitDeepest({180, 120}), id); // inside the filled triangle
        QCOMPARE(ed->hitDeepest({100, 150}), id); // on the open branch

        // The paint bucket clears the triangle's fill.
        key(Qt::Key_Escape);
        key(Qt::Key_B);
        click({180, 120});
        QVERIFY(doc().at(id).p.vector.regions.empty());
        ed->undo();
        QCOMPARE(doc().at(id).p.vector.regions.size(), size_t(1));

        // Clicking a segment adds a point.
        QCOMPARE(ed->toolName(), QStringLiteral("select")); // undo leaves point editing
        key(Qt::Key_Return);
        click({150, 100});
        QCOMPARE(doc().at(id).p.vector.vertices.size(), size_t(5));
        key(Qt::Key_Return);
        QCOMPARE(ed->toolName(), QStringLiteral("select"));
    }

    void openFigFile()
    {
        QVERIFY(ed->open(QUrl::fromLocalFile(QStringLiteral(DRAV_TEST_DATA "/sample.fig"))));
        QCOMPARE(ed->pages().size(), 1);
        QCOMPARE(ed->pages()[0].toMap()["name"].toString(), QStringLiteral("Page 1"));
        QVERIFY(!doc().at(doc().currentPage()).children.empty());
        ed->undo(); // one step: back to the empty document
        QCOMPARE(ed->pages()[0].toMap()["name"].toString(), QStringLiteral("Page 1"));
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
