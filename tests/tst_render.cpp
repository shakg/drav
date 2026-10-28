#include "core/Color.h"
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

    void drawingExtrasRender()
    {
        Document d;
        d.begin("x");
        NodeId f = d.create(NodeType::Frame, d.currentPage());
        d.resize(f, 220, 120);
        d.edit(f).fills = {Paint::solid(SkColors::kWhite)};
        // A line tapering from 20 px to nothing.
        Props lp;
        lp.type = NodeType::Line;
        lp.width = 200;
        lp.height = 0;
        lp.strokes = {Paint::solid(SkColors::kBlack)};
        lp.stroke.weight = 20;
        lp.stroke.widthProfile = {{0, 1}, {1, 0}};
        NodeId line = d.create(lp, f);
        d.setPosition(line, 10, 30);
        // Two strokes with their own weights: the thick red one under the thin blue one.
        Props rp;
        rp.type = NodeType::Rectangle;
        rp.width = rp.height = 40;
        Paint red = Paint::solid(SkColors::kRed), blue = Paint::solid(SkColors::kBlue);
        red.strokeWeight = 16;
        blue.strokeWeight = 2;
        rp.strokes = {red, blue};
        rp.stroke.align = StrokeAlign::Center;
        NodeId r = d.create(rp, f);
        d.setPosition(r, 100, 60);
        d.commit();
        SkBitmap bm = decode(exporter::exportNode(d, f, exporter::Format::PNG, 1));
        QCOMPARE(bm.getColor(15, 38), SK_ColorBLACK);  // near the start: wide
        QCOMPARE(bm.getColor(200, 38), SK_ColorWHITE); // near the end: thin, nothing 8 px off the line
        QCOMPARE(bm.getColor(95, 80), SK_ColorRED);    // 5 px outside the box: only the 16 px stroke
        QCOMPARE(bm.getColor(100, 80), SK_ColorBLUE);  // on the edge: the 2 px stroke on top
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

    void displayP3Export()
    {
        // A P3 document exports P3 pixels tagged with a P3 profile that look like the sRGB original.
        const SkColor4f srgb = {0.2f, 0.6f, 0.4f, 1};
        Document d;
        d.begin("x");
        d.edit(d.root()).colorProfile = ColorProfile::DisplayP3;
        NodeId r = d.create(NodeType::Rectangle, d.currentPage());
        d.resize(r, 10, 10);
        d.edit(r).fills = {Paint::solid(color::convert(srgb, ColorProfile::SRGB, ColorProfile::DisplayP3))};
        d.commit();
        const QByteArray png = exporter::exportNode(d, r, exporter::Format::PNG, 1);
        Renderer::registerCodecs();
        auto img = SkImages::DeferredFromEncodedData(SkData::MakeWithCopy(png.constData(), png.size()));
        QVERIFY(img && img->colorSpace());
        QVERIFY(img->colorSpace()->gammaCloseToSRGB());
        QVERIFY(!img->colorSpace()->isSRGB());
        SkBitmap bm;
        bm.allocPixels(SkImageInfo::MakeN32Premul(10, 10, SkColorSpace::MakeSRGB()));
        QVERIFY(img->readPixels(nullptr, bm.pixmap(), 0, 0));
        const SkColor px = bm.getColor(5, 5);
        QVERIFY(std::abs(int(SkColorGetR(px)) - 51) <= 2);
        QVERIFY(std::abs(int(SkColorGetG(px)) - 153) <= 2);
        QVERIFY(std::abs(int(SkColorGetB(px)) - 102) <= 2);
    }
};

QTEST_GUILESS_MAIN(TestRender)
#include "tst_render.moc"
