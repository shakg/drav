#include "Renderer.h"

#include <include/core/SkColorFilter.h>
#include <include/effects/SkColorMatrix.h>

#include "core/Geometry.h"
#include "core/Text.h"

#include <include/codec/SkCodec.h>
#include <include/codec/SkJpegDecoder.h>
#include <include/codec/SkPngDecoder.h>
#include <include/codec/SkWebpDecoder.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkColorFilter.h>
#include <include/core/SkContourMeasure.h>
#include <include/core/SkData.h>
#include <include/core/SkMaskFilter.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPathEffect.h>
#include <include/core/SkPathUtils.h>
#include <include/core/SkBlurTypes.h>
#include <include/core/SkPictureRecorder.h>
#include <include/core/SkRRect.h>
#include <include/core/SkShader.h>
#include <include/effects/SkDashPathEffect.h>
#include <include/effects/SkGradientShader.h>
#include <include/effects/SkImageFilters.h>
#include <include/effects/SkRuntimeEffect.h>
#include <include/pathops/SkPathOps.h>

#include <cmath>
#include <mutex>

namespace drav {

sk_sp<SkColorFilter> imageAdjustments(const Paint& p)
{
    // Linear part: exposure (gain), contrast around mid gray, saturation, temperature and tint.
    SkColorMatrix m;
    const float gain = std::pow(2.f, p.exposure * 2);
    m.setScale(gain, gain, gain);
    const float c = 1 + p.contrast;
    SkColorMatrix contrast;
    contrast.setScale(c, c, c);
    contrast.postTranslate(0.5f * (1 - c), 0.5f * (1 - c), 0.5f * (1 - c), 0);
    m.postConcat(contrast);
    SkColorMatrix sat;
    sat.setSaturation(1 + p.saturation);
    m.postConcat(sat);
    SkColorMatrix warm;
    warm.setScale(1 + 0.2f * p.temperature, 1 - 0.2f * p.tint, 1 - 0.2f * p.temperature);
    m.postConcat(warm);
    sk_sp<SkColorFilter> f = SkColorFilters::Matrix(m);
    if (p.highlights || p.shadows) {
        // Tone curve: shadows move dark values, highlights move bright ones.
        uint8_t table[256];
        for (int i = 0; i < 256; ++i) {
            const float v = i / 255.f;
            float o = v + p.shadows * 0.5f * (1 - v) * (1 - v) * v * 4 + p.highlights * 0.5f * v * v * (1 - v) * 4;
            table[i] = uint8_t(std::clamp(o, 0.f, 1.f) * 255 + 0.5f);
        }
        f = SkColorFilters::Table(table)->makeComposed(f);
    }
    return f;
}

SkBlendMode toSkBlend(BlendMode m)
{
    switch (m) {
    case BlendMode::PassThrough:
    case BlendMode::Normal: return SkBlendMode::kSrcOver;
    case BlendMode::Darken: return SkBlendMode::kDarken;
    case BlendMode::Multiply: return SkBlendMode::kMultiply;
    case BlendMode::ColorBurn: return SkBlendMode::kColorBurn;
    case BlendMode::Lighten: return SkBlendMode::kLighten;
    case BlendMode::Screen: return SkBlendMode::kScreen;
    case BlendMode::ColorDodge: return SkBlendMode::kColorDodge;
    case BlendMode::Overlay: return SkBlendMode::kOverlay;
    case BlendMode::SoftLight: return SkBlendMode::kSoftLight;
    case BlendMode::HardLight: return SkBlendMode::kHardLight;
    case BlendMode::Difference: return SkBlendMode::kDifference;
    case BlendMode::Exclusion: return SkBlendMode::kExclusion;
    case BlendMode::Hue: return SkBlendMode::kHue;
    case BlendMode::Saturation: return SkBlendMode::kSaturation;
    case BlendMode::Color: return SkBlendMode::kColor;
    case BlendMode::Luminosity: return SkBlendMode::kLuminosity;
    }
    return SkBlendMode::kSrcOver;
}

namespace {

// Figma blur radius to Gaussian sigma.
float sigma(float radius) { return std::max(radius, 0.f) / 2.f; }

bool hasVisible(const std::vector<Paint>& v)
{
    for (const Paint& p : v)
        if (p.visible)
            return true;
    return false;
}
int countVisible(const std::vector<Paint>& v)
{
    int n = 0;
    for (const Paint& p : v)
        n += p.visible;
    return n;
}

bool isOpen(const SkPath& path)
{
    SkPath::Iter it(path, false);
    SkPoint pts[4];
    bool any = false;
    for (SkPath::Verb v; (v = it.next(pts)) != SkPath::kDone_Verb;) {
        if (v == SkPath::kMove_Verb)
            any = true;
    }
    return any && !path.isLastContourClosed();
}

sk_sp<SkRuntimeEffect> diamondEffect()
{
    static sk_sp<SkRuntimeEffect> effect = [] {
        auto r = SkRuntimeEffect::MakeForShader(SkString(R"(
            uniform shader grad;
            half4 main(float2 p) { return grad.eval(float2(abs(p.x) + abs(p.y), 0)); }
        )"));
        return r.effect;
    }();
    return effect;
}

// Maps unit gradient space (start at origin, end at (1,0)) into the node box.
bool gradientMatrix(const Paint& paint, const Props& p, SkMatrix* m)
{
    SkPoint s = {paint.gradStart.x() * p.width, paint.gradStart.y() * p.height};
    SkPoint e = {paint.gradEnd.x() * p.width, paint.gradEnd.y() * p.height};
    SkVector u = e - s;
    if (u.length() < 1e-4f)
        return false;
    SkVector v = SkVector{-u.y(), u.x()} * paint.gradWidth;
    *m = SkMatrix::MakeAll(u.x(), v.x(), s.x(), u.y(), v.y(), s.y(), 0, 0, 1);
    return true;
}

} // namespace

void Renderer::registerCodecs()
{
    static std::once_flag once;
    std::call_once(once, [] {
        SkCodecs::Register(SkPngDecoder::Decoder());
        SkCodecs::Register(SkJpegDecoder::Decoder());
        SkCodecs::Register(SkWebpDecoder::Decoder());
    });
}

Renderer::Renderer(const Document& doc)
    : m_doc(doc)
{
    registerCodecs();
}

void Renderer::setOptions(const Options& o)
{
    if (o.outlines != m_opts.outlines || o.showGrids != m_opts.showGrids)
        m_pics.clear();
    m_opts = o;
}

void Renderer::clear()
{
    m_pics.clear();
    m_paras.clear();
}

sk_sp<SkImage> Renderer::image(const QString& hash)
{
    auto it = m_images.find(hash);
    if (it != m_images.end())
        return it.value();
    sk_sp<SkImage> img;
    auto bytes = m_doc.images().value(hash);
    if (!bytes.isEmpty()) {
        img = SkImages::DeferredFromEncodedData(SkData::MakeWithCopy(bytes.constData(), bytes.size()));
        if (img)
            img = img->makeRasterImage(nullptr); // decode once, share across threads
    }
    m_images.insert(hash, img);
    return img;
}

skia::textlayout::Paragraph* Renderer::paragraph(NodeId id)
{
    const Node& n = m_doc.at(id);
    auto& e = m_paras[id];
    if (!e.para || e.version != n.version) {
        SkColor4f color = SkColors::kBlack;
        int visible = countVisible(n.p.fills);
        if (visible == 1) {
            for (const Paint& f : n.p.fills)
                if (f.visible && f.type == PaintType::Solid && f.blend == BlendMode::Normal) {
                    color = f.color;
                    color.fA *= f.opacity;
                }
        }
        float width = n.p.width;
        if (n.p.textStyle.autoResize == TextAutoResize::WidthAndHeight)
            width += 0.5f; // guard against wrapping from rounding
        e.para = text::build(n.p, width, color);
        e.version = n.version;
    }
    return e.para.get();
}

sk_sp<SkPicture> Renderer::recordPage(NodeId page)
{
    const Node& pg = m_doc.at(page);
    SkRect cull = SkRect::MakeEmpty();
    for (NodeId c : pg.children)
        cull.join(m_doc.visualBounds(c));
    cull.outset(64, 64);
    SkPictureRecorder rec;
    SkCanvas* c = rec.beginRecording(cull.isEmpty() ? SkRect::MakeWH(1, 1) : cull);
    drawChildren(c, pg);
    // Sweep cache entries of nodes that left the page.
    if (m_pics.size() > 4096 || m_paras.size() > 4096) {
        std::erase_if(m_pics, [&](auto& kv) { return !m_doc.isAttached(kv.first); });
        std::erase_if(m_paras, [&](auto& kv) { return !m_doc.isAttached(kv.first); });
    }
    return rec.finishRecordingAsPicture();
}

void Renderer::drawNode(SkCanvas* c, NodeId id, bool useCache)
{
    const Node& n = m_doc.at(id);
    if (!n.p.visible)
        return;
    if (useCache && !n.children.empty() && n.type() != NodeType::BooleanOp) {
        PicEntry& e = m_pics[id];
        if (!e.pic || e.version != n.subtreeVersion) {
            SkMatrix parentWorld = m_doc.worldTransform(n.parent), inv;
            SkRect cull = m_doc.visualBounds(id);
            if (parentWorld.invert(&inv))
                cull = inv.mapRect(cull);
            cull.outset(4, 4);
            SkPictureRecorder rec;
            SkCanvas* rc = rec.beginRecording(cull);
            rc->save();
            rc->concat(n.p.transform);
            drawContent(rc, n);
            rc->restore();
            e.pic = rec.finishRecordingAsPicture();
            e.version = n.subtreeVersion;
        }
        c->drawPicture(e.pic);
        return;
    }
    c->save();
    c->concat(n.p.transform);
    drawContent(c, n);
    c->restore();
}

void Renderer::drawNodeLocal(SkCanvas* c, NodeId id)
{
    const Node& n = m_doc.at(id);
    drawContent(c, n);
}

void Renderer::drawContent(SkCanvas* c, const Node& n)
{
    const Props& p = n.p;
    const NodeType t = n.type();

    if (m_opts.outlines) {
        SkPaint hp;
        hp.setStyle(SkPaint::kStroke_Style);
        hp.setStrokeWidth(0);
        hp.setAntiAlias(true);
        hp.setColor(SkColorSetARGB(255, 160, 160, 160));
        if (t == NodeType::Text) {
            paragraph(n.id)->paint(c, 0, text::verticalOffset(p, paragraph(n.id)->getHeight()));
        } else if (t != NodeType::Group) {
            c->drawPath(fillPath(m_doc, n.id), hp);
        }
        if (t != NodeType::BooleanOp)
            drawChildren(c, n);
        return;
    }

    const bool container = isFrameLike(t) || t == NodeType::Group;
    bool dropShadow = false, innerShadow = false, layerBlur = false, bgBlur = false;
    for (const Effect& e : p.effects) {
        if (!e.visible)
            continue;
        dropShadow |= e.type == EffectType::DropShadow;
        innerShadow |= e.type == EffectType::InnerShadow;
        layerBlur |= e.type == EffectType::LayerBlur;
        bgBlur |= e.type == EffectType::BackgroundBlur;
    }
    const SkBlendMode blend = toSkBlend(p.blend);
    const bool hasEffects = dropShadow || innerShadow || layerBlur || bgBlur;
    const int ops = countVisible(p.fills) + (p.stroke.weight > 0 ? countVisible(p.strokes) : 0);
    // Single-op leaves fold opacity into the paint instead of allocating a layer.
    const bool fold = !container && t != NodeType::Text && t != NodeType::BooleanOp && blend == SkBlendMode::kSrcOver
        && !hasEffects && ops <= 1;
    float alpha = fold ? p.opacity : 1.f;

    // Shadows of containers without their own fill come from rendered content.
    const bool contentShadow = dropShadow && (t == NodeType::Group || (container && !hasVisible(p.fills)));
    const bool layer = !fold && (p.opacity < 1 || blend != SkBlendMode::kSrcOver || layerBlur || contentShadow);
    if (layer) {
        SkPaint lp;
        lp.setAlphaf(p.opacity);
        lp.setBlendMode(blend);
        sk_sp<SkImageFilter> filter;
        if (contentShadow) {
            std::vector<sk_sp<SkImageFilter>> parts;
            for (const Effect& e : p.effects)
                if (e.visible && e.type == EffectType::DropShadow) {
                    sk_sp<SkImageFilter> in;
                    if (e.spread > 0)
                        in = SkImageFilters::Dilate(e.spread, e.spread, nullptr);
                    parts.push_back(SkImageFilters::DropShadowOnly(
                        e.offset.x(), e.offset.y(), sigma(e.radius), sigma(e.radius), e.color, nullptr, in));
                }
            parts.push_back(nullptr); // the content itself
            filter = SkImageFilters::Merge(parts.data(), (int)parts.size());
        }
        if (layerBlur)
            for (const Effect& e : p.effects)
                if (e.visible && e.type == EffectType::LayerBlur)
                    filter = SkImageFilters::Blur(sigma(e.radius), sigma(e.radius), SkTileMode::kDecal, filter);
        lp.setImageFilter(filter);
        c->saveLayer(nullptr, &lp);
    }

    if (t == NodeType::Group) {
        drawChildren(c, n);
    } else if (t == NodeType::Text) {
        if (dropShadow && !contentShadow) {
            // Text shadow follows glyphs: render through a layer filter.
            SkPaint lp;
            std::vector<sk_sp<SkImageFilter>> parts;
            for (const Effect& e : p.effects)
                if (e.visible && e.type == EffectType::DropShadow)
                    parts.push_back(SkImageFilters::DropShadowOnly(
                        e.offset.x(), e.offset.y(), sigma(e.radius), sigma(e.radius), e.color, nullptr, nullptr));
            parts.push_back(nullptr);
            lp.setImageFilter(SkImageFilters::Merge(parts.data(), (int)parts.size()));
            c->saveLayer(nullptr, &lp);
            drawText(c, n, alpha);
            c->restore();
        } else {
            drawText(c, n, alpha);
        }
    } else {
        const SkPath path = fillPath(m_doc, n.id);
        if (dropShadow && !contentShadow)
            drawDropShadows(c, n, path);
        if (bgBlur) {
            for (const Effect& e : p.effects)
                if (e.visible && e.type == EffectType::BackgroundBlur) {
                    c->save();
                    c->clipPath(path, true);
                    auto blur = SkImageFilters::Blur(sigma(e.radius), sigma(e.radius), SkTileMode::kClamp, nullptr);
                    SkCanvas::SaveLayerRec rec(nullptr, nullptr, blur.get(), 0);
                    c->saveLayer(rec);
                    c->restore();
                    c->restore();
                }
        }
        drawFills(c, n, path, alpha);
        if (innerShadow)
            drawInnerShadows(c, n, path);
        if (isFrameLike(t)) {
            drawGrids(c, n);
            if (p.clipsContent) {
                c->save();
                c->clipPath(path, true);
                drawChildren(c, n);
                c->restore();
            } else {
                drawChildren(c, n);
            }
        }
        drawStrokes(c, n, t == NodeType::Vector ? strokePath(m_doc, n.id) : path, alpha);
    }

    if (layer)
        c->restore();
}

void Renderer::drawChildren(SkCanvas* c, const Node& n)
{
    std::vector<NodeId> order = n.children;
    if (n.p.layout.mode != LayoutMode::None && n.p.layout.reverseZ)
        std::reverse(order.begin(), order.end());
    for (size_t i = 0; i < order.size(); ++i) {
        const Node& child = m_doc.at(order[i]);
        if (child.p.isMask && child.p.visible) {
            // A mask clips every sibling above it.
            c->saveLayer(nullptr, nullptr);
            for (size_t j = i + 1; j < order.size(); ++j)
                drawNode(c, order[j]);
            SkPaint mp;
            mp.setBlendMode(SkBlendMode::kDstIn);
            c->saveLayer(nullptr, &mp);
            drawNode(c, order[i], false);
            c->restore();
            c->restore();
            return;
        }
        drawNode(c, order[i]);
    }
}

bool Renderer::makePaint(const Paint& paint, const Props& p, float extraAlpha, SkPaint* out)
{
    if (!paint.visible)
        return false;
    out->setAntiAlias(true);
    out->setBlendMode(toSkBlend(paint.blend));
    const float a = paint.opacity * extraAlpha;
    if (paint.type == PaintType::Solid) {
        SkColor4f col = paint.color;
        col.fA *= a;
        out->setColor4f(col);
        return true;
    }
    if (paint.type == PaintType::Image) {
        sk_sp<SkImage> img = image(paint.imageHash);
        if (!img) {
            out->setColor4f({0.8f, 0.8f, 0.8f, a});
            return true;
        }
        const float iw = img->width(), ih = img->height();
        SkMatrix m;
        SkTileMode tile = SkTileMode::kDecal;
        switch (paint.scaleMode) {
        case ImageScaleMode::Crop: {
            // The crop rect (normalized image coordinates) fills the layer's box.
            const SkRect src = SkRect::MakeXYWH(paint.crop.left() * iw, paint.crop.top() * ih,
                std::max(paint.crop.width(), 1e-4f) * iw, std::max(paint.crop.height(), 1e-4f) * ih);
            m = SkMatrix::RectToRect(src, SkRect::MakeWH(p.width, p.height));
            break;
        }
        case ImageScaleMode::Fill: {
            float s = std::max(p.width / iw, p.height / ih);
            m = SkMatrix::Translate((p.width - iw * s) / 2, (p.height - ih * s) / 2);
            m.preScale(s, s);
            tile = SkTileMode::kClamp;
            break;
        }
        case ImageScaleMode::Fit: {
            float s = std::min(p.width / iw, p.height / ih);
            m = SkMatrix::Translate((p.width - iw * s) / 2, (p.height - ih * s) / 2);
            m.preScale(s, s);
            break;
        }
        case ImageScaleMode::Tile:
            m = SkMatrix::Scale(paint.imageScale, paint.imageScale);
            tile = SkTileMode::kRepeat;
            break;
        }
        out->setShader(img->makeShader(tile, tile, SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear), &m));
        out->setAlphaf(a);
        if (paint.hasAdjustments())
            out->setColorFilter(imageAdjustments(paint));
        return true;
    }
    std::vector<SkColor4f> colors;
    std::vector<float> pos;
    for (const ColorStop& s : paint.stops) {
        colors.push_back(s.color);
        pos.push_back(std::clamp(s.pos, 0.f, 1.f));
    }
    if (colors.size() < 2) {
        colors.push_back(colors.empty() ? SkColors::kBlack : colors.back());
        pos = {0, 1};
        if (colors.size() < 2)
            colors.push_back(SkColors::kBlack);
    }
    SkMatrix m;
    if (!gradientMatrix(paint, p, &m))
        return false;
    sk_sp<SkShader> sh;
    const int count = (int)colors.size();
    switch (paint.type) {
    case PaintType::LinearGradient: {
        SkPoint pts[2] = {{0, 0}, {1, 0}};
        sh = SkGradientShader::MakeLinear(pts, colors.data(), nullptr, pos.data(), count, SkTileMode::kClamp, 0, &m);
        break;
    }
    case PaintType::RadialGradient:
        sh = SkGradientShader::MakeRadial({0, 0}, 1, colors.data(), nullptr, pos.data(), count, SkTileMode::kClamp, 0, &m);
        break;
    case PaintType::AngularGradient:
        sh = SkGradientShader::MakeSweep(0, 0, colors.data(), nullptr, pos.data(), count, SkTileMode::kClamp, 0, 360, 0, &m);
        break;
    case PaintType::DiamondGradient: {
        SkPoint pts[2] = {{0, 0}, {1, 0}};
        sk_sp<SkShader> grad = SkGradientShader::MakeLinear(pts, colors.data(), nullptr, pos.data(), count, SkTileMode::kClamp);
        SkRuntimeEffect::ChildPtr children[] = {grad};
        if (auto eff = diamondEffect())
            sh = eff->makeShader(nullptr, children, &m);
        break;
    }
    default: break;
    }
    if (!sh)
        return false;
    out->setShader(sh);
    out->setAlphaf(a);
    return true;
}

void Renderer::drawFills(SkCanvas* c, const Node& n, const SkPath& path, float extraAlpha)
{
    if (n.type() == NodeType::Line)
        return;
    for (const Paint& f : n.p.fills) {
        SkPaint paint;
        if (makePaint(f, n.p, extraAlpha, &paint))
            c->drawPath(path, paint);
    }
}

void Renderer::drawText(SkCanvas* c, const Node& n, float extraAlpha)
{
    auto* para = paragraph(n.id);
    const float y = text::verticalOffset(n.p, para->getHeight());
    const int visible = countVisible(n.p.fills);
    bool simple = visible == 1;
    for (const Paint& f : n.p.fills)
        if (f.visible && (f.type != PaintType::Solid || f.blend != BlendMode::Normal))
            simple = false;
    if (visible == 0)
        return;
    if (simple && extraAlpha >= 1) {
        para->paint(c, 0, y);
        return;
    }
    // Use the glyphs as a mask for each fill.
    SkRect bounds = SkRect::MakeXYWH(-n.p.textStyle.size, y - n.p.textStyle.size, para->getMaxWidth() + n.p.textStyle.size * 2,
        para->getHeight() + n.p.textStyle.size * 2);
    c->saveLayer(bounds, nullptr);
    para->paint(c, 0, y);
    bool first = true;
    for (const Paint& f : n.p.fills) {
        SkPaint paint;
        if (!makePaint(f, n.p, extraAlpha, &paint))
            continue;
        paint.setBlendMode(first ? SkBlendMode::kSrcIn : SkBlendMode::kSrcATop);
        first = false;
        c->drawRect(bounds, paint);
    }
    c->restore();
}

void Renderer::drawStrokes(SkCanvas* c, const Node& n, const SkPath& path, float extraAlpha)
{
    const Props& p = n.p;
    if (p.stroke.weight <= 0 || !hasVisible(p.strokes))
        return;
    const bool open = n.type() == NodeType::Line || isOpen(path);
    StrokeAlign align = open ? StrokeAlign::Center : p.stroke.align;
    for (const Paint& s : p.strokes) {
        SkPaint sp;
        if (!makePaint(s, p, extraAlpha, &sp))
            continue;
        sp.setStyle(SkPaint::kStroke_Style);
        sp.setStrokeWidth(align == StrokeAlign::Center ? p.stroke.weight : p.stroke.weight * 2);
        sp.setStrokeJoin(p.stroke.join == StrokeJoin::Round ? SkPaint::kRound_Join
                : p.stroke.join == StrokeJoin::Bevel        ? SkPaint::kBevel_Join
                                                            : SkPaint::kMiter_Join);
        sp.setStrokeMiter(p.stroke.miterLimit);
        StrokeCap cap = p.stroke.startCap;
        sp.setStrokeCap(cap == StrokeCap::Round ? SkPaint::kRound_Cap
                : cap == StrokeCap::Square     ? SkPaint::kSquare_Cap
                                               : SkPaint::kButt_Cap);
        if (!p.stroke.dashes.empty()) {
            std::vector<float> d = p.stroke.dashes;
            if (d.size() % 2)
                d.insert(d.end(), p.stroke.dashes.begin(), p.stroke.dashes.end());
            sp.setPathEffect(SkDashPathEffect::Make(d.data(), (int)d.size(), 0));
        }
        if (align == StrokeAlign::Center) {
            c->drawPath(path, sp);
        } else {
            c->save();
            c->clipPath(path, align == StrokeAlign::Inside ? SkClipOp::kIntersect : SkClipOp::kDifference, true);
            c->drawPath(path, sp);
            c->restore();
        }
        if (open)
            drawArrowCaps(c, n, path, sp);
    }
}

void Renderer::drawArrowCaps(SkCanvas* c, const Node& n, const SkPath& path, const SkPaint& stroke)
{
    const StrokeStyle& st = n.p.stroke;
    auto isArrow = [](StrokeCap cap) {
        return cap == StrokeCap::LineArrow || cap == StrokeCap::TriangleArrow || cap == StrokeCap::CircleArrow
            || cap == StrokeCap::DiamondArrow;
    };
    if (!isArrow(st.startCap) && !isArrow(st.endCap))
        return;
    SkContourMeasureIter iter(path, false);
    sk_sp<SkContourMeasure> first, last;
    while (auto cm = iter.next()) {
        if (!first)
            first = cm;
        last = cm;
    }
    if (!first)
        return;
    const float w = st.weight;
    auto drawCap = [&](StrokeCap cap, SkPoint tip, SkVector dir) {
        dir.normalize();
        SkVector perp = {-dir.y(), dir.x()};
        const float len = std::max(w * 3.f, 6.f);
        SkPaint fill = stroke;
        fill.setPathEffect(nullptr);
        switch (cap) {
        case StrokeCap::LineArrow: {
            SkPath a;
            a.moveTo(tip - dir * len + perp * len * 0.8f);
            a.lineTo(tip);
            a.lineTo(tip - dir * len - perp * len * 0.8f);
            SkPaint ln = fill;
            ln.setStrokeWidth(w);
            ln.setStrokeJoin(SkPaint::kMiter_Join);
            c->drawPath(a, ln);
            break;
        }
        case StrokeCap::TriangleArrow: {
            SkPath a;
            a.moveTo(tip + dir * w * 0.5f);
            a.lineTo(tip - dir * len + perp * len * 0.6f);
            a.lineTo(tip - dir * len - perp * len * 0.6f);
            a.close();
            fill.setStyle(SkPaint::kFill_Style);
            c->drawPath(a, fill);
            break;
        }
        case StrokeCap::CircleArrow:
            fill.setStyle(SkPaint::kFill_Style);
            c->drawCircle(tip, len * 0.45f, fill);
            break;
        case StrokeCap::DiamondArrow: {
            SkPath a;
            const float r = len * 0.55f;
            a.moveTo(tip + dir * r);
            a.lineTo(tip + perp * r);
            a.lineTo(tip - dir * r);
            a.lineTo(tip - perp * r);
            a.close();
            fill.setStyle(SkPaint::kFill_Style);
            c->drawPath(a, fill);
            break;
        }
        default: break;
        }
    };
    SkPoint pos;
    SkVector tan;
    if (isArrow(st.startCap) && first->getPosTan(0, &pos, &tan))
        drawCap(st.startCap, pos, -tan);
    if (isArrow(st.endCap) && last->getPosTan(last->length(), &pos, &tan))
        drawCap(st.endCap, pos, tan);
}

void Renderer::drawDropShadows(SkCanvas* c, const Node& n, const SkPath& path)
{
    for (const Effect& e : n.p.effects) {
        if (!e.visible || e.type != EffectType::DropShadow)
            continue;
        SkPath shape = path;
        if (e.spread != 0) {
            SkPath grown;
            SkPaint sp;
            sp.setStyle(SkPaint::kStroke_Style);
            sp.setStrokeWidth(std::fabs(e.spread) * 2);
            sp.setStrokeJoin(SkPaint::kRound_Join);
            skpathutils::FillPathWithPaint(path, sp, &grown);
            SkPath out;
            if (Op(path, grown, e.spread > 0 ? kUnion_SkPathOp : kDifference_SkPathOp, &out))
                shape = out;
        }
        c->save();
        if (!e.showBehindTransparent)
            c->clipPath(path, SkClipOp::kDifference, true);
        SkPaint sp;
        sp.setAntiAlias(true);
        sp.setColor4f(e.color);
        sp.setBlendMode(toSkBlend(e.blend));
        if (e.radius > 0)
            sp.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma(e.radius)));
        c->translate(e.offset.x(), e.offset.y());
        c->drawPath(shape, sp);
        c->restore();
    }
}

void Renderer::drawInnerShadows(SkCanvas* c, const Node& n, const SkPath& path)
{
    for (const Effect& e : n.p.effects) {
        if (!e.visible || e.type != EffectType::InnerShadow)
            continue;
        SkPath hole = path;
        if (e.spread > 0) {
            SkPath ring;
            SkPaint sp;
            sp.setStyle(SkPaint::kStroke_Style);
            sp.setStrokeWidth(e.spread * 2);
            skpathutils::FillPathWithPaint(path, sp, &ring);
            SkPath out;
            if (Op(path, ring, kDifference_SkPathOp, &out))
                hole = out;
        }
        hole.offset(e.offset.x(), e.offset.y());
        SkRect outer = path.getBounds();
        outer.outset(e.radius * 3 + std::fabs(e.offset.x()) + std::fabs(e.offset.y()) + 10,
            e.radius * 3 + std::fabs(e.offset.x()) + std::fabs(e.offset.y()) + 10);
        SkPath frame;
        Op(SkPath::Rect(outer), hole, kDifference_SkPathOp, &frame);
        c->save();
        c->clipPath(path, true);
        SkPaint sp;
        sp.setAntiAlias(true);
        sp.setColor4f(e.color);
        sp.setBlendMode(toSkBlend(e.blend));
        if (e.radius > 0)
            sp.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma(e.radius)));
        c->drawPath(frame, sp);
        c->restore();
    }
}

void Renderer::drawGrids(SkCanvas* c, const Node& n)
{
    if (!m_opts.showGrids)
        return;
    for (const LayoutGrid& g : n.p.grids) {
        if (!g.visible)
            continue;
        SkPaint gp;
        gp.setColor4f(g.color);
        gp.setAntiAlias(false);
        const float w = n.p.width, h = n.p.height;
        if (g.kind == LayoutGrid::Kind::Grid) {
            gp.setStyle(SkPaint::kStroke_Style);
            gp.setStrokeWidth(0);
            for (float x = g.size; x < w; x += g.size)
                c->drawLine(x, 0, x, h, gp);
            for (float y = g.size; y < h; y += g.size)
                c->drawLine(0, y, w, y, gp);
            continue;
        }
        const bool cols = g.kind == LayoutGrid::Kind::Columns;
        const float total = cols ? w : h;
        const int count = std::max(1, g.count);
        const float cell = (total - 2 * g.margin - g.gutter * (count - 1)) / count;
        for (int i = 0; i < count; ++i) {
            float o = g.margin + i * (cell + g.gutter);
            c->drawRect(cols ? SkRect::MakeXYWH(o, 0, cell, h) : SkRect::MakeXYWH(0, o, w, cell), gp);
        }
    }
}

} // namespace drav
