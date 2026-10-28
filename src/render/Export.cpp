#include "Export.h"

#include "Renderer.h"

#include "core/Color.h"

#include <include/core/SkCanvas.h>
#include <include/core/SkDocument.h>
#include <include/core/SkStream.h>
#include <include/core/SkSurface.h>
#include <include/docs/SkPDFDocument.h>
#include <include/docs/SkPDFJpegHelpers.h>
#include <include/encode/SkJpegEncoder.h>
#include <include/encode/SkPngEncoder.h>
#include <include/encode/SkWebpEncoder.h>
#include <include/svg/SkSVGCanvas.h>

#include <cmath>

namespace drav::exporter {

Format formatFromName(const QString& s)
{
    const QString u = s.toUpper();
    if (u == QLatin1String("JPG") || u == QLatin1String("JPEG")) return Format::JPG;
    if (u == QLatin1String("WEBP")) return Format::WEBP;
    if (u == QLatin1String("SVG")) return Format::SVG;
    if (u == QLatin1String("PDF")) return Format::PDF;
    return Format::PNG;
}

QString extension(Format f)
{
    switch (f) {
    case Format::PNG: return QStringLiteral("png");
    case Format::JPG: return QStringLiteral("jpg");
    case Format::WEBP: return QStringLiteral("webp");
    case Format::SVG: return QStringLiteral("svg");
    case Format::PDF: return QStringLiteral("pdf");
    }
    return QStringLiteral("png");
}

namespace {

// Bounds to export in page space, plus the drawing callback for the content.
struct Target {
    SkRect bounds;
    std::function<void(SkCanvas*, Renderer&)> draw;
};

Target target(const Document& doc, NodeId id)
{
    const Node& n = doc.at(id);
    if (n.type() == NodeType::Page) {
        SkRect b = SkRect::MakeEmpty();
        for (NodeId c : n.children)
            b.join(doc.visualBounds(c));
        return {b, [&doc, id](SkCanvas* c, Renderer& r) {
                    SkPaint bg;
                    bg.setColor4f(doc.at(id).p.pageBackground, r.colorSpace());
                    bg.setBlendMode(SkBlendMode::kSrc);
                    c->drawPaint(bg);
                    for (NodeId k : doc.at(id).children)
                        r.drawNode(c, k, false);
                }};
    }
    SkRect b = doc.visualBounds(id);
    // Exporting a frame clips to its box like Figma.
    if (isFrameLike(n.type()) && n.p.clipsContent && n.p.effects.empty())
        b = doc.worldBounds(id);
    b.roundOut(&b);
    return {b, [&doc, id](SkCanvas* c, Renderer& r) {
                c->concat(doc.worldTransform(id));
                r.drawNodeLocal(c, id);
            }};
}

QByteArray toBytes(SkDynamicMemoryWStream& s)
{
    sk_sp<SkData> data = s.detachAsData();
    return QByteArray(static_cast<const char*>(data->data()), int(data->size()));
}

} // namespace

QByteArray exportNode(const Document& doc, NodeId id, Format format, float scale)
{
    Renderer renderer(doc);
    Target t = target(doc, id);
    if (t.bounds.isEmpty())
        return {};
    SkDynamicMemoryWStream out;
    if (format == Format::SVG) {
        auto canvas = SkSVGCanvas::Make(SkRect::MakeWH(t.bounds.width(), t.bounds.height()), &out);
        canvas->translate(-t.bounds.left(), -t.bounds.top());
        t.draw(canvas.get(), renderer);
        canvas.reset(); // flushes the document
        return toBytes(out);
    }
    if (format == Format::PDF)
        return exportPdf(doc, {id});

    const int w = std::max(1, int(std::ceil(t.bounds.width() * scale)));
    const int h = std::max(1, int(std::ceil(t.bounds.height() * scale)));
    // Pixels are in the document's color space; encoders embed its ICC profile.
    sk_sp<SkColorSpace> cs = color::space(doc.colorProfile());
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(w, h, cs ? cs : SkColorSpace::MakeSRGB()));
    if (!surface)
        return {};
    SkCanvas* c = surface->getCanvas();
    c->clear(format == Format::JPG ? SK_ColorWHITE : SK_ColorTRANSPARENT);
    c->scale(scale, scale);
    c->translate(-t.bounds.left(), -t.bounds.top());
    t.draw(c, renderer);
    SkPixmap pm;
    if (!surface->peekPixels(&pm))
        return {};
    bool ok = false;
    switch (format) {
    case Format::JPG: {
        SkJpegEncoder::Options o;
        o.fQuality = 92;
        ok = SkJpegEncoder::Encode(&out, pm, o);
        break;
    }
    case Format::WEBP: {
        SkWebpEncoder::Options o;
        o.fQuality = 90;
        ok = SkWebpEncoder::Encode(&out, pm, o);
        break;
    }
    default:
        ok = SkPngEncoder::Encode(&out, pm, {});
        break;
    }
    return ok ? toBytes(out) : QByteArray();
}

QByteArray exportPdf(const Document& doc, const std::vector<NodeId>& ids)
{
    Renderer renderer(doc);
    SkDynamicMemoryWStream out;
    SkPDF::Metadata meta;
    meta.fCreator = "drav";
    meta.jpegEncoder = SkPDF::JPEG::Encode;
    meta.jpegDecoder = SkPDF::JPEG::Decode;
    sk_sp<SkDocument> pdf = SkPDF::MakeDocument(&out, meta);
    if (!pdf)
        return {};
    for (NodeId id : ids) {
        Target t = target(doc, id);
        if (t.bounds.isEmpty())
            continue;
        SkCanvas* c = pdf->beginPage(t.bounds.width(), t.bounds.height());
        c->translate(-t.bounds.left(), -t.bounds.top());
        t.draw(c, renderer);
        pdf->endPage();
    }
    pdf->close();
    return toBytes(out);
}

} // namespace drav::exporter
