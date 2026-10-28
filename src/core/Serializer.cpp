#include "Serializer.h"

#include "Document.h"
#include "Library.h"

#include <QCborArray>
#include <QCborValue>

namespace drav::io {

namespace {

QCborArray color(SkColor4f c) { return {c.fR, c.fG, c.fB, c.fA}; }
SkColor4f color(const QCborValue& v, SkColor4f def = SkColors::kBlack)
{
    QCborArray a = v.toArray();
    if (a.size() != 4)
        return def;
    return {float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()), float(a[3].toDouble())};
}
QCborArray point(SkPoint p) { return {p.x(), p.y()}; }
SkPoint point(const QCborValue& v)
{
    QCborArray a = v.toArray();
    return a.size() == 2 ? SkPoint{float(a[0].toDouble()), float(a[1].toDouble())} : SkPoint{0, 0};
}
QCborArray matrix(const SkMatrix& m)
{
    return {m.getScaleX(), m.getSkewX(), m.getTranslateX(), m.getSkewY(), m.getScaleY(), m.getTranslateY()};
}
SkMatrix matrix(const QCborValue& v)
{
    QCborArray a = v.toArray();
    if (a.size() != 6)
        return SkMatrix::I();
    auto f = [&](int i) { return float(a[i].toDouble()); };
    return SkMatrix::MakeAll(f(0), f(1), f(2), f(3), f(4), f(5), 0, 0, 1);
}
float num(const QCborMap& m, const char* k, float def = 0)
{
    QCborValue v = m.value(QLatin1String(k));
    return v.isDouble() || v.isInteger() ? float(v.toDouble()) : def;
}
int integer(const QCborMap& m, const char* k, int def = 0)
{
    QCborValue v = m.value(QLatin1String(k));
    return v.isInteger() ? int(v.toInteger()) : v.isDouble() ? int(v.toDouble()) : def;
}
bool flag(const QCborMap& m, const char* k, bool def = false)
{
    QCborValue v = m.value(QLatin1String(k));
    return v.isBool() ? v.toBool() : def;
}
QString str(const QCborMap& m, const char* k, const QString& def = {})
{
    QCborValue v = m.value(QLatin1String(k));
    return v.isString() ? v.toString() : def;
}
#define K(x) QStringLiteral(x)

QCborMap paintToCbor(const Paint& p)
{
    QCborMap m;
    m[K("type")] = int(p.type);
    m[K("color")] = color(p.color);
    m[K("opacity")] = p.opacity;
    m[K("visible")] = p.visible;
    m[K("blend")] = int(p.blend);
    if (p.type != PaintType::Solid && p.type != PaintType::Image) {
        m[K("start")] = point(p.gradStart);
        m[K("end")] = point(p.gradEnd);
        m[K("gradWidth")] = p.gradWidth;
        QCborArray stops;
        for (const ColorStop& s : p.stops)
            stops.append(QCborArray{s.pos, color(s.color)});
        m[K("stops")] = stops;
    }
    if (p.type == PaintType::Image) {
        m[K("image")] = p.imageHash;
        m[K("scaleMode")] = int(p.scaleMode);
        m[K("imageScale")] = p.imageScale;
        if (p.hasAdjustments())
            m[K("adjust")] = QCborArray{p.exposure, p.contrast, p.saturation, p.temperature, p.tint, p.highlights, p.shadows};
        if (p.crop != SkRect::MakeWH(1, 1))
            m[K("crop")] = QCborArray{p.crop.left(), p.crop.top(), p.crop.width(), p.crop.height()};
    }
    if (!p.colorVar.isEmpty())
        m[K("colorVar")] = p.colorVar;
    return m;
}
Paint paintFromCbor(const QCborMap& m)
{
    Paint p;
    p.type = PaintType(integer(m, "type"));
    p.color = color(m.value(K("color")), p.color);
    p.opacity = num(m, "opacity", 1);
    p.visible = flag(m, "visible", true);
    p.blend = BlendMode(integer(m, "blend", int(BlendMode::Normal)));
    if (m.contains(K("start"))) {
        p.gradStart = point(m.value(K("start")));
        p.gradEnd = point(m.value(K("end")));
        p.gradWidth = num(m, "gradWidth", 1);
        p.stops.clear();
        for (const QCborValue& s : m.value(K("stops")).toArray()) {
            QCborArray a = s.toArray();
            p.stops.push_back({float(a[0].toDouble()), color(a[1])});
        }
    }
    p.imageHash = str(m, "image");
    p.scaleMode = ImageScaleMode(integer(m, "scaleMode"));
    p.imageScale = num(m, "imageScale", 1);
    if (m.contains(K("adjust"))) {
        const QCborArray a = m.value(K("adjust")).toArray();
        float* f[] = {&p.exposure, &p.contrast, &p.saturation, &p.temperature, &p.tint, &p.highlights, &p.shadows};
        for (int i = 0; i < 7 && i < a.size(); ++i)
            *f[i] = float(a.at(i).toDouble());
    }
    if (m.contains(K("crop"))) {
        const QCborArray a = m.value(K("crop")).toArray();
        if (a.size() == 4)
            p.crop = SkRect::MakeXYWH(a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble());
    }
    p.colorVar = str(m, "colorVar");
    return p;
}
QCborArray paints(const std::vector<Paint>& v)
{
    QCborArray a;
    for (const Paint& p : v)
        a.append(paintToCbor(p));
    return a;
}
std::vector<Paint> paints(const QCborValue& v)
{
    std::vector<Paint> out;
    for (const QCborValue& p : v.toArray())
        out.push_back(paintFromCbor(p.toMap()));
    return out;
}

QCborMap vectorToCbor(const VectorData& v)
{
    QCborMap m;
    m[K("fill")] = int(v.fill);
    QCborArray cs;
    for (const Contour& c : v.contours) {
        QCborArray pts;
        for (const Anchor& a : c.pts)
            pts.append(QCborArray{a.p.x(), a.p.y(), a.in.x(), a.in.y(), a.out.x(), a.out.y(), int(a.mode), a.cornerRadius});
        cs.append(QCborMap{{K("closed"), c.closed}, {K("pts"), pts}});
    }
    m[K("contours")] = cs;
    if (v.isNetwork()) {
        QCborArray verts, segs, regions;
        for (const NetVertex& x : v.vertices)
            verts.append(QCborArray{x.p.x(), x.p.y(), int(x.mode)});
        for (const NetSegment& sg : v.segments)
            segs.append(QCborArray{sg.a, sg.b, sg.ca.x(), sg.ca.y(), sg.cb.x(), sg.cb.y()});
        for (const NetRegion& r : v.regions) {
            QCborArray loops;
            for (const auto& loop : r.loops) {
                QCborArray l;
                for (int i : loop)
                    l.append(i);
                loops.append(l);
            }
            regions.append(loops);
        }
        m[K("net")] = QCborMap{{K("vertices"), verts}, {K("segments"), segs}, {K("regions"), regions}};
    }
    return m;
}
VectorData vectorFromCbor(const QCborMap& m)
{
    VectorData v;
    v.fill = SkPathFillType(integer(m, "fill"));
    for (const QCborValue& cv : m.value(K("contours")).toArray()) {
        QCborMap cm = cv.toMap();
        Contour c;
        c.closed = flag(cm, "closed");
        for (const QCborValue& pv : cm.value(K("pts")).toArray()) {
            QCborArray a = pv.toArray();
            auto f = [&](int i) { return float(a[i].toDouble()); };
            Anchor an;
            an.p = {f(0), f(1)};
            an.in = {f(2), f(3)};
            an.out = {f(4), f(5)};
            an.mode = AnchorMode(a[6].toInteger());
            an.cornerRadius = a.size() > 7 ? f(7) : 0;
            c.pts.push_back(an);
        }
        v.contours.push_back(c);
    }
    if (m.contains(K("net"))) {
        const QCborMap net = m.value(K("net")).toMap();
        for (const QCborValue& xv : net.value(K("vertices")).toArray()) {
            const QCborArray a = xv.toArray();
            v.vertices.push_back({{float(a[0].toDouble()), float(a[1].toDouble())}, AnchorMode(a[2].toInteger())});
        }
        const int nv = int(v.vertices.size());
        for (const QCborValue& sv : net.value(K("segments")).toArray()) {
            const QCborArray a = sv.toArray();
            const int sa = int(a[0].toInteger()), sb = int(a[1].toInteger());
            if (sa < 0 || sb < 0 || sa >= nv || sb >= nv)
                continue; // damaged file: skip rather than index out of range
            v.segments.push_back({sa, sb, {float(a[2].toDouble()), float(a[3].toDouble())}, {float(a[4].toDouble()), float(a[5].toDouble())}});
        }
        const int ns = int(v.segments.size());
        for (const QCborValue& rv : net.value(K("regions")).toArray()) {
            NetRegion r;
            for (const QCborValue& lv : rv.toArray()) {
                std::vector<int> loop;
                for (const QCborValue& i : lv.toArray())
                    if (i.toInteger() >= 0 && i.toInteger() < ns)
                        loop.push_back(int(i.toInteger()));
                if (!loop.empty())
                    r.loops.push_back(loop);
            }
            v.regions.push_back(r);
        }
    }
    return v;
}

QCborMap effectToCbor(const Effect& e)
{
    QCborMap m{{K("type"), int(e.type)}, {K("visible"), e.visible}, {K("color"), color(e.color)},
        {K("offset"), point(e.offset)}, {K("radius"), e.radius}, {K("spread"), e.spread}, {K("blend"), int(e.blend)},
        {K("behind"), e.showBehindTransparent}};
    if (!e.colorVar.isEmpty())
        m[K("colorVar")] = e.colorVar;
    return m;
}
Effect effectFromCbor(const QCborMap& em)
{
    Effect e;
    e.type = EffectType(integer(em, "type"));
    e.visible = flag(em, "visible", true);
    e.color = color(em.value(K("color")), e.color);
    e.offset = point(em.value(K("offset")));
    e.radius = num(em, "radius");
    e.spread = num(em, "spread");
    e.blend = BlendMode(integer(em, "blend", int(BlendMode::Normal)));
    e.showBehindTransparent = flag(em, "behind");
    e.colorVar = str(em, "colorVar");
    return e;
}
QCborArray effects(const std::vector<Effect>& v)
{
    QCborArray a;
    for (const Effect& e : v)
        a.append(effectToCbor(e));
    return a;
}
std::vector<Effect> effects(const QCborValue& v)
{
    std::vector<Effect> out;
    for (const QCborValue& e : v.toArray())
        out.push_back(effectFromCbor(e.toMap()));
    return out;
}

QCborMap textStyleToCbor(const TextStyle& t)
{
    QCborMap m{{K("family"), t.family}, {K("weight"), t.weight}, {K("italic"), t.italic}, {K("size"), t.size},
        {K("lh"), t.lineHeight}, {K("lhPct"), t.lineHeightPercent}, {K("ls"), t.letterSpacing},
        {K("ps"), t.paragraphSpacing}, {K("ah"), int(t.alignH)}, {K("av"), int(t.alignV)},
        {K("resize"), int(t.autoResize)}, {K("case"), int(t.textCase)}, {K("deco"), int(t.decoration)},
        {K("maxLines"), t.maxLines}};
    if (!t.features.isEmpty()) {
        QCborMap f;
        for (auto it = t.features.begin(); it != t.features.end(); ++it)
            f[it.key()] = it.value();
        m[K("features")] = f;
    }
    if (!t.axes.isEmpty()) {
        QCborMap a;
        for (auto it = t.axes.begin(); it != t.axes.end(); ++it)
            a[it.key()] = it.value();
        m[K("axes")] = a;
    }
    return m;
}
TextStyle textStyleFromCbor(const QCborMap& f)
{
    TextStyle t;
    t.family = str(f, "family", t.family);
    t.weight = integer(f, "weight", 400);
    t.italic = flag(f, "italic");
    t.size = num(f, "size", 16);
    t.lineHeight = num(f, "lh");
    t.lineHeightPercent = flag(f, "lhPct");
    t.letterSpacing = num(f, "ls");
    t.paragraphSpacing = num(f, "ps");
    t.alignH = TextAlignH(integer(f, "ah"));
    t.alignV = TextAlignV(integer(f, "av"));
    t.autoResize = TextAutoResize(integer(f, "resize", int(TextAutoResize::WidthAndHeight)));
    t.textCase = TextCase(integer(f, "case"));
    t.decoration = TextDecoration(integer(f, "deco"));
    t.maxLines = integer(f, "maxLines");
    const QCborMap feats = f.value(K("features")).toMap();
    for (auto it = feats.begin(); it != feats.end(); ++it)
        t.features.insert(it.key().toString(), int(it.value().toInteger()));
    const QCborMap axes = f.value(K("axes")).toMap();
    for (auto it = axes.begin(); it != axes.end(); ++it)
        t.axes.insert(it.key().toString(), float(it.value().toDouble()));
    return t;
}

QCborArray grids(const std::vector<LayoutGrid>& v)
{
    QCborArray a;
    for (const LayoutGrid& g : v)
        a.append(QCborMap{{K("kind"), int(g.kind)}, {K("count"), g.count}, {K("size"), g.size}, {K("gutter"), g.gutter},
            {K("margin"), g.margin}, {K("color"), color(g.color)}, {K("visible"), g.visible}});
    return a;
}
std::vector<LayoutGrid> grids(const QCborValue& v)
{
    std::vector<LayoutGrid> out;
    for (const QCborValue& gv : v.toArray()) {
        QCborMap gm = gv.toMap();
        LayoutGrid g;
        g.kind = LayoutGrid::Kind(integer(gm, "kind"));
        g.count = integer(gm, "count", 12);
        g.size = num(gm, "size", 10);
        g.gutter = num(gm, "gutter", 20);
        g.margin = num(gm, "margin");
        g.color = color(gm.value(K("color")), g.color);
        g.visible = flag(gm, "visible", true);
        out.push_back(g);
    }
    return out;
}

QCborMap stringMap(const QMap<QString, QString>& m)
{
    QCborMap c;
    for (auto it = m.begin(); it != m.end(); ++it)
        c[it.key()] = it.value();
    return c;
}
QMap<QString, QString> stringMap(const QCborValue& v)
{
    QMap<QString, QString> m;
    const QCborMap c = v.toMap();
    for (auto it = c.begin(); it != c.end(); ++it)
        m.insert(it.key().toString(), it.value().toString());
    return m;
}

} // namespace

QCborMap propsToCbor(const Props& p)
{
    QCborMap m;
    m[K("type")] = QString::fromLatin1(nodeTypeName(p.type));
    m[K("name")] = p.name;
    if (!p.visible) m[K("visible")] = false;
    if (p.locked) m[K("locked")] = true;
    if (p.opacity != 1) m[K("opacity")] = p.opacity;
    m[K("blend")] = int(p.blend);
    if (p.isMask) m[K("isMask")] = true;
    m[K("transform")] = matrix(p.transform);
    m[K("w")] = p.width;
    m[K("h")] = p.height;
    if (p.constrainProportions) m[K("lockRatio")] = true;
    m[K("cH")] = int(p.constraintH);
    m[K("cV")] = int(p.constraintV);
    m[K("fills")] = paints(p.fills);
    m[K("strokes")] = paints(p.strokes);
    {
        const StrokeStyle& s = p.stroke;
        QCborArray dashes;
        for (float d : s.dashes)
            dashes.append(d);
        m[K("stroke")] = QCborMap{{K("weight"), s.weight}, {K("align"), int(s.align)}, {K("startCap"), int(s.startCap)},
            {K("endCap"), int(s.endCap)}, {K("join"), int(s.join)}, {K("miter"), s.miterLimit}, {K("dashes"), dashes}};
    }
    m[K("effects")] = effects(p.effects);
    m[K("corners")] = QCborArray{p.corners.tl, p.corners.tr, p.corners.br, p.corners.bl};
    if (p.cornerSmoothing) m[K("smoothing")] = p.cornerSmoothing;
    m[K("clips")] = p.clipsContent;
    {
        const AutoLayout& l = p.layout;
        m[K("layout")] = QCborMap{{K("mode"), int(l.mode)}, {K("wrap"), l.wrap}, {K("spacing"), l.spacing},
            {K("cspacing"), l.counterSpacing}, {K("pad"), QCborArray{l.padL, l.padT, l.padR, l.padB}},
            {K("palign"), int(l.primaryAlign)}, {K("calign"), int(l.counterAlign)}, {K("reverseZ"), l.reverseZ},
            {K("strokesIncluded"), l.strokesIncluded}, {K("gridCols"), l.gridColumns}, {K("gridRows"), l.gridRows}};
    }
    {
        const LayoutChild& c = p.layoutChild;
        m[K("lchild")] = QCborMap{{K("sh"), int(c.sizingH)}, {K("sv"), int(c.sizingV)}, {K("abs"), c.absolute},
            {K("min"), QCborArray{c.minW, c.minH}}, {K("max"), QCborArray{c.maxW, c.maxH}}, {K("span"), QCborArray{c.colSpan, c.rowSpan}}};
    }
    if (!p.grids.empty())
        m[K("grids")] = grids(p.grids);
    if (p.type == NodeType::Ellipse)
        m[K("arc")] = QCborArray{p.arcStart, p.arcSweep, p.arcInner};
    if (p.type == NodeType::Polygon || p.type == NodeType::Star) {
        m[K("points")] = p.pointCount;
        m[K("inner")] = p.starInner;
    }
    if (p.type == NodeType::Vector || !p.vector.contours.empty() || p.vector.isNetwork())
        m[K("vector")] = vectorToCbor(p.vector);
    if (p.type == NodeType::Text) {
        m[K("text")] = p.text;
        m[K("font")] = textStyleToCbor(p.textStyle);
        if (!p.textRuns.empty()) {
            QCborArray runs;
            for (const TextRun& r : p.textRuns) {
                QCborMap rm{{K("s"), r.start}, {K("e"), r.end}, {K("mask"), qint64(r.mask)}};
                if (r.mask & RunFamily) rm[K("family")] = r.family;
                if (r.mask & RunWeight) rm[K("weight")] = r.weight;
                if (r.mask & RunItalic) rm[K("italic")] = r.italic;
                if (r.mask & RunSize) rm[K("size")] = r.size;
                if (r.mask & RunLetterSpacing) rm[K("ls")] = r.letterSpacing;
                if (r.mask & RunDecoration) rm[K("deco")] = int(r.decoration);
                if (r.mask & RunCase) rm[K("case")] = int(r.textCase);
                if (r.mask & RunFill) rm[K("fill")] = color(r.fill);
                if (r.mask & RunLink) rm[K("link")] = r.link;
                runs.append(rm);
            }
            m[K("runs")] = runs;
        }
        if (!p.paragraphs.empty()) {
            QCborArray paras;
            for (const ParagraphAttr& a : p.paragraphs)
                paras.append(QCborArray{int(a.list), a.indent});
            m[K("paragraphs")] = paras;
        }
    }
    if (p.type == NodeType::BooleanOp)
        m[K("boolOp")] = int(p.boolOp);
    if (p.componentId) m[K("componentId")] = qint64(p.componentId);
    if (p.sourceId) m[K("sourceId")] = qint64(p.sourceId);
    if (p.overrides) m[K("overrides")] = qint64(p.overrides);
    if (!p.description.isEmpty()) m[K("description")] = p.description;
    if (!p.variantProps.isEmpty()) {
        QCborMap vp;
        for (auto it = p.variantProps.begin(); it != p.variantProps.end(); ++it)
            vp[it.key()] = it.value();
        m[K("variantProps")] = vp;
    }
    if (!p.exports.empty()) {
        QCborArray ex;
        for (const ExportSetting& e : p.exports)
            ex.append(QCborMap{{K("format"), e.format}, {K("scale"), e.scale}, {K("suffix"), e.suffix}});
        m[K("exports")] = ex;
    }
    if (p.type == NodeType::Page)
        m[K("background")] = color(p.pageBackground);
    if (!p.libraryPage.isEmpty())
        m[K("libraryPage")] = p.libraryPage;
    if (!p.remote.isEmpty())
        m[K("remote")] = p.remote;
    if (!p.guides.empty()) {
        QCborArray gs;
        for (const Guide& g : p.guides)
            gs.append(QCborArray{g.vertical, g.pos});
        m[K("guides")] = gs;
    }
    const QString* styleIds[] = {&p.fillStyle, &p.strokeStyle, &p.textStyleId, &p.effectStyle, &p.gridStyle};
    const char* styleKeys[] = {"fillStyle", "strokeStyle", "textStyle", "effectStyle", "gridStyle"};
    for (int i = 0; i < 5; ++i)
        if (!styleIds[i]->isEmpty())
            m[QLatin1String(styleKeys[i])] = *styleIds[i];
    if (!p.bindings.isEmpty())
        m[K("bindings")] = stringMap(p.bindings);
    if (!p.componentProps.empty()) {
        QCborArray defs;
        for (const ComponentProp& d : p.componentProps) {
            QCborArray preferred;
            for (auto id : d.preferred)
                preferred.append(qint64(id));
            defs.append(QCborMap{{K("id"), d.id}, {K("name"), d.name}, {K("type"), int(d.type)},
                {K("default"), d.defaultValue}, {K("preferred"), preferred}});
        }
        m[K("componentProps")] = defs;
    }
    if (!p.propRefs.isEmpty())
        m[K("propRefs")] = stringMap(p.propRefs);
    if (!p.propValues.isEmpty())
        m[K("propValues")] = stringMap(p.propValues);
    if (!p.variableModes.isEmpty())
        m[K("variableModes")] = stringMap(p.variableModes);
    if (p.library && !p.library->empty())
        m[K("library")] = libraryToCbor(*p.library);
    return m;
}

Props propsFromCbor(const QCborMap& m)
{
    Props p;
    p.type = nodeTypeFromName(str(m, "type"));
    p.name = str(m, "name");
    p.visible = flag(m, "visible", true);
    p.locked = flag(m, "locked");
    p.opacity = num(m, "opacity", 1);
    p.blend = BlendMode(integer(m, "blend", int(BlendMode::PassThrough)));
    p.isMask = flag(m, "isMask");
    p.transform = matrix(m.value(K("transform")));
    p.width = num(m, "w", 100);
    p.height = num(m, "h", 100);
    p.constrainProportions = flag(m, "lockRatio");
    p.constraintH = ConstraintH(integer(m, "cH"));
    p.constraintV = ConstraintV(integer(m, "cV"));
    p.fills = paints(m.value(K("fills")));
    p.strokes = paints(m.value(K("strokes")));
    {
        QCborMap s = m.value(K("stroke")).toMap();
        p.stroke.weight = num(s, "weight", 1);
        p.stroke.align = StrokeAlign(integer(s, "align"));
        p.stroke.startCap = StrokeCap(integer(s, "startCap"));
        p.stroke.endCap = StrokeCap(integer(s, "endCap"));
        p.stroke.join = StrokeJoin(integer(s, "join"));
        p.stroke.miterLimit = num(s, "miter", 4);
        for (const QCborValue& d : s.value(K("dashes")).toArray())
            p.stroke.dashes.push_back(float(d.toDouble()));
    }
    p.effects = effects(m.value(K("effects")));
    {
        QCborArray c = m.value(K("corners")).toArray();
        if (c.size() == 4)
            p.corners = {float(c[0].toDouble()), float(c[1].toDouble()), float(c[2].toDouble()), float(c[3].toDouble())};
    }
    p.cornerSmoothing = num(m, "smoothing");
    p.clipsContent = flag(m, "clips", true);
    {
        QCborMap l = m.value(K("layout")).toMap();
        p.layout.mode = LayoutMode(integer(l, "mode"));
        p.layout.wrap = flag(l, "wrap");
        p.layout.spacing = num(l, "spacing");
        p.layout.counterSpacing = num(l, "cspacing");
        QCborArray pad = l.value(K("pad")).toArray();
        if (pad.size() == 4) {
            p.layout.padL = pad[0].toDouble();
            p.layout.padT = pad[1].toDouble();
            p.layout.padR = pad[2].toDouble();
            p.layout.padB = pad[3].toDouble();
        }
        p.layout.primaryAlign = LayoutAlign(integer(l, "palign"));
        p.layout.counterAlign = LayoutAlign(integer(l, "calign"));
        p.layout.reverseZ = flag(l, "reverseZ");
        p.layout.strokesIncluded = flag(l, "strokesIncluded");
        p.layout.gridColumns = integer(l, "gridCols", 2);
        p.layout.gridRows = integer(l, "gridRows");
    }
    {
        QCborMap c = m.value(K("lchild")).toMap();
        p.layoutChild.sizingH = LayoutSizing(integer(c, "sh"));
        p.layoutChild.sizingV = LayoutSizing(integer(c, "sv"));
        p.layoutChild.absolute = flag(c, "abs");
        QCborArray mn = c.value(K("min")).toArray(), mx = c.value(K("max")).toArray();
        if (mn.size() == 2) {
            p.layoutChild.minW = mn[0].toDouble();
            p.layoutChild.minH = mn[1].toDouble();
        }
        if (mx.size() == 2) {
            p.layoutChild.maxW = mx[0].toDouble();
            p.layoutChild.maxH = mx[1].toDouble();
        }
        const QCborArray span = c.value(K("span")).toArray();
        if (span.size() == 2) {
            p.layoutChild.colSpan = int(span[0].toInteger());
            p.layoutChild.rowSpan = int(span[1].toInteger());
        }
    }
    p.grids = grids(m.value(K("grids")));
    if (m.contains(K("arc"))) {
        QCborArray a = m.value(K("arc")).toArray();
        p.arcStart = a[0].toDouble();
        p.arcSweep = a[1].toDouble();
        p.arcInner = a[2].toDouble();
    }
    p.pointCount = integer(m, "points", 5);
    p.starInner = num(m, "inner", 0.382f);
    if (m.contains(K("vector")))
        p.vector = vectorFromCbor(m.value(K("vector")).toMap());
    p.text = str(m, "text");
    if (m.contains(K("font")))
        p.textStyle = textStyleFromCbor(m.value(K("font")).toMap());
    for (const QCborValue& rv : m.value(K("runs")).toArray()) {
        const QCborMap rm = rv.toMap();
        TextRun r;
        r.start = integer(rm, "s");
        r.end = integer(rm, "e");
        r.mask = std::uint32_t(rm.value(K("mask")).toInteger());
        r.family = str(rm, "family");
        r.weight = integer(rm, "weight", 400);
        r.italic = flag(rm, "italic");
        r.size = num(rm, "size", 16);
        r.letterSpacing = num(rm, "ls");
        r.decoration = TextDecoration(integer(rm, "deco"));
        r.textCase = TextCase(integer(rm, "case"));
        r.fill = color(rm.value(K("fill")), r.fill);
        r.link = str(rm, "link");
        p.textRuns.push_back(r);
    }
    for (const QCborValue& av : m.value(K("paragraphs")).toArray()) {
        const QCborArray a = av.toArray();
        p.paragraphs.push_back({ListType(a.at(0).toInteger()), int(a.at(1).toInteger())});
    }
    p.boolOp = BooleanOpType(integer(m, "boolOp"));
    p.componentId = NodeId(m.value(K("componentId")).toInteger());
    p.sourceId = NodeId(m.value(K("sourceId")).toInteger());
    p.overrides = std::uint32_t(m.value(K("overrides")).toInteger());
    p.description = str(m, "description");
    QCborMap vp = m.value(K("variantProps")).toMap();
    for (auto it = vp.begin(); it != vp.end(); ++it)
        p.variantProps.insert(it.key().toString(), it.value().toString());
    for (const QCborValue& ev : m.value(K("exports")).toArray()) {
        QCborMap em = ev.toMap();
        p.exports.push_back({str(em, "format", K("PNG")), num(em, "scale", 1), str(em, "suffix")});
    }
    if (m.contains(K("background")))
        p.pageBackground = color(m.value(K("background")), p.pageBackground);
    p.libraryPage = str(m, "libraryPage");
    p.remote = str(m, "remote");
    for (const QCborValue& gv : m.value(K("guides")).toArray()) {
        const QCborArray a = gv.toArray();
        p.guides.push_back({a.at(0).toBool(), float(a.at(1).toDouble())});
    }
    p.fillStyle = str(m, "fillStyle");
    p.strokeStyle = str(m, "strokeStyle");
    p.textStyleId = str(m, "textStyle");
    p.effectStyle = str(m, "effectStyle");
    p.gridStyle = str(m, "gridStyle");
    p.bindings = stringMap(m.value(K("bindings")));
    for (const QCborValue& dv : m.value(K("componentProps")).toArray()) {
        const QCborMap dm = dv.toMap();
        ComponentProp d;
        d.id = str(dm, "id");
        d.name = str(dm, "name");
        d.type = ComponentPropType(integer(dm, "type"));
        d.defaultValue = str(dm, "default");
        for (const QCborValue& x : dm.value(K("preferred")).toArray())
            d.preferred.push_back(NodeId(x.toInteger()));
        p.componentProps.push_back(d);
    }
    p.propRefs = stringMap(m.value(K("propRefs")));
    p.propValues = stringMap(m.value(K("propValues")));
    p.variableModes = stringMap(m.value(K("variableModes")));
    if (m.contains(K("library")))
        p.library = std::make_shared<Library>(libraryFromCbor(m.value(K("library")).toMap()));
    return p;
}

QCborMap libraryToCbor(const Library& lib)
{
    QCborArray styles;
    for (const Style& st : lib.styles) {
        QCborMap m{{K("id"), st.id}, {K("name"), st.name}, {K("type"), QString::fromLatin1(styleTypeName(st.type))}};
        if (!st.remote.isEmpty())
            m[K("remote")] = st.remote;
        if (!st.description.isEmpty())
            m[K("description")] = st.description;
        switch (st.type) {
        case StyleType::Fill: m[K("paints")] = paints(st.paints); break;
        case StyleType::Text: m[K("text")] = textStyleToCbor(st.text); break;
        case StyleType::Effect: m[K("effects")] = effects(st.effects); break;
        case StyleType::Grid: m[K("grids")] = grids(st.grids); break;
        }
        styles.append(m);
    }
    QCborArray collections;
    for (const VarCollection& c : lib.collections) {
        QCborArray modes;
        for (const VarMode& md : c.modes)
            modes.append(QCborArray{md.id, md.name});
        QCborMap cm{{K("id"), c.id}, {K("name"), c.name}, {K("modes"), modes}};
        if (!c.remote.isEmpty())
            cm[K("remote")] = c.remote;
        collections.append(cm);
    }
    QCborArray variables;
    for (const Variable& v : lib.variables) {
        QCborMap values;
        for (auto it = v.values.begin(); it != v.values.end(); ++it) {
            const VarValue& x = it.value();
            QCborMap vm;
            if (!x.alias.isEmpty()) {
                vm[K("alias")] = x.alias;
            } else {
                switch (v.type) {
                case VarType::Color: vm[K("color")] = color(x.color); break;
                case VarType::Number: vm[K("number")] = x.number; break;
                case VarType::String: vm[K("string")] = x.string; break;
                case VarType::Boolean: vm[K("boolean")] = x.boolean; break;
                }
            }
            values[it.key()] = vm;
        }
        QCborMap m{{K("id"), v.id}, {K("name"), v.name}, {K("collection"), v.collection},
            {K("type"), QString::fromLatin1(varTypeName(v.type))}, {K("values"), values}};
        if (!v.description.isEmpty())
            m[K("description")] = v.description;
        if (!v.remote.isEmpty())
            m[K("remote")] = v.remote;
        variables.append(m);
    }
    QCborArray links;
    for (const LibraryLink& l : lib.links)
        links.append(QCborMap{{K("id"), l.id}, {K("path"), l.path}, {K("name"), l.name}, {K("hash"), l.hash}});
    QCborMap out{{K("styles"), styles}, {K("collections"), collections}, {K("variables"), variables}};
    if (!links.isEmpty())
        out[K("links")] = links;
    return out;
}

Library libraryFromCbor(const QCborMap& m)
{
    Library lib;
    for (const QCborValue& sv : m.value(K("styles")).toArray()) {
        const QCborMap sm = sv.toMap();
        Style st;
        st.id = str(sm, "id");
        st.name = str(sm, "name");
        st.type = styleTypeFromName(str(sm, "type"));
        st.description = str(sm, "description");
        st.remote = str(sm, "remote");
        st.paints = paints(sm.value(K("paints")));
        if (sm.contains(K("text")))
            st.text = textStyleFromCbor(sm.value(K("text")).toMap());
        st.effects = effects(sm.value(K("effects")));
        st.grids = grids(sm.value(K("grids")));
        lib.styles.push_back(st);
    }
    for (const QCborValue& cv : m.value(K("collections")).toArray()) {
        const QCborMap cm = cv.toMap();
        VarCollection c;
        c.id = str(cm, "id");
        c.name = str(cm, "name");
        c.remote = str(cm, "remote");
        for (const QCborValue& mv : cm.value(K("modes")).toArray()) {
            const QCborArray a = mv.toArray();
            c.modes.push_back({a.at(0).toString(), a.at(1).toString()});
        }
        if (c.modes.empty())
            c.modes.push_back({Library::newId(), QStringLiteral("Mode 1")});
        lib.collections.push_back(c);
    }
    for (const QCborValue& vv : m.value(K("variables")).toArray()) {
        const QCborMap vm = vv.toMap();
        Variable v;
        v.id = str(vm, "id");
        v.name = str(vm, "name");
        v.collection = str(vm, "collection");
        v.type = varTypeFromName(str(vm, "type"));
        v.description = str(vm, "description");
        v.remote = str(vm, "remote");
        const QCborMap values = vm.value(K("values")).toMap();
        for (auto it = values.begin(); it != values.end(); ++it) {
            const QCborMap xm = it.value().toMap();
            VarValue x;
            x.alias = str(xm, "alias");
            x.color = color(xm.value(K("color")), x.color);
            x.number = num(xm, "number");
            x.string = str(xm, "string");
            x.boolean = flag(xm, "boolean");
            v.values.insert(it.key().toString(), x);
        }
        lib.variables.push_back(v);
    }
    for (const QCborValue& lv : m.value(K("links")).toArray()) {
        const QCborMap lm = lv.toMap();
        lib.links.push_back({str(lm, "id"), str(lm, "path"), str(lm, "name"), str(lm, "hash")});
    }
    return lib;
}

static void writeTree(const Document& doc, NodeId id, QCborArray& out)
{
    const Node& n = doc.at(id);
    QCborMap m = propsToCbor(n.p);
    m[K("id")] = qint64(id);
    m[K("parent")] = qint64(n.parent);
    out.append(m);
    for (NodeId c : n.children)
        writeTree(doc, c, out);
}

QByteArray save(const Document& doc)
{
    QCborArray nodes;
    writeTree(doc, doc.root(), nodes);
    // Instances whose main component lives in a deleted subtree keep rendering; nothing to do here.
    QCborMap images;
    for (auto it = doc.images().begin(); it != doc.images().end(); ++it)
        images[it.key()] = it.value();
    QCborMap root{{K("version"), kFormatVersion}, {K("root"), qint64(doc.root())}, {K("nodes"), nodes},
        {K("images"), images}, {K("currentPage"), qint64(doc.currentPage())}};
    return QByteArray("DRAV") + root.toCborValue().toCbor();
}

bool load(Document& doc, const QByteArray& data, QString* error)
{
    if (!data.startsWith("DRAV")) {
        if (error)
            *error = QStringLiteral("Not a drav file");
        return false;
    }
    QCborParserError perr;
    QCborValue v = QCborValue::fromCbor(data.mid(4), &perr);
    if (perr.error != QCborError::NoError || !v.isMap()) {
        if (error)
            *error = perr.errorString();
        return false;
    }
    QCborMap m = v.toMap();
    if (integer(m, "version") > kFormatVersion) {
        if (error)
            *error = QStringLiteral("File was written by a newer version");
        return false;
    }
    QCborArray nodes = m.value(K("nodes")).toArray();
    NodeId root = NodeId(m.value(K("root")).toInteger());
    struct Entry {
        NodeId id, parent;
    };
    std::vector<Entry> entries;
    std::unordered_map<NodeId, Props> props;
    for (const QCborValue& nv : nodes) {
        QCborMap nm = nv.toMap();
        NodeId id = NodeId(nm.value(K("id")).toInteger());
        entries.push_back({id, NodeId(nm.value(K("parent")).toInteger())});
        props.emplace(id, propsFromCbor(nm));
    }
    doc.clearForLoad();
    for (const Entry& e : entries)
        doc.rawCreate(e.id, props[e.id]);
    for (const Entry& e : entries)
        if (e.parent && doc.exists(e.parent))
            doc.rawLink(e.id, e.parent);
    doc.setRoot(root);
    QCborMap images = m.value(K("images")).toMap();
    for (auto it = images.begin(); it != images.end(); ++it)
        doc.addImage(it.value().toByteArray());
    doc.finishLoad();
    NodeId page = NodeId(m.value(K("currentPage")).toInteger());
    if (doc.exists(page))
        doc.setCurrentPage(page);
    doc.markSaved();
    return true;
}

QByteArray copy(const Document& doc, const std::vector<NodeId>& ids)
{
    QCborArray nodes;
    QCborArray roots;
    QCborMap images;
    for (NodeId id : ids) {
        roots.append(qint64(id));
        QCborArray sub;
        writeTree(doc, id, sub);
        for (const QCborValue& v : sub) {
            nodes.append(v);
            for (const QCborValue& f : v.toMap().value(K("fills")).toArray()) {
                QString h = f.toMap().value(K("image")).toString();
                if (!h.isEmpty() && doc.images().contains(h))
                    images[h] = doc.images().value(h);
            }
        }
    }
    QCborMap m{{K("roots"), roots}, {K("nodes"), nodes}, {K("images"), images}};
    return m.toCborValue().toCbor();
}

std::vector<NodeId> paste(Document& doc, const QByteArray& data, NodeId parent, int index)
{
    QCborMap m = QCborValue::fromCbor(data).toMap();
    QCborMap images = m.value(K("images")).toMap();
    for (auto it = images.begin(); it != images.end(); ++it)
        doc.addImage(it.value().toByteArray());

    std::unordered_map<NodeId, NodeId> remap;
    std::unordered_set<NodeId> rootSet;
    for (const QCborValue& r : m.value(K("roots")).toArray())
        rootSet.insert(NodeId(r.toInteger()));
    std::vector<NodeId> out;
    std::vector<std::pair<NodeId, Props>> created;
    for (const QCborValue& nv : m.value(K("nodes")).toArray()) {
        QCborMap nm = nv.toMap();
        NodeId oldId = NodeId(nm.value(K("id")).toInteger());
        NodeId oldParent = NodeId(nm.value(K("parent")).toInteger());
        Props p = propsFromCbor(nm);
        NodeId target = parent;
        int idx = -1;
        if (!rootSet.count(oldId) || remap.count(oldParent)) {
            auto it = remap.find(oldParent);
            if (it != remap.end())
                target = it->second;
        } else {
            idx = index < 0 ? -1 : index + int(out.size());
        }
        NodeId nid = doc.create(p, target, idx);
        remap[oldId] = nid;
        if (rootSet.count(oldId) && target == parent)
            out.push_back(nid);
        created.emplace_back(nid, p);
    }
    // Pasted copies of a main component become new components; copies inside instances keep their links.
    for (auto& [nid, p] : created) {
        bool touch = false;
        Props np = p;
        if (np.componentId && remap.count(np.componentId)) {
            np.componentId = remap[np.componentId];
            touch = true;
        }
        if (np.sourceId && remap.count(np.sourceId)) {
            np.sourceId = remap[np.sourceId];
            touch = true;
        } else if (np.sourceId && !doc.isInsideInstance(nid)) {
            // Copied out of an instance: becomes a plain layer.
            np.sourceId = kNoNode;
            if (np.type != NodeType::Instance)
                np.overrides = 0;
            touch = true;
        }
        if (touch)
            doc.edit(nid) = np;
    }
    return out;
}

} // namespace drav::io
