#include "core/Document.h"
#include "render/Export.h"
#include "render/Renderer.h"

#include <include/codec/SkCodec.h>
#include <include/core/SkBitmap.h>
#include <include/core/SkData.h>
#include <include/core/SkImage.h>

#include <QtTest>

using namespace drav;

class TestRender : public QObject {
    Q_OBJECT

    static SkBitmap decode(const QByteArray& png)
    {
        Renderer::registerCodecs();
        auto img = SkImages::DeferredFromEncodedData(SkData::MakeWithCopy(png.constData(), png.size()));
        SkBitmap bm;
        if (img) {
            bm.allocN32Pixels(img->width(), img->height());
            img->readPixels(nullptr, bm.pixmap(), 0, 0);
        }
        return bm;
    }

private slots:
    void pngExportPixels()
    {
        Document d;
        d.begin("x");
        NodeId f = d.create(NodeType::Frame, d.currentPage());
        d.resize(f, 40, 20);
        d.edit(f).fills = {Paint::solid(SkColors::kWhite)};
        NodeId r = d.create(NodeType::Rectangle, f);
        d.resize(r, 20, 20);
        d.edit(r).fills = {Paint::solid(SkColors::kRed)};
        NodeId t = d.create(NodeType::Text, f);
        d.edit(t).text = QStringLiteral("Hi");
        d.edit(t).fills = {Paint::solid(SkColors::kBlack)};
        d.commit();

        QByteArray png = exporter::exportNode(d, f, exporter::Format::PNG, 2);
        SkBitmap bm = decode(png);
        QCOMPARE(bm.width(), 80);
        QCOMPARE(bm.height(), 40);
        QCOMPARE(SkColorGetR(bm.getColor(10, 30)), 255u);
        QCOMPARE(SkColorGetG(bm.getColor(10, 30)), 0u);
        QCOMPARE(bm.getColor(70, 30), SK_ColorWHITE);
    }

    void vectorNetworkRender()
    {
        // Filled square region plus an open branch from a corner; stroked in blue.
        Document d;
        d.begin("x");
        NodeId f = d.create(NodeType::Frame, d.currentPage());
        d.resize(f, 100, 60);
        d.edit(f).fills = {Paint::solid(SkColors::kWhite)};
        Props vp;
        vp.type = NodeType::Vector;
        vp.width = 80;
        vp.height = 40;
        VectorData& v = vp.vector;
        v.vertices = {{{0, 0}}, {{40, 0}}, {{40, 40}}, {{0, 40}}, {{80, 0}}};
        v.addSegment(0, 1, {0, 0}, {40, 0}, true);
        v.addSegment(1, 2, {40, 0}, {40, 40}, true);
        v.addSegment(2, 3, {40, 40}, {0, 40}, true);
        v.addSegment(3, 0, {0, 40}, {0, 0}, true);
        v.addSegment(1, 4, {40, 0}, {80, 0}, true);
        vp.fills = {Paint::solid(SkColors::kRed)};
        vp.strokes = {Paint::solid(SkColors::kBlue)};
        vp.stroke.weight = 4;
        vp.stroke.align = StrokeAlign::Center;
        NodeId n = d.create(vp, f);
        d.setPosition(n, 10, 10);
        d.commit();
        QCOMPARE(d.at(n).p.vector.regions.size(), size_t(1));
        SkBitmap bm = decode(exporter::exportNode(d, f, exporter::Format::PNG, 1));
        QCOMPARE(bm.getColor(30, 30), SK_ColorRED);   // inside the region
        QCOMPARE(bm.getColor(75, 10), SK_ColorBLUE);  // on the open branch
        QCOMPARE(bm.getColor(75, 30), SK_ColorWHITE); // below the branch: not filled
    }

    void allFeaturesRender()
    {
        // Smoke test: every node type and effect renders without crashing.
        Document d;
        d.begin("x");
        NodeId page = d.currentPage();
        NodeId f = d.create(NodeType::Frame, page);
        d.resize(f, 400, 300);
        Paint grad;
        grad.type = PaintType::LinearGradient;
        Paint radial = grad;
        radial.type = PaintType::RadialGradient;
        Paint angular = grad;
        angular.type = PaintType::AngularGradient;
        Paint diamond = grad;
        diamond.type = PaintType::DiamondGradient;
        d.edit(f).fills = {grad, radial, angular, diamond};
        d.edit(f).effects = {Effect{}, Effect{EffectType::InnerShadow}, Effect{EffectType::BackgroundBlur}};
        for (NodeType t : {NodeType::Rectangle, NodeType::Ellipse, NodeType::Polygon, NodeType::Star, NodeType::Line,
                 NodeType::Text}) {
            NodeId n = d.create(t, f);
            d.edit(n).text = QStringLiteral("text");
            d.edit(n).fills = {Paint::solid(SkColors::kBlue), radial};
            d.edit(n).strokes = {Paint::solid(SkColors::kBlack)};
            d.edit(n).stroke.weight = 3;
            d.edit(n).stroke.endCap = StrokeCap::TriangleArrow;
            d.edit(n).effects = {Effect{}, Effect{EffectType::LayerBlur}};
            d.edit(n).opacity = 0.5f;
        }
        NodeId g = d.create(NodeType::Group, f);
        NodeId m = d.create(NodeType::Ellipse, g);
        d.edit(m).isMask = true;
        d.create(NodeType::Rectangle, g);
        NodeId b = d.create(NodeType::BooleanOp, f);
        d.edit(b).boolOp = BooleanOpType::Subtract;
        d.create(NodeType::Rectangle, b);
        NodeId e = d.create(NodeType::Ellipse, b);
        d.setPosition(e, 50, 50);
        d.commit();
        for (auto fmt : {exporter::Format::PNG, exporter::Format::JPG, exporter::Format::SVG, exporter::Format::PDF})
            QVERIFY(!exporter::exportNode(d, f, fmt).isEmpty());
        QVERIFY(!exporter::exportNode(d, page, exporter::Format::PNG).isEmpty());
        Renderer r(d);
        QVERIFY(r.recordPage(page));
    }
};

QTEST_GUILESS_MAIN(TestRender)
#include "tst_render.moc"
