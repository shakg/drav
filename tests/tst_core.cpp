#include "core/Document.h"
#include "core/FigImport.h"
#include "core/Geometry.h"
#include "core/Serializer.h"
#include "core/Text.h"
#include "core/Variables.h"

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

    // Library with one color collection (Light/Dark) and a few variables.
    struct Vars {
        QString coll, light, dark, bg, brand, gap, title, shown;
    };
    Vars makeVars(Document& d)
    {
        Vars v;
        Library& lib = d.editLibrary();
        v.coll = Library::newId();
        v.light = Library::newId();
        v.dark = Library::newId();
        lib.collections.push_back({v.coll, QStringLiteral("Theme"), {{v.light, QStringLiteral("Light")}, {v.dark, QStringLiteral("Dark")}}});
        auto add = [&](VarType t, const QString& name, VarValue a, VarValue b) {
            Variable x;
            x.id = Library::newId();
            x.name = name;
            x.collection = v.coll;
            x.type = t;
            x.values = {{v.light, a}, {v.dark, b}};
            lib.variables.push_back(x);
            return x.id;
        };
        VarValue white, black;
        white.color = SkColors::kWhite;
        black.color = SkColors::kBlack;
        v.bg = add(VarType::Color, QStringLiteral("bg"), white, black);
        VarValue alias;
        alias.alias = v.bg;
        v.brand = add(VarType::Color, QStringLiteral("brand"), alias, alias);
        VarValue n8, n16;
        n8.number = 8;
        n16.number = 16;
        v.gap = add(VarType::Number, QStringLiteral("gap"), n8, n16);
        VarValue s1, s2;
        s1.string = QStringLiteral("Hello");
        s2.string = QStringLiteral("Hola");
        v.title = add(VarType::String, QStringLiteral("title"), s1, s2);
        VarValue t, f;
        t.boolean = true;
        f.boolean = false;
        v.shown = add(VarType::Boolean, QStringLiteral("shown"), t, f);
        return v;
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

    void instanceInAutoLayoutRow()
    {
        // Hugging auto-layout component instanced twice inside an auto-layout row.
        Document d;
        d.begin("x");
        Props b;
        b.type = NodeType::Component;
        b.layout.mode = LayoutMode::Horizontal;
        b.layout.padL = b.layout.padR = 20;
        b.layout.padT = b.layout.padB = 12;
        b.layoutChild.sizingH = b.layoutChild.sizingV = LayoutSizing::Hug;
        b.fills = {Paint::solid(SkColors::kBlue)};
        NodeId comp = d.create(b, page(d));
        Props t;
        t.type = NodeType::Text;
        t.text = QStringLiteral("Get started");
        d.create(t, comp);
        Props row;
        row.type = NodeType::Frame;
        row.layout.mode = LayoutMode::Horizontal;
        row.layout.spacing = 10;
        row.layoutChild.sizingH = row.layoutChild.sizingV = LayoutSizing::Hug;
        NodeId rowId = d.create(row, page(d));
        Props ip;
        ip.type = NodeType::Instance;
        ip.componentId = comp;
        NodeId i1 = d.create(ip, rowId);
        NodeId i2 = d.create(ip, rowId);
        d.updateDerived();
        d.edit(d.at(i2).children.front()).text = QStringLiteral("Learn more");
        d.commit();
        const float cw = d.at(comp).p.width;
        QVERIFY(cw > 60);
        QCOMPARE(d.at(i1).p.width, cw);
        QCOMPARE(d.at(i1).p.fills.size(), size_t(1));
        QVERIFY(d.at(i2).p.width > cw);
        QCOMPARE(d.at(i2).p.transform.getTranslateX(), cw + 10);
        QCOMPARE(d.at(rowId).p.width, cw + 10 + d.at(i2).p.width);
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

    void fillStyleAppliesAndPropagates()
    {
        Document d;
        d.begin("style");
        Style st;
        st.id = Library::newId();
        st.name = QStringLiteral("Brand/Primary");
        st.paints = {Paint::solid(SkColors::kRed)};
        d.editLibrary().styles.push_back(st);
        NodeId a = rect(d, page(d), 0, 0, 10, 10);
        NodeId b = rect(d, page(d), 20, 0, 10, 10);
        d.edit(a).fillStyle = st.id;
        d.edit(b).strokeStyle = st.id;
        d.commit();
        QCOMPARE(d.at(a).p.fills.size(), size_t(1));
        QCOMPARE(d.at(a).p.fills[0].color, SkColors::kRed);
        QCOMPARE(d.at(b).p.strokes[0].color, SkColors::kRed);

        d.begin("edit style");
        d.editLibrary().style(st.id)->paints = {Paint::solid(SkColors::kGreen)};
        d.commit();
        QCOMPARE(d.at(a).p.fills[0].color, SkColors::kGreen);
        QCOMPARE(d.at(b).p.strokes[0].color, SkColors::kGreen);
        d.undo();
        QCOMPARE(d.at(a).p.fills[0].color, SkColors::kRed);
        QCOMPARE(d.library().style(st.id)->paints[0].color, SkColors::kRed);
    }

    void variablesResolveByMode()
    {
        Document d;
        d.begin("setup");
        Vars v = makeVars(d);
        NodeId frame = d.create(NodeType::Frame, page(d));
        d.resize(frame, 200, 200);
        NodeId r = rect(d, frame, 0, 0, 10, 10);
        Paint fill = Paint::solid(SkColors::kRed);
        fill.colorVar = v.brand; // alias of bg
        d.edit(r).fills = {fill};
        d.edit(r).bindings.insert(QStringLiteral("visible"), v.shown);
        NodeId t = d.create(NodeType::Text, frame);
        d.edit(t).bindings.insert(QStringLiteral("characters"), v.title);
        d.commit();
        QCOMPARE(d.at(r).p.fills[0].color, SkColors::kWhite);
        QCOMPARE(d.at(t).p.text, QStringLiteral("Hello"));
        QVERIFY(d.at(r).p.visible);

        d.begin("dark");
        d.edit(frame).variableModes.insert(v.coll, v.dark);
        d.commit();
        QCOMPARE(d.at(r).p.fills[0].color, SkColors::kBlack);
        QCOMPARE(d.at(t).p.text, QStringLiteral("Hola"));
        QVERIFY(!d.at(r).p.visible);

        // Moving out of the dark frame resolves in the page's (default) mode.
        d.begin("move out");
        d.move(r, page(d));
        d.commit();
        QCOMPARE(d.at(r).p.fills[0].color, SkColors::kWhite);
        d.undo();
        d.undo();
        QCOMPARE(d.at(r).p.fills[0].color, SkColors::kWhite);
        QCOMPARE(d.at(t).p.text, QStringLiteral("Hello"));
    }

    void numberBindingsDriveLayout()
    {
        Document d;
        d.begin("setup");
        Vars v = makeVars(d);
        NodeId row = d.create(NodeType::Frame, page(d));
        d.edit(row).layout.mode = LayoutMode::Horizontal;
        d.edit(row).layoutChild.sizingH = LayoutSizing::Hug;
        d.edit(row).bindings.insert(QStringLiteral("spacing"), v.gap);
        d.edit(row).bindings.insert(QStringLiteral("w"), v.gap); // ignored: hug wins after layout
        NodeId a = rect(d, row, 0, 0, 10, 10);
        NodeId b = rect(d, row, 0, 0, 10, 10);
        d.edit(a).bindings.insert(QStringLiteral("w"), v.gap);
        d.commit();
        QCOMPARE(d.at(row).p.layout.spacing, 8.f);
        QCOMPARE(d.at(a).p.width, 8.f);
        QCOMPARE(d.at(b).p.transform.getTranslateX(), 16.f);

        d.begin("mode");
        d.edit(row).variableModes.insert(v.coll, v.dark);
        d.commit();
        QCOMPARE(d.at(row).p.layout.spacing, 16.f);
        QCOMPARE(d.at(a).p.width, 16.f);
        QCOMPARE(d.at(b).p.transform.getTranslateX(), 32.f);
    }

    void instancesResolveInContextWithoutOverrides()
    {
        Document d;
        d.begin("setup");
        Vars v = makeVars(d);
        NodeId comp = d.create(NodeType::Component, page(d));
        NodeId bg = rect(d, comp, 0, 0, 100, 40);
        Paint fill = Paint::solid(SkColors::kRed);
        fill.colorVar = v.bg;
        d.edit(bg).fills = {fill};
        NodeId dark = d.create(NodeType::Frame, page(d));
        d.edit(dark).variableModes.insert(v.coll, v.dark);
        Props ip;
        ip.type = NodeType::Instance;
        ip.componentId = comp;
        NodeId inst = d.create(ip, dark);
        d.commit();
        NodeId iBg = d.at(inst).children.at(0);
        QCOMPARE(d.at(bg).p.fills[0].color, SkColors::kWhite);
        QCOMPARE(d.at(iBg).p.fills[0].color, SkColors::kBlack);
        QCOMPARE(d.at(iBg).p.overrides & OvFills, 0u);

        // An unrelated edit must not turn the context-resolved fill into an override.
        d.begin("rename");
        d.edit(iBg).name = QStringLiteral("Renamed");
        d.commit();
        QCOMPARE(d.at(iBg).p.overrides & OvFills, 0u);

        // Main switches variable: instance follows.
        d.begin("rebind");
        Paint f2 = d.at(bg).p.fills[0];
        f2.colorVar = v.brand;
        d.edit(bg).fills = {f2};
        d.commit();
        QCOMPARE(d.at(iBg).p.fills[0].colorVar, v.brand);
        QCOMPARE(d.at(iBg).p.fills[0].color, SkColors::kBlack);
    }

    void libraryRoundTrip()
    {
        Document d;
        d.begin("setup");
        Vars v = makeVars(d);
        Style ts;
        ts.id = Library::newId();
        ts.name = QStringLiteral("Heading");
        ts.type = StyleType::Text;
        ts.text.size = 32;
        ts.text.weight = 700;
        d.editLibrary().styles.push_back(ts);
        NodeId frame = d.create(NodeType::Frame, page(d));
        d.edit(frame).variableModes.insert(v.coll, v.dark);
        NodeId t = d.create(NodeType::Text, frame);
        d.edit(t).textStyleId = ts.id;
        d.edit(t).bindings.insert(QStringLiteral("characters"), v.title);
        d.commit();
        QCOMPARE(d.at(t).p.textStyle.size, 32.f);

        Document d2;
        QVERIFY(io::load(d2, io::save(d)));
        QCOMPARE(d2.library(), d.library());
        QCOMPARE(d2.at(t).p.textStyleId, ts.id);
        QCOMPARE(d2.at(t).p.bindings, d.at(t).p.bindings);
        QCOMPARE(d2.at(frame).p.variableModes, d.at(frame).p.variableModes);
        QCOMPARE(d2.at(t).p.text, QStringLiteral("Hola"));
    }

    void aliasCycleDoesNotHang()
    {
        Library lib;
        lib.collections.push_back({QStringLiteral("c"), QStringLiteral("C"), {{QStringLiteral("m"), QStringLiteral("M")}}});
        Variable a, b;
        a.id = QStringLiteral("a");
        b.id = QStringLiteral("b");
        a.collection = b.collection = QStringLiteral("c");
        VarValue va, vb;
        va.alias = QStringLiteral("b");
        vb.alias = QStringLiteral("a");
        a.values.insert(QStringLiteral("m"), va);
        b.values.insert(QStringLiteral("m"), vb);
        lib.variables = {a, b};
        QVERIFY(!vars::resolve(lib, QStringLiteral("a"), {}).has_value());
    }

    void componentProperties()
    {
        Document d;
        d.begin("setup");
        // Two icon components for the swap property.
        NodeId iconA = d.create(NodeType::Component, page(d));
        d.edit(iconA).name = QStringLiteral("Icon/A");
        rect(d, iconA, 0, 0, 10, 10);
        NodeId iconB = d.create(NodeType::Component, page(d));
        d.edit(iconB).name = QStringLiteral("Icon/B");
        rect(d, iconB, 0, 0, 20, 20);
        rect(d, iconB, 0, 0, 5, 5);

        NodeId button = d.create(NodeType::Component, page(d));
        NodeId label = d.create(NodeType::Text, button);
        d.edit(label).text = QStringLiteral("Button");
        Props ni;
        ni.type = NodeType::Instance;
        ni.componentId = iconA;
        NodeId icon = d.create(ni, button);
        ComponentProp text{QStringLiteral("p-text"), QStringLiteral("Label"), ComponentPropType::Text, QStringLiteral("Button"), {}};
        ComponentProp show{QStringLiteral("p-show"), QStringLiteral("Show icon"), ComponentPropType::Boolean, QStringLiteral("true"), {}};
        ComponentProp swap{QStringLiteral("p-icon"), QStringLiteral("Icon"), ComponentPropType::InstanceSwap,
            QString::number(iconA), {iconA, iconB}};
        d.edit(button).componentProps = {text, show, swap};
        d.edit(label).propRefs.insert(QStringLiteral("characters"), text.id);
        d.edit(icon).propRefs.insert(QStringLiteral("visible"), show.id);
        d.edit(icon).propRefs.insert(QStringLiteral("mainComponent"), swap.id);

        Props ip;
        ip.type = NodeType::Instance;
        ip.componentId = button;
        NodeId inst = d.create(ip, page(d));
        d.commit();
        NodeId iLabel = d.at(inst).children.at(0);
        NodeId iIcon = d.at(inst).children.at(1);
        QCOMPARE(d.at(iLabel).p.text, QStringLiteral("Button"));
        QVERIFY(d.at(iIcon).p.visible);
        QCOMPARE(d.at(iIcon).p.componentId, iconA);

        d.begin("set props");
        d.edit(inst).propValues = {{text.id, QStringLiteral("Save")}, {show.id, QStringLiteral("false")},
            {swap.id, QString::number(iconB)}};
        d.commit();
        QCOMPARE(d.at(iLabel).p.text, QStringLiteral("Save"));
        QVERIFY(!d.at(iIcon).p.visible);
        QCOMPARE(d.at(iIcon).p.componentId, iconB);
        QCOMPARE(d.at(iIcon).children.size(), size_t(2));
        QCOMPARE(d.at(iLabel).p.overrides & OvText, 0u);
        QCOMPARE(d.at(iIcon).p.overrides & (OvComponent | OvVisible), 0u);

        // An unrelated main edit keeps the swapped nested instance (same node, same children).
        const NodeId swappedChild = d.at(iIcon).children.at(0);
        d.begin("main edit");
        d.edit(button).name = QStringLiteral("Button!");
        d.edit(label).fills = {Paint::solid(SkColors::kRed)};
        d.commit();
        QCOMPARE(d.at(iIcon).children.at(0), swappedChild);
        QCOMPARE(d.at(iLabel).p.text, QStringLiteral("Save"));

        // Typing into the instance's label sets the property value instead of an override.
        d.begin("type");
        d.edit(iLabel).text = QStringLiteral("Submit");
        d.commit();
        QCOMPARE(d.at(inst).p.propValues.value(text.id), QStringLiteral("Submit"));
        QCOMPARE(d.at(iLabel).p.overrides & OvText, 0u);

        // A second instance without values follows the default; changing the default updates it.
        d.begin("second");
        NodeId inst2 = d.create(ip, page(d));
        d.commit();
        NodeId i2Label = d.at(inst2).children.at(0);
        QCOMPARE(d.at(i2Label).p.text, QStringLiteral("Button"));
        d.begin("default");
        d.edit(button).componentProps[0].defaultValue = QStringLiteral("Go");
        d.commit();
        QCOMPARE(d.at(i2Label).p.text, QStringLiteral("Go"));
        QCOMPARE(d.at(label).p.text, QStringLiteral("Go"));
        QCOMPARE(d.at(iLabel).p.text, QStringLiteral("Submit"));

        Document d2;
        QVERIFY(io::load(d2, io::save(d)));
        QCOMPARE(d2.at(button).p.componentProps, d.at(button).p.componentProps);
        QCOMPARE(d2.at(inst).p.propValues, d.at(inst).p.propValues);
        QCOMPARE(d2.at(label).p.propRefs, d.at(label).p.propRefs);
    }

    void richTextRunsAndSplice()
    {
        Props p;
        p.type = NodeType::Text;
        p.text = QStringLiteral("Hello world");
        TextRun bold;
        bold.weight = 700;
        text::styleRange(p, 6, 11, RunWeight, bold); // "world"
        QCOMPARE(p.textRuns.size(), size_t(1));
        QCOMPARE(p.textRuns[0].start, 6);
        QCOMPARE(text::styleAt(p, 7).weight, 700);
        QCOMPARE(text::styleAt(p, 2).weight, 400);

        // Typing at the end of the bold run extends it; typing before it shifts it.
        text::splice(p, 11, 0, QStringLiteral("!"));
        QCOMPARE(p.textRuns[0].end, 12);
        text::splice(p, 0, 0, QStringLiteral(">> "));
        QCOMPARE(p.textRuns[0].start, 9);
        QCOMPARE(p.text, QStringLiteral(">> Hello world!"));
        // Deleting across the run start trims it.
        text::splice(p, 7, 4, QString()); // removes "o wo"
        QCOMPARE(p.text, QStringLiteral(">> Hellrld!"));
        QCOMPARE(p.textRuns[0].start, 7);
        QCOMPARE(p.textRuns[0].end, 11);

        // Same style next to each other merges; clearing removes the run.
        text::styleRange(p, 3, 7, RunWeight, bold);
        QCOMPARE(p.textRuns.size(), size_t(1));
        QCOMPARE(p.textRuns[0].start, 3);
        TextRun big;
        big.size = 30;
        text::styleRange(p, 0, 2, RunSize, big);
        QCOMPARE(p.textRuns.size(), size_t(2));
        text::clearRange(p, 0, int(p.text.size()), RunWeight);
        QCOMPARE(p.textRuns.size(), size_t(1));
        QCOMPARE(p.textRuns[0].mask, std::uint32_t(RunSize));
        // A larger run makes the layer taller.
        Props plain = p;
        plain.textRuns.clear();
        QVERIFY(text::measure(p, -1).height > text::measure(plain, -1).height);
    }

    void listsAndDisplayMap()
    {
        Props p;
        p.type = NodeType::Text;
        p.text = QStringLiteral("one\ntwo\nthree");
        ParagraphAttr ordered;
        ordered.list = ListType::Ordered;
        text::setParagraphs(p, 0, 2, ordered);
        auto map = text::displayMap(p);
        // "1.  one\n2.  two\n3.  three": each paragraph gains a 4-char marker.
        QCOMPARE(map[0], 4);
        QCOMPARE(map[4], 12);
        QCOMPARE(text::fromDisplay(map, 12), 4);
        QCOMPARE(map[int(p.text.size())], int(p.text.size()) + 12);

        // A new line inside a list item continues the list; joining lines merges attributes.
        text::splice(p, 3, 0, QStringLiteral("\n"));
        QCOMPARE(p.paragraphs.size(), size_t(4));
        QCOMPARE(p.paragraphs[1].list, ListType::Ordered);
        text::splice(p, 3, 1, QString());
        QCOMPARE(p.paragraphs.size(), size_t(3));

        // Paragraph spacing adds one display line per break.
        p.paragraphs.clear();
        p.textStyle.paragraphSpacing = 10;
        map = text::displayMap(p);
        QCOMPARE(map[4], 5);
        QCOMPARE(map[8], 10);
    }

    void richTextRoundTrip()
    {
        Document d;
        d.begin("t");
        NodeId t = d.create(NodeType::Text, page(d));
        Props& p = d.edit(t);
        p.text = QStringLiteral("a\nb");
        p.textStyle.features.insert(QStringLiteral("tnum"), 1);
        p.textStyle.axes.insert(QStringLiteral("opsz"), 32);
        TextRun link;
        link.link = QStringLiteral("https://example.com");
        link.fill = SkColors::kRed;
        text::styleRange(p, 0, 1, RunLink | RunFill, link);
        ParagraphAttr bullet;
        bullet.list = ListType::Bullet;
        bullet.indent = 1;
        text::setParagraphs(p, 1, 1, bullet);
        d.commit();
        Document d2;
        QVERIFY(io::load(d2, io::save(d)));
        QCOMPARE(d2.at(t).p.textRuns, d.at(t).p.textRuns);
        QCOMPARE(d2.at(t).p.paragraphs, d.at(t).p.paragraphs);
        QCOMPARE(d2.at(t).p.textStyle.features, d.at(t).p.textStyle.features);
        QCOMPARE(d2.at(t).p.textStyle.axes, d.at(t).p.textStyle.axes);
    }

    void gridLayout()
    {
        Document d;
        d.begin("grid");
        NodeId g = d.create(NodeType::Frame, page(d));
        d.resize(g, 220, 100);
        Props& gp = d.edit(g);
        gp.layout.mode = LayoutMode::Grid;
        gp.layout.gridColumns = 2;
        gp.layout.spacing = 20;        // column gap
        gp.layout.counterSpacing = 10; // row gap
        gp.layout.padL = gp.layout.padR = gp.layout.padT = gp.layout.padB = 10;
        gp.layoutChild.sizingV = LayoutSizing::Hug;
        NodeId a = rect(d, g, 0, 0, 30, 40);
        NodeId b = rect(d, g, 0, 0, 30, 20);
        NodeId wide = rect(d, g, 0, 0, 30, 30);
        d.edit(wide).layoutChild.colSpan = 2;
        d.edit(wide).layoutChild.sizingH = LayoutSizing::Fill;
        NodeId c = rect(d, g, 0, 0, 30, 30);
        d.edit(c).layoutChild.sizingH = LayoutSizing::Fill;
        d.commit();
        // Columns: (220 - 20 padding - 20 gap) / 2 = 90.
        QCOMPARE(d.at(a).p.transform.getTranslateX(), 10.f);
        QCOMPARE(d.at(b).p.transform.getTranslateX(), 120.f);
        QCOMPARE(d.at(wide).p.transform.getTranslateY(), 60.f); // row 0 is 40 tall + 10 gap
        QCOMPARE(d.at(wide).p.width, 200.f);
        QCOMPARE(d.at(c).p.width, 90.f);
        QCOMPARE(d.at(c).p.transform.getTranslateY(), 100.f);
        QCOMPARE(d.at(g).p.height, 140.f); // 10 + 40 + 10 + 30 + 10 + 30 + 10

        // Hugging width sizes columns to content.
        d.begin("hug");
        d.edit(g).layoutChild.sizingH = LayoutSizing::Hug;
        d.edit(wide).layoutChild.sizingH = LayoutSizing::Fixed;
        d.edit(c).layoutChild.sizingH = LayoutSizing::Fixed;
        d.resize(c, 30, 30);
        d.commit();
        QCOMPARE(d.at(g).p.width, 10.f + 30 + 20 + 30 + 10);
    }

    void vectorNetworks()
    {
        VectorData sq;
        Contour c;
        for (SkPoint p : {SkPoint{0, 0}, SkPoint{100, 0}, SkPoint{100, 100}, SkPoint{0, 100}})
            c.pts.push_back({p, p, p, AnchorMode::Corner});
        c.closed = true;
        sq.contours = {c};
        VectorData n = sq.asNetwork();
        QCOMPARE(n.vertices.size(), size_t(4));
        QCOMPARE(n.segments.size(), size_t(4));
        QVERIFY(n.toPath().contains(50, 50));
        QCOMPARE(n.toPath().computeTightBounds(), sq.toPath().computeTightBounds());

        // A branch from a corner: stroked, but it doesn't change the filled area.
        n.vertices.push_back({{150, -50}, AnchorMode::Corner});
        n.addSegment(1, 4, n.vertices[1].p, n.vertices[4].p, false);
        QCOMPARE(n.segmentsAt(1).size(), size_t(3));
        QVERIFY(n.strokePath().computeTightBounds().right() >= 150);
        QVERIFY(!n.toPath().contains(120, -10));
        QCOMPARE(n.faces().size(), size_t(1));

        // Splitting a region edge keeps the fill.
        n.splitSegment(0, 0.5f);
        QCOMPARE(n.regions[0].loops[0].size(), size_t(5));
        QVERIFY(n.toPath().contains(50, 50));
        QVERIFY(n.toPath().contains(50, 2));

        // A diagonal makes two faces; the paint bucket fills one at a time.
        n.regions.clear();
        n.addSegment(0, 2, n.vertices[0].p, n.vertices[2].p, false);
        QCOMPARE(n.faces().size(), size_t(2));
        QVERIFY(n.toggleRegionAt({75, 25}));
        QVERIFY(n.toPath().contains(75, 25));
        QVERIFY(!n.toPath().contains(25, 75));
        QVERIFY(n.toggleRegionAt({75, 25}));
        QVERIFY(n.regions.empty());

        // Connecting back to the path fills the new loop.
        VectorData open;
        open.vertices = {{{0, 0}}, {{10, 0}}, {{10, 10}}};
        open.addSegment(0, 1, {0, 0}, {10, 0}, true);
        open.addSegment(1, 2, {10, 0}, {10, 10}, true);
        QVERIFY(open.regions.empty());
        open.addSegment(2, 0, {10, 10}, {0, 0}, true);
        QCOMPARE(open.regions.size(), size_t(1));
        QVERIFY(open.toPath().contains(7, 3));

        // Deleting a vertex drops its segments and the loops through them.
        open.removeVertices({1});
        QCOMPARE(open.segments.size(), size_t(1));
        QVERIFY(open.regions.empty());

        Document d;
        d.begin("v");
        Props vp;
        vp.type = NodeType::Vector;
        vp.vector = n;
        NodeId id = d.create(vp, page(d));
        d.commit();
        Document d2;
        QVERIFY(io::load(d2, io::save(d)));
        QCOMPARE(d2.at(id).p.vector, d.at(id).p.vector);
    }

    void figImport()
    {
        QFile f(QStringLiteral(DRAV_TEST_DATA "/sample.fig"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        Document d;
        d.begin("import");
        fig::Result res;
        QString err;
        QVERIFY2(fig::import(d, f.readAll(), &res, &err), qPrintable(err));
        d.commit();
        QCOMPARE(res.pages.size(), size_t(1)); // the internal canvas is skipped
        QVERIFY(res.warnings.join(" ").contains("STICKY"));
        const NodeId page = res.pages[0];
        QCOMPARE(d.at(page).p.name, QStringLiteral("Page 1"));
        auto byName = [&](const QString& name) {
            NodeId found = kNoNode;
            d.walk(page, [&](const Node& n) {
                if (!found && n.p.name == name && !d.isInsideInstance(n.id))
                    found = n.id;
                return !found;
            });
            return found;
        };
        QCOMPARE(d.library().styles.size(), size_t(1));
        const QString brand = d.library().styles[0].id;

        const Node& card = d.at(byName("Card"));
        QCOMPARE(card.type(), NodeType::Frame);
        QCOMPARE(card.p.layout.mode, LayoutMode::Vertical);
        QCOMPARE(card.p.layout.spacing, 8.f);
        QCOMPARE(card.p.layout.padL, 16.f);
        QCOMPARE(card.p.layoutChild.sizingV, LayoutSizing::Hug);
        QCOMPARE(card.p.corners.tl, 8.f);
        QCOMPARE(card.p.effects.size(), size_t(1));
        QCOMPARE(card.p.transform.getTranslateX(), 10.f);

        const Node& title = d.at(byName("Title"));
        QCOMPARE(title.p.text, QStringLiteral("Hello Figma"));
        QCOMPARE(title.p.textStyle.weight, 700);
        QCOMPARE(title.p.textStyle.size, 20.f);
        QCOMPARE(text::styleAt(title.p, 8).fill, (SkColor4f{1, 0, 0, 1}));
        QCOMPARE(text::styleAt(title.p, 2).fill, (SkColor4f{0, 0, 0, 1}));

        QCOMPARE(d.at(byName("Styled")).p.fillStyle, brand);

        const NodeId button = byName("Button");
        QCOMPARE(d.at(button).type(), NodeType::Component);
        QCOMPARE(d.at(button).p.componentProps.size(), size_t(1));
        NodeId inst = kNoNode;
        d.walk(page, [&](const Node& n) {
            if (n.type() == NodeType::Instance && !inst)
                inst = n.id;
            return true;
        });
        QVERIFY(inst);
        QCOMPARE(d.at(inst).p.componentId, button);
        QCOMPARE(d.at(inst).p.width, 160.f);
        QCOMPARE(d.at(inst).children.size(), size_t(2));
        QCOMPARE(d.at(d.at(inst).children[0]).p.text, QStringLiteral("Go"));
        QCOMPARE(d.at(d.at(inst).children[1]).p.fills.at(0).color, (SkColor4f{1, 0, 0, 1}));
        QCOMPARE(d.at(d.at(button).children[1]).p.fills.at(0).color, (SkColor4f{0, 0, 0, 1}));

        const Node& tri = d.at(byName("Triangle"));
        QVERIFY(tri.p.vector.isNetwork());
        QCOMPARE(tri.p.vector.vertices.size(), size_t(3));
        QCOMPARE(tri.p.vector.vertices[2].p, (SkPoint{50, 100}));
        QCOMPARE(tri.p.vector.regions.size(), size_t(1));

        const Node& group = d.at(byName("Group"));
        QCOMPARE(group.type(), NodeType::Group);
        const Node& ring = d.at(group.children.at(0));
        QVERIFY(std::abs(ring.p.arcSweep - 180) < 0.01f);
        QCOMPARE(ring.p.arcInner, 0.5f);

        const Paint& photo = d.at(byName("Photo")).p.fills.at(0);
        QCOMPARE(photo.type, PaintType::Image);
        QVERIFY(d.images().contains(photo.imageHash));
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
