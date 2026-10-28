#include "core/Document.h"
#include "core/Geometry.h"
#include "core/Serializer.h"
#include "core/Text.h"

#include <QtTest>

using namespace drav;

class TestCore : public QObject {
    Q_OBJECT

    NodeId page(Document& d) { return d.currentPage(); }

    NodeId rect(Document& d, NodeId parent, float x, float y, float w, float h)
    {
        NodeId id = d.create(NodeType::Rectangle, parent);
        d.setPosition(id, x, y);
        d.resize(id, w, h);
        return id;
    }

private slots:
    void undoRedoProps()
    {
        Document d;
        d.begin("create");
        NodeId r = rect(d, page(d), 10, 20, 30, 40);
        d.commit();
        QCOMPARE(d.at(r).p.width, 30.f);
        d.begin("resize");
        d.resize(r, 50, 60);
        d.commit();
        QCOMPARE(d.at(r).p.width, 50.f);
        d.undo();
        QCOMPARE(d.at(r).p.width, 30.f);
        d.redo();
        QCOMPARE(d.at(r).p.height, 60.f);
        d.undo();
        d.undo();
        QVERIFY(!d.isAttached(r));
        d.redo();
        QVERIFY(d.isAttached(r));
        QCOMPARE(d.at(r).p.transform.getTranslateX(), 10.f);
    }

    void cancelReverts()
    {
        Document d;
        d.begin("a");
        NodeId r = rect(d, page(d), 0, 0, 10, 10);
        d.commit();
        d.begin("drag");
        d.setPosition(r, 100, 100);
        d.cancel();
        QCOMPARE(d.at(r).p.transform.getTranslateX(), 0.f);
        QVERIFY(!d.canRedo());
    }

    void removeAndReorder()
    {
        Document d;
        d.begin("a");
        NodeId a = rect(d, page(d), 0, 0, 10, 10);
        NodeId b = rect(d, page(d), 0, 0, 10, 10);
        NodeId c = rect(d, page(d), 0, 0, 10, 10);
        d.commit();
        d.begin("reorder");
        d.move(c, page(d), 0);
        d.commit();
        QCOMPARE(d.at(page(d)).children, (std::vector<NodeId>{c, a, b}));
        d.begin("delete");
        d.remove(a);
        d.commit();
        QCOMPARE(d.at(page(d)).children, (std::vector<NodeId>{c, b}));
        d.undo();
        QCOMPARE(d.at(page(d)).children, (std::vector<NodeId>{c, a, b}));
        d.undo();
        QCOMPARE(d.at(page(d)).children, (std::vector<NodeId>{a, b, c}));
    }

    void groupFits()
    {
        Document d;
        d.begin("g");
        NodeId g = d.create(NodeType::Group, page(d));
        NodeId a = rect(d, g, 10, 10, 10, 10);
        NodeId b = rect(d, g, 40, 30, 20, 20);
        d.commit();
        QCOMPARE(d.at(g).p.width, 50.f);
        QCOMPARE(d.at(g).p.height, 40.f);
        QCOMPARE(d.worldBounds(a).left(), 10.f);
        QCOMPARE(d.worldBounds(b).left(), 40.f);
        QCOMPARE(d.at(a).p.transform.getTranslateX(), 0.f);
    }

    void constraints()
    {
        Document d;
        d.begin("f");
        NodeId f = d.create(NodeType::Frame, page(d));
        d.resize(f, 200, 100);
        NodeId r = rect(d, f, 150, 10, 40, 10);
        d.edit(r).constraintH = ConstraintH::Right;
        NodeId s = rect(d, f, 10, 10, 180, 10);
        d.edit(s).constraintH = ConstraintH::LeftRight;
        d.commit();
        d.begin("resize");
        d.resize(f, 300, 100);
        d.commit();
        QCOMPARE(d.at(r).p.transform.getTranslateX(), 250.f);
        QCOMPARE(d.at(s).p.width, 280.f);
    }

    void autoLayoutHorizontalHug()
    {
        Document d;
        d.begin("al");
        NodeId f = d.create(NodeType::Frame, page(d));
        Props& fp = d.edit(f);
        fp.layout.mode = LayoutMode::Horizontal;
        fp.layout.spacing = 10;
        fp.layout.padL = fp.layout.padR = 5;
        fp.layout.padT = fp.layout.padB = 8;
        fp.layoutChild.sizingH = LayoutSizing::Hug;
        fp.layoutChild.sizingV = LayoutSizing::Hug;
        NodeId a = rect(d, f, 0, 0, 20, 30);
        NodeId b = rect(d, f, 0, 0, 40, 10);
        d.commit();
        QCOMPARE(d.at(f).p.width, 5 + 20 + 10 + 40 + 5.f);
        QCOMPARE(d.at(f).p.height, 8 + 30 + 8.f);
        QCOMPARE(d.at(a).p.transform.getTranslateX(), 5.f);
        QCOMPARE(d.at(b).p.transform.getTranslateX(), 35.f);
        QCOMPARE(d.at(b).p.transform.getTranslateY(), 8.f);

        d.begin("center");
        d.edit(f).layout.counterAlign = LayoutAlign::Center;
        d.commit();
        QCOMPARE(d.at(b).p.transform.getTranslateY(), 8 + 10.f);

        // Child growth propagates to the hugging parent.
        d.begin("grow");
        d.resize(a, 100, 30);
        d.commit();
        QCOMPARE(d.at(f).p.width, 5 + 100 + 10 + 40 + 5.f);
        d.undo();
        QCOMPARE(d.at(f).p.width, 5 + 20 + 10 + 40 + 5.f);
    }

    void autoLayoutFill()
    {
        Document d;
        d.begin("al");
        NodeId f = d.create(NodeType::Frame, page(d));
        d.resize(f, 300, 100);
        Props& fp = d.edit(f);
        fp.layout.mode = LayoutMode::Horizontal;
        fp.layout.spacing = 10;
        NodeId a = rect(d, f, 0, 0, 50, 20);
        NodeId b = rect(d, f, 0, 0, 50, 20);
        d.edit(b).layoutChild.sizingH = LayoutSizing::Fill;
        d.edit(b).layoutChild.sizingV = LayoutSizing::Fill;
        NodeId c = rect(d, f, 0, 0, 50, 20);
        d.edit(c).layoutChild.sizingH = LayoutSizing::Fill;
        d.commit();
        QCOMPARE(d.at(b).p.width, 115.f);
        QCOMPARE(d.at(c).p.width, 115.f);
        QCOMPARE(d.at(b).p.height, 100.f);
        QCOMPARE(d.at(c).p.transform.getTranslateX(), 50 + 10 + 115 + 10.f);
        Q_UNUSED(a);

        d.begin("space between");
        d.edit(b).layoutChild.sizingH = LayoutSizing::Fixed;
        d.edit(c).layoutChild.sizingH = LayoutSizing::Fixed;
        d.edit(f).layout.primaryAlign = LayoutAlign::SpaceBetween;
        d.commit();
        QCOMPARE(d.at(c).p.transform.getTranslateX() + d.at(c).p.width, 300.f);
    }

    void autoLayoutWrap()
    {
        Document d;
        d.begin("wrap");
        NodeId f = d.create(NodeType::Frame, page(d));
        d.resize(f, 100, 10);
        Props& fp = d.edit(f);
        fp.layout.mode = LayoutMode::Horizontal;
        fp.layout.wrap = true;
        fp.layout.spacing = 10;
        fp.layout.counterSpacing = 5;
        fp.layoutChild.sizingV = LayoutSizing::Hug;
        NodeId a = rect(d, f, 0, 0, 40, 20);
        NodeId b = rect(d, f, 0, 0, 40, 20);
        NodeId c = rect(d, f, 0, 0, 40, 20);
        d.commit();
        QCOMPARE(d.at(b).p.transform.getTranslateX(), 50.f);
        QCOMPARE(d.at(c).p.transform.getTranslateX(), 0.f);
        QCOMPARE(d.at(c).p.transform.getTranslateY(), 25.f);
        QCOMPARE(d.at(f).p.height, 45.f);
        Q_UNUSED(a);
    }

    void nestedHugText()
    {
        Document d;
        d.begin("text");
        NodeId f = d.create(NodeType::Frame, page(d));
        Props& fp = d.edit(f);
        fp.layout.mode = LayoutMode::Vertical;
        fp.layout.padL = fp.layout.padR = fp.layout.padT = fp.layout.padB = 10;
        fp.layoutChild.sizingH = LayoutSizing::Hug;
        fp.layoutChild.sizingV = LayoutSizing::Hug;
        NodeId t = d.create(NodeType::Text, f);
        d.edit(t).text = QStringLiteral("Hello world");
        d.commit();
        const float tw = d.at(t).p.width;
        QVERIFY(tw > 40);
        QCOMPARE(d.at(f).p.width, tw + 20);
        d.begin("longer");
        d.edit(t).text = QStringLiteral("Hello world, longer");
        d.commit();
        QVERIFY(d.at(t).p.width > tw);
        QCOMPARE(d.at(f).p.width, d.at(t).p.width + 20);
    }

    void instancesSyncAndOverride()
    {
        Document d;
        d.begin("component");
        NodeId comp = d.create(NodeType::Component, page(d));
        d.resize(comp, 100, 40);
        NodeId label = d.create(NodeType::Text, comp);
        d.edit(label).text = QStringLiteral("Button");
        NodeId bg = rect(d, comp, 0, 0, 100, 40);
        d.edit(bg).fills = {Paint::solid(SkColors::kBlue)};
        d.commit();

        d.begin("instance");
        Props ip;
        ip.type = NodeType::Instance;
        ip.componentId = comp;
        NodeId inst = d.create(ip, page(d));
        d.setPosition(inst, 200, 0);
        d.commit();
        QCOMPARE(d.at(inst).children.size(), size_t(2));
        NodeId iLabel = d.at(inst).children[0];
        NodeId iBg = d.at(inst).children[1];
        QCOMPARE(d.at(iLabel).p.text, QStringLiteral("Button"));
        QCOMPARE(d.at(iLabel).p.sourceId, label);
        QCOMPARE(d.at(inst).p.width, 100.f);
        QCOMPARE(d.at(inst).p.transform.getTranslateX(), 200.f);

        // Override text in the instance.
        d.begin("override");
        d.edit(iLabel).text = QStringLiteral("OK");
        d.commit();
        QVERIFY(d.at(iLabel).p.overrides & OvText);

        // Change main: color propagates, overridden text stays.
        d.begin("main edit");
        d.edit(bg).fills = {Paint::solid(SkColors::kRed)};
        d.edit(label).text = QStringLiteral("Submit");
        d.commit();
        QCOMPARE(d.at(iBg).p.fills[0].color, SkColors::kRed);
        QCOMPARE(d.at(iLabel).p.text, QStringLiteral("OK"));

        // Adding a child to main adds it to instances.
        d.begin("add");
        NodeId icon = rect(d, comp, 5, 5, 10, 10);
        d.commit();
        QCOMPARE(d.at(inst).children.size(), size_t(3));
        QCOMPARE(d.at(d.at(inst).children[2]).p.sourceId, icon);
        d.undo();
        QCOMPARE(d.at(inst).children.size(), size_t(2));
    }

    void nestedInstances()
    {
        Document d;
        d.begin("icon");
        NodeId icon = d.create(NodeType::Component, page(d));
        NodeId dot = rect(d, icon, 0, 0, 10, 10);
        d.edit(dot).fills = {Paint::solid(SkColors::kGreen)};
        NodeId button = d.create(NodeType::Component, page(d));
        Props ip;
        ip.type = NodeType::Instance;
        ip.componentId = icon;
        NodeId nested = d.create(ip, button);
        d.commit();
        d.begin("outer");
        ip.componentId = button;
        NodeId outer = d.create(ip, page(d));
        d.commit();
        QCOMPARE(d.at(outer).children.size(), size_t(1));
        NodeId oNested = d.at(outer).children[0];
        QCOMPARE(d.at(oNested).type(), NodeType::Instance);
        QCOMPARE(d.at(oNested).children.size(), size_t(1));

        d.begin("change icon");
        d.edit(dot).fills = {Paint::solid(SkColors::kRed)};
        d.commit();
        QCOMPARE(d.at(d.at(nested).children[0]).p.fills[0].color, SkColors::kRed);
        QCOMPARE(d.at(d.at(oNested).children[0]).p.fills[0].color, SkColors::kRed);
    }

    void serializeRoundTrip()
    {
        Document d;
        d.begin("x");
        NodeId f = d.create(NodeType::Frame, page(d));
        d.edit(f).name = QStringLiteral("Card");
        d.edit(f).layout.mode = LayoutMode::Vertical;
        d.edit(f).effects.push_back(Effect{});
        NodeId v = d.create(NodeType::Vector, f);
        Contour c;
        c.pts = {{{0, 0}, {0, 0}, {10, 0}}, {{20, 20}, {20, 10}, {20, 20}}};
        d.edit(v).vector.contours = {c};
        NodeId t = d.create(NodeType::Text, f);
        d.edit(t).text = QStringLiteral("héllo");
        d.commit();
        QByteArray bytes = io::save(d);
        Document e;
        QString err;
        QVERIFY2(io::load(e, bytes, &err), qPrintable(err));
        QCOMPARE(e.at(f).p, d.at(f).p);
        QCOMPARE(e.at(v).p, d.at(v).p);
        QCOMPARE(e.at(t).p, d.at(t).p);
        QCOMPARE(e.at(f).children, d.at(f).children);
        QVERIFY(!e.isDirty());
    }

    void copyPaste()
    {
        Document d;
        d.begin("x");
        NodeId g = d.create(NodeType::Group, page(d));
        rect(d, g, 0, 0, 10, 10);
        rect(d, g, 20, 0, 10, 10);
        d.commit();
        QByteArray clip = io::copy(d, {g});
        d.begin("paste");
        auto ids = io::paste(d, clip, page(d));
        d.commit();
        QCOMPARE(ids.size(), size_t(1));
        QVERIFY(ids[0] != g);
        QCOMPARE(d.at(ids[0]).children.size(), size_t(2));
    }

    void smoothRectGeometry()
    {
        SkPath p = smoothRect(100, 100, {20, 20, 20, 20}, 0.6f);
        SkRect b = p.computeTightBounds();
        QVERIFY(std::abs(b.width() - 100) < 0.01);
        QVERIFY(p.contains(50, 50));
        QVERIFY(!p.contains(1, 1));
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
