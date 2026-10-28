#include "FigImport.h"

#include "Geometry.h"
#include "Library.h"
#include "Text.h"
#include "Zip.h"

#include <include/core/SkPathBuilder.h>

#include <cmath>
#include <cstring>
#include <functional>

namespace drav::fig {

// ================================================================ kiwi

namespace {

class Reader {
public:
    explicit Reader(const QByteArray& d)
        : m_d(d)
    {
    }
    bool ok() const { return m_ok; }
    bool atEnd() const { return m_pos >= m_d.size(); }
    quint8 byte()
    {
        if (m_pos >= m_d.size()) {
            m_ok = false;
            return 0;
        }
        return quint8(m_d[m_pos++]);
    }
    bool boolean() { return byte() != 0; }
    quint32 uint()
    {
        quint32 v = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            const quint8 b = byte();
            v |= quint32(b & 127) << shift;
            if (!(b & 128) || !m_ok)
                break;
        }
        return v;
    }
    quint64 uint64()
    {
        quint64 v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            const quint8 b = byte();
            v |= quint64(b & 127) << shift;
            if (!(b & 128) || !m_ok)
                break;
        }
        return v;
    }
    qint32 sint()
    {
        const quint32 v = uint();
        return (v & 1) ? ~qint32(v >> 1) : qint32(v >> 1);
    }
    qint64 sint64()
    {
        const quint64 v = uint64();
        return (v & 1) ? ~qint64(v >> 1) : qint64(v >> 1);
    }
    float real()
    {
        // kiwi stores floats with the exponent first so small integers encode as one byte.
        const quint8 b0 = byte();
        if (b0 == 0)
            return 0.f;
        quint32 bits = b0 | quint32(byte()) << 8 | quint32(byte()) << 16 | quint32(byte()) << 24;
        bits = (bits << 23) | (bits >> 9);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    QString string()
    {
        const qsizetype start = m_pos;
        while (m_pos < m_d.size() && m_d[m_pos] != 0)
            ++m_pos;
        if (m_pos >= m_d.size()) {
            m_ok = false;
            return {};
        }
        const QString s = QString::fromUtf8(m_d.constData() + start, m_pos - start);
        ++m_pos;
        return s;
    }
    QByteArray bytes(quint32 n)
    {
        if (m_pos + n > m_d.size()) {
            m_ok = false;
            return {};
        }
        const QByteArray b = m_d.mid(m_pos, n);
        m_pos += n;
        return b;
    }
    void fail() { m_ok = false; }

private:
    const QByteArray& m_d;
    qsizetype m_pos = 0;
    bool m_ok = true;
};

class Decoder {
public:
    Decoder(const Schema& s, Reader& r)
        : m_s(s)
        , m_r(r)
    {
    }

    QVariant value(int type, bool array)
    {
        if (array) {
            const quint32 n = m_r.uint();
            if (type == -2) // byte[]: blob data
                return m_r.bytes(n);
            QVariantList l;
            for (quint32 i = 0; i < n && m_r.ok(); ++i)
                l << value(type, false);
            return l;
        }
        if (type < 0) {
            switch (~type) {
            case 0: return m_r.boolean();
            case 1: return int(m_r.byte());
            case 2: return m_r.sint();
            case 3: return m_r.uint();
            case 4: return m_r.real();
            case 5: return m_r.string();
            case 6: return qlonglong(m_r.sint64());
            case 7: return qulonglong(m_r.uint64());
            default: m_r.fail(); return {};
            }
        }
        if (type >= int(m_s.defs.size())) {
            m_r.fail();
            return {};
        }
        const Schema::Def& d = m_s.defs[type];
        switch (d.kind) {
        case 0: { // enum
            const quint32 v = m_r.uint();
            for (const Schema::Field& f : d.fields)
                if (f.value == v)
                    return f.name;
            return QString::number(v);
        }
        case 1: { // struct: every field, in order
            QVariantMap m;
            for (const Schema::Field& f : d.fields) {
                m.insert(f.name, value(f.type, f.array));
                if (!m_r.ok())
                    break;
            }
            return m;
        }
        case 2: return message(d);
        default: m_r.fail(); return {};
        }
    }

    QVariantMap message(const Schema::Def& d)
    {
        QVariantMap m;
        while (m_r.ok()) {
            const quint32 id = m_r.uint();
            if (id == 0)
                break;
            const Schema::Field* field = nullptr;
            for (const Schema::Field& f : d.fields)
                if (f.value == id)
                    field = &f;
            if (!field) { // unknown field: no way to skip it
                m_r.fail();
                break;
            }
            m.insert(field->name, value(field->type, field->array));
        }
        return m;
    }

private:
    const Schema& m_s;
    Reader& m_r;
};

QByteArray decompress(const QByteArray& chunk)
{
    if (chunk.startsWith("\x28\xb5\x2f\xfd"))
        return zip::zstd(chunk);
    return zip::inflateRaw(chunk);
}

quint32 le32(const QByteArray& b, qsizetype at)
{
    if (at + 4 > b.size())
        return 0;
    const auto* p = reinterpret_cast<const unsigned char*>(b.constData() + at);
    return quint32(p[0]) | quint32(p[1]) << 8 | quint32(p[2]) << 16 | quint32(p[3]) << 24;
}

} // namespace

bool parseSchema(const QByteArray& data, Schema* out)
{
    Reader r(data);
    const quint32 count = r.uint();
    for (quint32 i = 0; i < count && r.ok(); ++i) {
        Schema::Def d;
        d.name = r.string();
        d.kind = r.byte();
        const quint32 n = r.uint();
        for (quint32 k = 0; k < n && r.ok(); ++k) {
            Schema::Field f;
            f.name = r.string();
            f.type = r.sint();
            f.array = r.boolean();
            f.value = r.uint();
            d.fields.push_back(f);
        }
        out->defs.push_back(d);
    }
    return r.ok();
}

QVariantMap decode(const Schema& schema, const QByteArray& data, const QString& root, QString* error)
{
    for (const Schema::Def& d : schema.defs)
        if (d.name == root) {
            Reader r(data);
            Decoder dec(schema, r);
            QVariantMap m = dec.message(d);
            if (!r.ok() && error)
                *error = QStringLiteral("Malformed .fig data");
            return r.ok() ? m : QVariantMap();
        }
    if (error)
        *error = QStringLiteral("No %1 in the .fig schema").arg(root);
    return {};
}

bool unpack(const QByteArray& file, QByteArray* schema, QByteArray* message, QHash<QString, QByteArray>* images,
    QString* error)
{
    QByteArray canvas = file;
    if (file.startsWith("PK")) {
        const auto entries = zip::read(file);
        canvas = entries.value(QStringLiteral("canvas.fig"));
        if (images)
            for (auto it = entries.begin(); it != entries.end(); ++it)
                if (it.key().startsWith(QLatin1String("images/")))
                    images->insert(it.key().mid(7), it.value());
    }
    if (!canvas.startsWith("fig-kiwi") && !canvas.startsWith("fig-jam.")) {
        if (error)
            *error = QStringLiteral("Not a Figma file");
        return false;
    }
    qsizetype at = 12; // magic + version
    QByteArray chunks[2];
    for (QByteArray& c : chunks) {
        const quint32 n = le32(canvas, at);
        at += 4;
        if (at + n > canvas.size()) {
            if (error)
                *error = QStringLiteral("Truncated .fig file");
            return false;
        }
        c = decompress(canvas.mid(at, n));
        at += n;
    }
    if (chunks[0].isEmpty() || chunks[1].isEmpty()) {
        if (error)
            *error = QStringLiteral("Cannot decompress .fig data");
        return false;
    }
    *schema = chunks[0];
    *message = chunks[1];
    return true;
}

// ================================================================ conversion

namespace {

using M = QVariantMap;
using L = QVariantList;

constexpr quint32 kInvalid = 0xFFFFFFFFu;

QString guidKey(const QVariant& v)
{
    const M g = v.toMap();
    if (g.contains(QStringLiteral("guid")))
        return guidKey(g.value(QStringLiteral("guid")));
    if (!g.contains(QStringLiteral("sessionID")))
        return {};
    const quint32 s = g.value(QStringLiteral("sessionID")).toUInt(), l = g.value(QStringLiteral("localID")).toUInt();
    if (s == kInvalid || l == kInvalid)
        return {};
    return QString::number(s) + QLatin1Char(':') + QString::number(l);
}

float num(const M& m, const char* k, float def = 0)
{
    const QVariant v = m.value(QLatin1String(k));
    if (!v.isValid())
        return def;
    const float f = v.toFloat();
    return std::isfinite(f) ? f : def;
}
QString str(const M& m, const char* k, const QString& def = {})
{
    const QVariant v = m.value(QLatin1String(k));
    return v.isValid() ? v.toString() : def;
}
bool flag(const M& m, const char* k, bool def = false)
{
    const QVariant v = m.value(QLatin1String(k));
    return v.isValid() ? v.toBool() : def;
}
M map(const M& m, const char* k) { return m.value(QLatin1String(k)).toMap(); }
L list(const M& m, const char* k) { return m.value(QLatin1String(k)).toList(); }

SkColor4f color(const M& c, float opacity = 1)
{
    return {num(c, "r"), num(c, "g"), num(c, "b"), num(c, "a", 1) * opacity};
}

SkMatrix matrix(const M& m)
{
    if (m.isEmpty())
        return SkMatrix::I();
    return SkMatrix::MakeAll(num(m, "m00", 1), num(m, "m01"), num(m, "m02"), num(m, "m10"), num(m, "m11", 1),
        num(m, "m12"), 0, 0, 1);
}

BlendMode blend(const QString& s, BlendMode def)
{
    static const QHash<QString, BlendMode> m = {{QStringLiteral("PASS_THROUGH"), BlendMode::PassThrough},
        {QStringLiteral("NORMAL"), BlendMode::Normal}, {QStringLiteral("DARKEN"), BlendMode::Darken},
        {QStringLiteral("MULTIPLY"), BlendMode::Multiply}, {QStringLiteral("LINEAR_BURN"), BlendMode::ColorBurn},
        {QStringLiteral("COLOR_BURN"), BlendMode::ColorBurn}, {QStringLiteral("LIGHTEN"), BlendMode::Lighten},
        {QStringLiteral("SCREEN"), BlendMode::Screen}, {QStringLiteral("LINEAR_DODGE"), BlendMode::ColorDodge},
        {QStringLiteral("COLOR_DODGE"), BlendMode::ColorDodge}, {QStringLiteral("OVERLAY"), BlendMode::Overlay},
        {QStringLiteral("SOFT_LIGHT"), BlendMode::SoftLight}, {QStringLiteral("HARD_LIGHT"), BlendMode::HardLight},
        {QStringLiteral("DIFFERENCE"), BlendMode::Difference}, {QStringLiteral("EXCLUSION"), BlendMode::Exclusion},
        {QStringLiteral("HUE"), BlendMode::Hue}, {QStringLiteral("SATURATION"), BlendMode::Saturation},
        {QStringLiteral("COLOR"), BlendMode::Color}, {QStringLiteral("LUMINOSITY"), BlendMode::Luminosity}};
    return m.value(s, def);
}

int weightFromStyle(const QString& style)
{
    const QString s = style.toLower().remove(QLatin1Char(' ')).remove(QLatin1Char('-'));
    static const std::pair<const char*, int> table[] = {{"thin", 100}, {"hairline", 100}, {"extralight", 200},
        {"ultralight", 200}, {"light", 300}, {"medium", 500}, {"semibold", 600}, {"demibold", 600},
        {"extrabold", 800}, {"ultrabold", 800}, {"bold", 700}, {"black", 900}, {"heavy", 900}};
    for (const auto& [name, w] : table)
        if (s.contains(QLatin1String(name)))
            return w;
    return 400;
}

class Converter {
public:
    Converter(Document& doc, const M& msg, const QHash<QString, QByteArray>& images, Result* result)
        : m_doc(doc)
        , m_msg(msg)
        , m_zipImages(images)
        , m_result(result)
    {
    }

    bool run(QString* error)
    {
        // Index nodes and children (sorted by fractional position).
        QString root;
        for (const QVariant& v : list(m_msg, "nodeChanges")) {
            const M n = v.toMap();
            const QString key = guidKey(n.value(QStringLiteral("guid")));
            if (key.isEmpty())
                continue;
            m_nodes.insert(key, n);
            const M parent = map(n, "parentIndex");
            const QString pk = guidKey(parent.value(QStringLiteral("guid")));
            if (!pk.isEmpty())
                m_children[pk].push_back({str(parent, "position"), key});
            if (str(n, "type") == QLatin1String("DOCUMENT"))
                root = key;
            const QString ok = guidKey(n.value(QStringLiteral("overrideKey")));
            if (!ok.isEmpty())
                m_overrideKeys.insert(ok, key);
        }
        for (auto& kids : m_children)
            std::sort(kids.begin(), kids.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        if (root.isEmpty()) {
            if (error)
                *error = QStringLiteral("No document in the .fig file");
            return false;
        }
        importStyles();
        for (const auto& [pos, key] : m_children.value(root)) {
            const M canvas = m_nodes.value(key);
            if (str(canvas, "type") != QLatin1String("CANVAS") || flag(canvas, "internalOnly"))
                continue;
            Props pp;
            pp.type = NodeType::Page;
            pp.name = str(canvas, "name", QStringLiteral("Page"));
            if (canvas.contains(QStringLiteral("backgroundColor")))
                pp.pageBackground = color(map(canvas, "backgroundColor"));
            const NodeId page = m_doc.create(pp, m_doc.root());
            m_result->pages.push_back(page);
            for (const auto& [p2, child] : m_children.value(key))
                create(child, page, M());
        }
        // Instances point at their mains once everything exists, then sync builds their content.
        for (const auto& [inst, mainKey] : m_pendingInstances) {
            const NodeId main = local(mainKey);
            if (main && m_doc.at(main).type() == NodeType::Component)
                m_doc.edit(inst).componentId = main;
            else
                warn(QStringLiteral("Instance without its main component: ") + m_doc.at(inst).p.name);
        }
        resolveComponentLinks();
        m_doc.updateDerived();
        applyInstances();
        return true;
    }

private:
    void warn(const QString& w)
    {
        if (!m_result->warnings.contains(w) && m_result->warnings.size() < 50)
            m_result->warnings << w;
    }

    NodeId local(const QString& key) const
    {
        if (const NodeId id = m_local.value(key, kNoNode))
            return id;
        return m_local.value(m_overrideKeys.value(key), kNoNode);
    }

    QString image(const M& img)
    {
        const QByteArray hash = img.value(QStringLiteral("hash")).toByteArray();
        const QString hex = QString::fromLatin1(hash.toHex());
        if (m_imageHashes.contains(hex))
            return m_imageHashes.value(hex);
        QByteArray bytes = m_zipImages.value(hex);
        if (bytes.isEmpty() && img.contains(QStringLiteral("dataBlob"))) {
            const L blobs = list(m_msg, "blobs");
            const int i = img.value(QStringLiteral("dataBlob")).toInt();
            if (i >= 0 && i < blobs.size())
                bytes = blobs[i].toMap().value(QStringLiteral("bytes")).toByteArray();
        }
        if (bytes.isEmpty()) {
            warn(QStringLiteral("Missing image ") + hex.left(8));
            return {};
        }
        const QString h = m_doc.addImage(bytes);
        m_imageHashes.insert(hex, h);
        return h;
    }

    std::vector<Paint> paints(const L& l, float w, float h)
    {
        std::vector<Paint> out;
        for (const QVariant& v : l) {
            const M f = v.toMap();
            const QString type = str(f, "type", QStringLiteral("SOLID"));
            Paint p;
            p.visible = flag(f, "visible", true);
            p.opacity = num(f, "opacity", 1);
            p.blend = blend(str(f, "blendMode"), BlendMode::Normal);
            if (type == QLatin1String("SOLID")) {
                p.color = color(map(f, "color"));
            } else if (type.startsWith(QLatin1String("GRADIENT"))) {
                p.type = type == QLatin1String("GRADIENT_LINEAR") ? PaintType::LinearGradient
                    : type == QLatin1String("GRADIENT_RADIAL")    ? PaintType::RadialGradient
                    : type == QLatin1String("GRADIENT_ANGULAR")   ? PaintType::AngularGradient
                                                                  : PaintType::DiamondGradient;
                p.stops.clear();
                for (const QVariant& sv : list(f, "stops")) {
                    const M s = sv.toMap();
                    p.stops.push_back({num(s, "position"), color(map(s, "color"))});
                }
                // The paint transform maps the layer's unit box into gradient space.
                SkMatrix inv;
                if (!matrix(map(f, "transform")).invert(&inv))
                    inv = SkMatrix::I();
                if (p.type == PaintType::LinearGradient) {
                    p.gradStart = inv.mapXY(0, 0.5f);
                    p.gradEnd = inv.mapXY(1, 0.5f);
                } else {
                    p.gradStart = inv.mapXY(0.5f, 0.5f);
                    p.gradEnd = inv.mapXY(1, 0.5f);
                    const SkPoint co = inv.mapXY(0.5f, 1);
                    auto d = [&](SkPoint a, SkPoint b) { return SkPoint::Length((a.x() - b.x()) * w, (a.y() - b.y()) * h); };
                    const float main = d(p.gradStart, p.gradEnd);
                    p.gradWidth = main > 0 ? d(p.gradStart, co) / main : 1;
                }
            } else if (type == QLatin1String("IMAGE")) {
                p.type = PaintType::Image;
                p.imageHash = image(map(f, "image"));
                const QString mode = str(f, "imageScaleMode", QStringLiteral("FILL"));
                p.scaleMode = mode == QLatin1String("FIT") ? ImageScaleMode::Fit
                    : mode == QLatin1String("TILE")         ? ImageScaleMode::Tile
                    : mode == QLatin1String("STRETCH")      ? ImageScaleMode::Crop
                                                            : ImageScaleMode::Fill;
                p.imageScale = num(f, "scale", 1);
                if (p.scaleMode == ImageScaleMode::Crop && f.contains(QStringLiteral("transform"))) {
                    SkRect r = matrix(map(f, "transform")).mapRect(SkRect::MakeWH(1, 1));
                    if (!r.isEmpty())
                        p.crop = r;
                }
                const M fx = map(f, "paintFilter");
                p.exposure = num(fx, "exposure");
                p.contrast = num(fx, "contrast");
                p.saturation = num(fx, "saturation");
                p.temperature = num(fx, "temperature");
                p.tint = num(fx, "tint");
                p.highlights = num(fx, "highlights");
                p.shadows = num(fx, "shadows");
            } else {
                continue;
            }
            out.push_back(p);
        }
        return out;
    }

    std::vector<Effect> effects(const L& l)
    {
        std::vector<Effect> out;
        for (const QVariant& v : l) {
            const M e = v.toMap();
            const QString type = str(e, "type");
            Effect x;
            if (type == QLatin1String("DROP_SHADOW"))
                x.type = EffectType::DropShadow;
            else if (type == QLatin1String("INNER_SHADOW"))
                x.type = EffectType::InnerShadow;
            else if (type == QLatin1String("FOREGROUND_BLUR"))
                x.type = EffectType::LayerBlur;
            else if (type == QLatin1String("BACKGROUND_BLUR"))
                x.type = EffectType::BackgroundBlur;
            else
                continue;
            x.visible = flag(e, "visible", true);
            x.color = color(map(e, "color"));
            const M off = map(e, "offset");
            x.offset = {num(off, "x"), num(off, "y")};
            x.radius = num(e, "radius");
            x.spread = num(e, "spread");
            x.blend = blend(str(e, "blendMode"), BlendMode::Normal);
            x.showBehindTransparent = flag(e, "showShadowBehindNode");
            out.push_back(x);
        }
        return out;
    }

    std::vector<LayoutGrid> grids(const L& l)
    {
        std::vector<LayoutGrid> out;
        for (const QVariant& v : l) {
            const M g = v.toMap();
            LayoutGrid x;
            const bool grid = str(g, "pattern") == QLatin1String("GRID");
            x.kind = grid ? LayoutGrid::Kind::Grid : str(g, "axis") == QLatin1String("Y") ? LayoutGrid::Kind::Rows : LayoutGrid::Kind::Columns;
            x.size = num(g, "sectionSize", 10);
            x.count = std::max(1, int(num(g, "numSections", 12)));
            x.gutter = num(g, "gutterSize", 20);
            x.margin = num(g, "offset");
            x.color = color(map(g, "color"));
            x.visible = flag(g, "visible", true);
            out.push_back(x);
        }
        return out;
    }

    VectorData network(const M& n, float w, float h)
    {
        VectorData v;
        const M vd = map(n, "vectorData");
        const L blobs = list(m_msg, "blobs");
        if (vd.contains(QStringLiteral("vectorNetworkBlob"))) {
            const int bi = vd.value(QStringLiteral("vectorNetworkBlob")).toInt();
            const QByteArray b = bi >= 0 && bi < blobs.size() ? blobs[bi].toMap().value(QStringLiteral("bytes")).toByteArray() : QByteArray();
            const M ns = map(vd, "normalizedSize");
            const float sx = num(ns, "x") > 0 ? w / num(ns, "x") : 1, sy = num(ns, "y") > 0 ? h / num(ns, "y") : 1;
            qsizetype at = 0;
            auto u = [&]() { const quint32 x = le32(b, at); at += 4; return x; };
            auto f = [&]() { const quint32 x = le32(b, at); at += 4; float r; std::memcpy(&r, &x, 4); return std::isfinite(r) ? r : 0.f; };
            if (b.size() >= 12) {
                const quint32 nv = u(), ns2 = u(), nr = u();
                for (quint32 i = 0; i < nv && at + 12 <= b.size(); ++i) {
                    u(); // style
                    const float x = f(), y = f();
                    v.vertices.push_back({{x * sx, y * sy}, AnchorMode::Asymmetric});
                }
                for (quint32 i = 0; i < ns2 && at + 28 <= b.size(); ++i) {
                    u();
                    const quint32 a = u();
                    const float t1x = f(), t1y = f();
                    const quint32 z = u();
                    const float t2x = f(), t2y = f();
                    if (a >= v.vertices.size() || z >= v.vertices.size())
                        continue;
                    const SkPoint pa = v.vertices[a].p, pz = v.vertices[z].p;
                    v.segments.push_back({int(a), int(z), {pa.x() + t1x * sx, pa.y() + t1y * sy}, {pz.x() + t2x * sx, pz.y() + t2y * sy}});
                }
                for (quint32 i = 0; i < nr && at + 8 <= b.size(); ++i) {
                    const quint32 flags = u(), loops = u();
                    if (i == 0)
                        v.fill = (flags & 1) ? SkPathFillType::kWinding : SkPathFillType::kEvenOdd;
                    NetRegion r;
                    for (quint32 k = 0; k < loops && at + 4 <= b.size(); ++k) {
                        const quint32 count = u();
                        std::vector<int> loop;
                        for (quint32 j = 0; j < count && at + 4 <= b.size(); ++j) {
                            const quint32 s = u();
                            if (s < v.segments.size())
                                loop.push_back(int(s));
                        }
                        if (!loop.empty())
                            r.loops.push_back(loop);
                    }
                    if (!r.loops.empty())
                        v.regions.push_back(r);
                }
            }
            if (!v.segments.empty())
                return v;
            v = VectorData();
        }
        // Fallback: the precomputed fill geometry.
        SkPathBuilder pb;
        for (const QVariant& gv : list(n, "fillGeometry")) {
            const M g = gv.toMap();
            const int bi = g.value(QStringLiteral("commandsBlob")).toInt();
            const QByteArray b = bi >= 0 && bi < blobs.size() ? blobs[bi].toMap().value(QStringLiteral("bytes")).toByteArray() : QByteArray();
            qsizetype at = 0;
            auto f = [&]() { const quint32 x = le32(b, at); at += 4; float r; std::memcpy(&r, &x, 4); return r; };
            while (at < b.size()) {
                const char cmd = b[at++];
                if (cmd == 0)
                    pb.close();
                else if (cmd == 1) { const float x = f(), y = f(); pb.moveTo(x, y); }
                else if (cmd == 2) { const float x = f(), y = f(); pb.lineTo(x, y); }
                else if (cmd == 3) { const float a = f(), c = f(), x = f(), y = f(); pb.quadTo(a, c, x, y); }
                else if (cmd == 4) { const float a = f(), c = f(), d = f(), e = f(), x = f(), y = f(); pb.cubicTo(a, c, d, e, x, y); }
                else break;
            }
        }
        return vectorFromPath(pb.detach());
    }

    void text(const M& n, Props& p)
    {
        const M td = map(n, "textData");
        p.text = str(td, "characters");
        const M font = map(n, "fontName");
        TextStyle& t = p.textStyle;
        t.family = str(font, "family", QStringLiteral("Inter"));
        const QString style = str(font, "style", QStringLiteral("Regular"));
        t.weight = weightFromStyle(style);
        t.italic = style.contains(QLatin1String("Italic"), Qt::CaseInsensitive);
        t.size = num(n, "fontSize", 12);
        const M lh = map(n, "lineHeight");
        const QString lhUnits = str(lh, "units");
        if (lhUnits == QLatin1String("PIXELS")) {
            t.lineHeight = num(lh, "value");
        } else if (lhUnits == QLatin1String("PERCENT") && num(lh, "value", 100) != 100) {
            t.lineHeight = num(lh, "value");
            t.lineHeightPercent = true;
        } else if (lhUnits == QLatin1String("RAW") && num(lh, "value", 1) != 1) {
            t.lineHeight = num(lh, "value") * 100;
            t.lineHeightPercent = true;
        }
        const M ls = map(n, "letterSpacing");
        t.letterSpacing = str(ls, "units") == QLatin1String("PIXELS") ? (t.size > 0 ? num(ls, "value") / t.size * 100 : 0)
                                                                      : num(ls, "value");
        t.paragraphSpacing = num(n, "paragraphSpacing");
        const QString ah = str(n, "textAlignHorizontal", QStringLiteral("LEFT"));
        t.alignH = ah == QLatin1String("CENTER") ? TextAlignH::Center : ah == QLatin1String("RIGHT") ? TextAlignH::Right
            : ah == QLatin1String("JUSTIFIED")                          ? TextAlignH::Justified
                                                                        : TextAlignH::Left;
        const QString av = str(n, "textAlignVertical", QStringLiteral("TOP"));
        t.alignV = av == QLatin1String("CENTER") ? TextAlignV::Center : av == QLatin1String("BOTTOM") ? TextAlignV::Bottom : TextAlignV::Top;
        const QString ar = str(n, "textAutoResize", QStringLiteral("NONE"));
        t.autoResize = ar == QLatin1String("WIDTH_AND_HEIGHT") ? TextAutoResize::WidthAndHeight
            : ar == QLatin1String("HEIGHT")                     ? TextAutoResize::Height
                                                                : TextAutoResize::None;
        if (str(n, "textTruncation") == QLatin1String("ENDING"))
            t.autoResize = TextAutoResize::Truncate;
        t.maxLines = int(num(n, "maxLines"));
        const QString tc = str(n, "textCase", QStringLiteral("ORIGINAL"));
        t.textCase = tc == QLatin1String("UPPER") ? TextCase::Upper : tc == QLatin1String("LOWER") ? TextCase::Lower
            : tc == QLatin1String("TITLE")                          ? TextCase::Title
                                                                    : TextCase::Original;
        if (tc == QLatin1String("SMALL_CAPS") || tc == QLatin1String("SMALL_CAPS_FORCED"))
            t.features.insert(QStringLiteral("smcp"), 1);
        const QString deco = str(n, "textDecoration", QStringLiteral("NONE"));
        t.decoration = deco == QLatin1String("UNDERLINE") ? TextDecoration::Underline
            : deco == QLatin1String("STRIKETHROUGH")       ? TextDecoration::Strikethrough
                                                           : TextDecoration::None;
        for (const QVariant& f : list(n, "toggledOnOTFeatures"))
            t.features.insert(f.toString().toLower().left(4), 1);
        for (const QVariant& f : list(n, "toggledOffOTFeatures"))
            t.features.insert(f.toString().toLower().left(4), 0);
        p.layoutChild.sizingH = t.autoResize == TextAutoResize::WidthAndHeight ? LayoutSizing::Hug : LayoutSizing::Fixed;
        p.layoutChild.sizingV = t.autoResize == TextAutoResize::WidthAndHeight || t.autoResize == TextAutoResize::Height
            ? LayoutSizing::Hug
            : LayoutSizing::Fixed;

        // Character styles: per-character ids into an override table.
        QHash<quint32, M> table;
        for (const QVariant& sv : list(td, "styleOverrideTable")) {
            const M s = sv.toMap();
            table.insert(s.value(QStringLiteral("styleID")).toUInt(), s);
        }
        const L ids = list(td, "characterStyleIDs");
        int i = 0;
        while (i < ids.size() && i < p.text.size()) {
            const quint32 id = ids[i].toUInt();
            int j = i + 1;
            while (j < ids.size() && j < p.text.size() && ids[j].toUInt() == id)
                ++j;
            if (id && table.contains(id)) {
                const M s = table.value(id);
                TextRun r;
                std::uint32_t mask = 0;
                if (s.contains(QStringLiteral("fontName"))) {
                    const M fn = map(s, "fontName");
                    if (str(fn, "family") != t.family) {
                        r.family = str(fn, "family");
                        mask |= RunFamily;
                    }
                    r.weight = weightFromStyle(str(fn, "style"));
                    r.italic = str(fn, "style").contains(QLatin1String("Italic"), Qt::CaseInsensitive);
                    if (r.weight != t.weight)
                        mask |= RunWeight;
                    if (r.italic != t.italic)
                        mask |= RunItalic;
                }
                if (s.contains(QStringLiteral("fontSize"))) {
                    r.size = num(s, "fontSize", t.size);
                    mask |= RunSize;
                }
                if (s.contains(QStringLiteral("textDecoration"))) {
                    const QString d = str(s, "textDecoration");
                    r.decoration = d == QLatin1String("UNDERLINE") ? TextDecoration::Underline
                        : d == QLatin1String("STRIKETHROUGH")       ? TextDecoration::Strikethrough
                                                                    : TextDecoration::None;
                    mask |= RunDecoration;
                }
                const auto fills = paints(list(s, "fillPaints"), p.width, p.height);
                if (!fills.empty() && fills.front().type == PaintType::Solid) {
                    r.fill = fills.front().color;
                    r.fill.fA *= fills.front().opacity;
                    mask |= RunFill;
                }
                const QString url = str(map(s, "hyperlink"), "url");
                if (!url.isEmpty()) {
                    r.link = url;
                    mask |= RunLink;
                }
                if (mask)
                    text::styleRange(p, i, j, mask, r);
            }
            i = j;
        }
        // Lists
        const L lines = list(td, "lines");
        for (int k = 0; k < lines.size(); ++k) {
            const M line = lines[k].toMap();
            const QString lt = str(line, "lineType", QStringLiteral("PLAIN"));
            if (lt == QLatin1String("PLAIN"))
                continue;
            ParagraphAttr a;
            a.list = lt == QLatin1String("ORDERED_LIST") ? ListType::Ordered : ListType::Bullet;
            a.indent = std::max(0, int(num(line, "indentationLevel", 1)) - 1);
            text::setParagraphs(p, k, k, a);
        }
    }

    void importStyles()
    {
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
            const M n = it.value();
            const QString type = str(n, "styleType");
            if (type.isEmpty() || type == QLatin1String("NONE"))
                continue;
            Style st;
            st.id = Library::newId();
            st.name = str(n, "name", QStringLiteral("Style"));
            st.description = str(n, "description");
            const M size = map(n, "size");
            if (type == QLatin1String("FILL")) {
                st.type = StyleType::Fill;
                st.paints = paints(list(n, "fillPaints"), num(size, "x", 1), num(size, "y", 1));
            } else if (type == QLatin1String("TEXT")) {
                st.type = StyleType::Text;
                Props tp;
                tp.type = NodeType::Text;
                text(n, tp);
                applyTypography(st.text, tp.textStyle);
            } else if (type == QLatin1String("EFFECT")) {
                st.type = StyleType::Effect;
                st.effects = effects(list(n, "effects"));
            } else if (type == QLatin1String("GRID")) {
                st.type = StyleType::Grid;
                st.grids = grids(list(n, "layoutGrids"));
            } else {
                continue;
            }
            m_doc.editLibrary().styles.push_back(st);
            m_styles.insert(it.key(), st.id);
            if (n.contains(QStringLiteral("key")))
                m_styles.insert(str(n, "key"), st.id);
        }
    }

    QString styleRef(const M& n, const char* oldKey, const char* newKey)
    {
        // Older files: inheritFillStyleID (a GUID); newer: styleIdForFill {guid, assetRef {key}}.
        QString id = m_styles.value(guidKey(n.value(QLatin1String(oldKey))));
        if (id.isEmpty()) {
            const M ref = map(n, newKey);
            id = m_styles.value(guidKey(ref.value(QStringLiteral("guid"))));
            if (id.isEmpty())
                id = m_styles.value(str(map(ref, "assetRef"), "key"));
        }
        return id;
    }

    NodeType typeFor(const M& n)
    {
        const QString t = str(n, "type");
        if (t == QLatin1String("FRAME")) {
            if (flag(n, "isStateGroup"))
                return NodeType::ComponentSet;
            if (flag(n, "resizeToFit") && str(n, "stackMode", QStringLiteral("NONE")) == QLatin1String("NONE"))
                return NodeType::Group; // Figma stores groups as frames that fit their content
            return NodeType::Frame;
        }
        static const QHash<QString, NodeType> m = {{QStringLiteral("GROUP"), NodeType::Group},
            {QStringLiteral("SECTION"), NodeType::Section}, {QStringLiteral("SYMBOL"), NodeType::Component},
            {QStringLiteral("INSTANCE"), NodeType::Instance}, {QStringLiteral("RECTANGLE"), NodeType::Rectangle},
            {QStringLiteral("ROUNDED_RECTANGLE"), NodeType::Rectangle}, {QStringLiteral("ELLIPSE"), NodeType::Ellipse},
            {QStringLiteral("REGULAR_POLYGON"), NodeType::Polygon}, {QStringLiteral("STAR"), NodeType::Star},
            {QStringLiteral("LINE"), NodeType::Line}, {QStringLiteral("VECTOR"), NodeType::Vector},
            {QStringLiteral("BOOLEAN_OPERATION"), NodeType::BooleanOp}, {QStringLiteral("TEXT"), NodeType::Text}};
        return m.value(t, NodeType::Document); // Document = unsupported
    }

    void convert(const M& n, const M& parent, Props& p)
    {
        p.name = str(n, "name");
        p.visible = flag(n, "visible", true);
        p.locked = flag(n, "locked");
        p.opacity = num(n, "opacity", 1);
        p.blend = blend(str(n, "blendMode"), BlendMode::PassThrough);
        p.isMask = flag(n, "mask");
        p.transform = matrix(map(n, "transform"));
        const M size = map(n, "size");
        p.width = std::max(0.01f, num(size, "x", 1));
        p.height = p.type == NodeType::Line ? 0 : std::max(0.01f, num(size, "y", 1));
        auto cons = [](const QString& s) {
            return s == QLatin1String("MAX") ? 1 : s == QLatin1String("STRETCH") ? 2 : s == QLatin1String("CENTER") ? 3
                : s == QLatin1String("SCALE")                                    ? 4
                                                                                 : 0;
        };
        p.constraintH = ConstraintH(cons(str(n, "horizontalConstraint")));
        p.constraintV = ConstraintV(cons(str(n, "verticalConstraint")));

        p.fills = paints(list(n, "fillPaints"), p.width, p.height);
        p.strokes = paints(list(n, "strokePaints"), p.width, p.height);
        StrokeStyle& s = p.stroke;
        s.weight = num(n, "strokeWeight", 1);
        const QString align = str(n, "strokeAlign", QStringLiteral("CENTER"));
        s.align = align == QLatin1String("INSIDE") ? StrokeAlign::Inside : align == QLatin1String("OUTSIDE") ? StrokeAlign::Outside : StrokeAlign::Center;
        const QString join = str(n, "strokeJoin", QStringLiteral("MITER"));
        s.join = join == QLatin1String("ROUND") ? StrokeJoin::Round : join == QLatin1String("BEVEL") ? StrokeJoin::Bevel : StrokeJoin::Miter;
        const QString cap = str(n, "strokeCap", QStringLiteral("NONE"));
        s.startCap = s.endCap = cap == QLatin1String("ROUND") ? StrokeCap::Round : cap == QLatin1String("SQUARE") ? StrokeCap::Square
            : cap == QLatin1String("ARROW_LINES")                                                                ? StrokeCap::LineArrow
            : cap == QLatin1String("ARROW_EQUILATERAL")                                                          ? StrokeCap::TriangleArrow
                                                                                                                 : StrokeCap::None;
        s.miterLimit = num(n, "miterLimit", 4);
        for (const QVariant& d : list(n, "dashPattern"))
            s.dashes.push_back(d.toFloat());
        p.effects = effects(list(n, "effects"));

        const float r = num(n, "cornerRadius");
        p.corners = {r, r, r, r};
        if (flag(n, "rectangleCornerRadiiIndependent") || n.contains(QStringLiteral("rectangleTopLeftCornerRadius")))
            p.corners = {num(n, "rectangleTopLeftCornerRadius", r), num(n, "rectangleTopRightCornerRadius", r),
                num(n, "rectangleBottomRightCornerRadius", r), num(n, "rectangleBottomLeftCornerRadius", r)};
        p.cornerSmoothing = num(n, "cornerSmoothing");

        if (isFrameLike(p.type)) {
            p.clipsContent = !flag(n, "frameMaskDisabled");
            const QString mode = str(n, "stackMode", QStringLiteral("NONE"));
            AutoLayout& l = p.layout;
            l.mode = mode == QLatin1String("HORIZONTAL") ? LayoutMode::Horizontal : mode == QLatin1String("VERTICAL") ? LayoutMode::Vertical : LayoutMode::None;
            if (l.mode != LayoutMode::None) {
                l.spacing = num(n, "stackSpacing");
                l.padL = num(n, "stackHorizontalPadding", num(n, "stackPadding"));
                l.padT = num(n, "stackVerticalPadding", num(n, "stackPadding"));
                l.padR = num(n, "stackPaddingRight", l.padL);
                l.padB = num(n, "stackPaddingBottom", l.padT);
                const QString pa = str(n, "stackPrimaryAlignItems", QStringLiteral("MIN"));
                l.primaryAlign = pa == QLatin1String("CENTER") ? LayoutAlign::Center : pa == QLatin1String("MAX") ? LayoutAlign::Max
                    : pa.startsWith(QLatin1String("SPACE"))                                                    ? LayoutAlign::SpaceBetween
                                                                                                               : LayoutAlign::Min;
                const QString ca = str(n, "stackCounterAlignItems", QStringLiteral("MIN"));
                l.counterAlign = ca == QLatin1String("CENTER") ? LayoutAlign::Center : ca == QLatin1String("MAX") ? LayoutAlign::Max
                    : ca == QLatin1String("BASELINE")                                                          ? LayoutAlign::Baseline
                                                                                                               : LayoutAlign::Min;
                l.wrap = str(n, "stackWrap") == QLatin1String("WRAP");
                l.counterSpacing = num(n, "stackCounterSpacing");
                l.reverseZ = flag(n, "stackReverseZIndex");
                l.strokesIncluded = flag(n, "bordersTakeSpace");
                const bool hugP = str(n, "stackPrimarySizing", QStringLiteral("FIXED")) != QLatin1String("FIXED");
                const bool hugC = str(n, "stackCounterSizing", QStringLiteral("FIXED")) != QLatin1String("FIXED");
                const bool horiz = l.mode == LayoutMode::Horizontal;
                p.layoutChild.sizingH = (horiz ? hugP : hugC) ? LayoutSizing::Hug : LayoutSizing::Fixed;
                p.layoutChild.sizingV = (horiz ? hugC : hugP) ? LayoutSizing::Hug : LayoutSizing::Fixed;
            }
            p.grids = grids(list(n, "layoutGrids"));
        }
        // As a child of an auto layout frame.
        const QString pmode = str(parent, "stackMode", QStringLiteral("NONE"));
        if (pmode == QLatin1String("HORIZONTAL") || pmode == QLatin1String("VERTICAL")) {
            const bool horiz = pmode == QLatin1String("HORIZONTAL");
            const bool growP = num(n, "stackChildPrimaryGrow") > 0;
            const bool stretchC = str(n, "stackChildAlignSelf") == QLatin1String("STRETCH");
            if (growP)
                (horiz ? p.layoutChild.sizingH : p.layoutChild.sizingV) = LayoutSizing::Fill;
            if (stretchC)
                (horiz ? p.layoutChild.sizingV : p.layoutChild.sizingH) = LayoutSizing::Fill;
            p.layoutChild.absolute = str(n, "stackPositioning") == QLatin1String("ABSOLUTE");
        }
        const M mn = map(map(n, "minSize"), "value"), mx = map(map(n, "maxSize"), "value");
        p.layoutChild.minW = num(mn, "x");
        p.layoutChild.minH = num(mn, "y");
        p.layoutChild.maxW = std::isinf(num(mx, "x")) ? 0 : num(mx, "x");
        p.layoutChild.maxH = std::isinf(num(mx, "y")) ? 0 : num(mx, "y");

        for (const QVariant& ev : list(n, "exportSettings")) {
            const M e = ev.toMap();
            QString fmt = str(e, "imageType", QStringLiteral("PNG"));
            if (fmt == QLatin1String("JPEG"))
                fmt = QStringLiteral("JPG");
            const M c = map(e, "constraint");
            p.exports.push_back({fmt, str(c, "type") == QLatin1String("CONTENT_SCALE") ? num(c, "value", 1) : 1.f, str(e, "suffix")});
        }

        switch (p.type) {
        case NodeType::Ellipse: {
            const M arc = map(n, "arcData");
            if (!arc.isEmpty()) {
                const float a0 = num(arc, "startingAngle"), a1 = num(arc, "endingAngle", 6.2831853f);
                p.arcStart = a0 * 180 / 3.14159265f;
                p.arcSweep = (a1 - a0) * 180 / 3.14159265f;
                p.arcInner = num(arc, "innerRadius");
            }
            break;
        }
        case NodeType::Polygon:
        case NodeType::Star:
            p.pointCount = std::max(3, int(num(n, "count", p.type == NodeType::Star ? 5 : 3)));
            p.starInner = num(n, "starInnerScale", 0.382f);
            break;
        case NodeType::BooleanOp: {
            const QString op = str(n, "booleanOperation", QStringLiteral("UNION"));
            p.boolOp = op == QLatin1String("SUBTRACT") ? BooleanOpType::Subtract : op == QLatin1String("INTERSECT") ? BooleanOpType::Intersect
                : op == QLatin1String("XOR")                                                                     ? BooleanOpType::Exclude
                                                                                                                 : BooleanOpType::Union;
            break;
        }
        case NodeType::Vector: p.vector = network(n, p.width, p.height); break;
        case NodeType::Text: text(n, p); break;
        default: break;
        }

        p.fillStyle = styleRef(n, "inheritFillStyleID", "styleIdForFill");
        p.strokeStyle = styleRef(n, "inheritFillStyleIDForStroke", "styleIdForStrokeFill");
        if (p.type == NodeType::Text)
            p.textStyleId = styleRef(n, "inheritTextStyleID", "styleIdForText");
        p.effectStyle = styleRef(n, "inheritEffectStyleID", "styleIdForEffect");
        if (isFrameLike(p.type))
            p.gridStyle = styleRef(n, "inheritGridStyleID", "styleIdForGrid");

        // Components: property definitions, variants, links from layers.
        for (const QVariant& dv : list(n, "componentPropDefs")) {
            const M d = dv.toMap();
            const QString type = str(d, "type");
            if (type == QLatin1String("VARIANT"))
                continue;
            ComponentProp c;
            c.id = guidKey(d.value(QStringLiteral("id")));
            c.name = str(d, "name");
            const M init = map(d, "initialValue");
            if (type == QLatin1String("BOOL")) {
                c.type = ComponentPropType::Boolean;
                c.defaultValue = flag(init, "boolValue", true) ? QStringLiteral("true") : QStringLiteral("false");
            } else if (type == QLatin1String("TEXT")) {
                c.type = ComponentPropType::Text;
                c.defaultValue = str(map(init, "textValue"), "characters");
            } else if (type == QLatin1String("INSTANCE_SWAP")) {
                c.type = ComponentPropType::InstanceSwap;
                c.defaultValue = guidKey(init.value(QStringLiteral("guidValue"))); // resolved later
            } else {
                continue;
            }
            p.componentProps.push_back(c);
        }
        for (const QVariant& rv : list(n, "componentPropRefs")) {
            const M r = rv.toMap();
            if (flag(r, "isDeleted"))
                continue;
            const QString field = str(r, "componentPropNodeField");
            const QString key = field == QLatin1String("VISIBLE") ? QStringLiteral("visible")
                : field == QLatin1String("TEXT_DATA")               ? QStringLiteral("characters")
                : field == QLatin1String("OVERRIDDEN_SYMBOL_ID")    ? QStringLiteral("mainComponent")
                                                                    : QString();
            if (!key.isEmpty())
                p.propRefs.insert(key, guidKey(r.value(QStringLiteral("defID"))));
        }
        if (p.type == NodeType::Component && str(parent, "type") == QLatin1String("FRAME") && flag(parent, "isStateGroup"))
            for (const QString& part : p.name.split(QLatin1Char(','))) {
                const int eq = int(part.indexOf(QLatin1Char('=')));
                if (eq > 0)
                    p.variantProps.insert(part.left(eq).trimmed(), part.mid(eq + 1).trimmed());
            }
    }

    void create(const QString& key, NodeId parent, const M& parentNode)
    {
        const M n = m_nodes.value(key);
        Props p;
        p.type = typeFor(n);
        if (p.type == NodeType::Document) {
            warn(QStringLiteral("Skipped unsupported layer type ") + str(n, "type"));
            return;
        }
        convert(n, parentNode, p);
        if (p.type == NodeType::Instance) {
            for (const QVariant& av : list(n, "componentPropAssignments")) {
                const M a = av.toMap();
                const M v = map(a, "value");
                QString value;
                if (v.contains(QStringLiteral("boolValue")))
                    value = flag(v, "boolValue") ? QStringLiteral("true") : QStringLiteral("false");
                else if (v.contains(QStringLiteral("textValue")))
                    value = str(map(v, "textValue"), "characters");
                else if (v.contains(QStringLiteral("guidValue")))
                    value = QStringLiteral("guid:") + guidKey(v.value(QStringLiteral("guidValue")));
                p.propValues.insert(guidKey(a.value(QStringLiteral("defID"))), value);
            }
        }
        const NodeId id = m_doc.create(p, parent);
        m_local.insert(key, id);
        m_result->layers++;
        if (p.type == NodeType::Instance) {
            m_pendingInstances.push_back({id, guidKey(map(n, "symbolData").value(QStringLiteral("symbolID")))});
            m_instances.push_back({id, key});
            return; // instance content comes from the main component
        }
        for (const auto& [pos, child] : m_children.value(key))
            create(child, id, n);
    }

    // Instance-swap defaults and values refer to components by guid; map them to local ids.
    void resolveComponentLinks()
    {
        for (auto it = m_local.begin(); it != m_local.end(); ++it) {
            const Props& cur = m_doc.at(it.value()).p;
            bool touch = false;
            Props p = cur;
            for (ComponentProp& c : p.componentProps)
                if (c.type == ComponentPropType::InstanceSwap) {
                    const NodeId comp = local(c.defaultValue);
                    c.defaultValue = comp ? QString::number(comp) : QString();
                    touch = true;
                }
            for (auto v = p.propValues.begin(); v != p.propValues.end(); ++v)
                if (v.value().startsWith(QLatin1String("guid:"))) {
                    const NodeId comp = local(v.value().mid(5));
                    v.value() = comp ? QString::number(comp) : QString();
                    touch = true;
                }
            if (touch)
                m_doc.edit(it.value()) = p;
        }
    }

    // The instance node under `scope` made from the main node `fig key`.
    NodeId findInInstance(NodeId scope, const QString& figKey)
    {
        const NodeId main = local(figKey);
        if (!main)
            return kNoNode;
        NodeId found = kNoNode;
        m_doc.walk(scope, [&](const Node& n) {
            if (found)
                return false;
            if (n.id != scope && n.p.sourceId == main)
                found = n.id;
            return !found;
        });
        return found;
    }

    void applyInstances()
    {
        for (const auto& [inst, key] : m_instances) {
            const M n = m_nodes.value(key);
            // The instance's own size (sync gave it the main's).
            const M size = map(n, "size");
            m_doc.resize(inst, std::max(0.01f, num(size, "x", 1)), std::max(0.01f, num(size, "y", 1)));
            m_doc.edit(inst).overrides |= OvSize; // new instances aren't diffed against their main
            for (const QVariant& ov : list(map(n, "symbolData"), "symbolOverrides")) {
                const M o = ov.toMap();
                NodeId target = inst;
                for (const QVariant& g : list(map(o, "guidPath"), "guids")) {
                    target = findInInstance(target, guidKey(g));
                    if (!target)
                        break;
                }
                if (!target || target == inst)
                    continue;
                applyOverride(target, o);
            }
            m_doc.updateDerived();
        }
    }

    void applyOverride(NodeId id, const M& o)
    {
        Props p = m_doc.at(id).p;
        if (o.contains(QStringLiteral("textData")) && p.type == NodeType::Text)
            text::splice(p, 0, int(p.text.size()), str(map(o, "textData"), "characters"));
        if (o.contains(QStringLiteral("fillPaints")))
            p.fills = paints(list(o, "fillPaints"), p.width, p.height);
        if (o.contains(QStringLiteral("strokePaints")))
            p.strokes = paints(list(o, "strokePaints"), p.width, p.height);
        if (o.contains(QStringLiteral("effects")))
            p.effects = effects(list(o, "effects"));
        if (o.contains(QStringLiteral("visible")))
            p.visible = flag(o, "visible", true);
        if (o.contains(QStringLiteral("opacity")))
            p.opacity = num(o, "opacity", 1);
        if (o.contains(QStringLiteral("fontSize")))
            p.textStyle.size = num(o, "fontSize", p.textStyle.size);
        if (o.contains(QStringLiteral("strokeWeight")))
            p.stroke.weight = num(o, "strokeWeight", p.stroke.weight);
        if (o.contains(QStringLiteral("cornerRadius"))) {
            const float r = num(o, "cornerRadius");
            p.corners = {r, r, r, r};
        }
        if (o.contains(QStringLiteral("overriddenSymbolID")) && p.type == NodeType::Instance) {
            const NodeId comp = local(guidKey(o.value(QStringLiteral("overriddenSymbolID"))));
            if (comp && m_doc.at(comp).type() == NodeType::Component)
                p.componentId = comp;
        }
        if (o.contains(QStringLiteral("componentPropAssignments")))
            for (const QVariant& av : list(o, "componentPropAssignments")) {
                const M a = av.toMap();
                const M v = map(a, "value");
                if (v.contains(QStringLiteral("boolValue")))
                    p.propValues.insert(guidKey(a.value(QStringLiteral("defID"))), flag(v, "boolValue") ? QStringLiteral("true") : QStringLiteral("false"));
                else if (v.contains(QStringLiteral("textValue")))
                    p.propValues.insert(guidKey(a.value(QStringLiteral("defID"))), str(map(v, "textValue"), "characters"));
            }
        if (!(p == m_doc.at(id).p))
            m_doc.edit(id) = p;
        if (p.componentId != m_doc.at(id).p.componentId)
            m_doc.updateDerived();
    }

    Document& m_doc;
    const M& m_msg;
    const QHash<QString, QByteArray>& m_zipImages;
    Result* m_result;
    QHash<QString, M> m_nodes;
    QHash<QString, std::vector<std::pair<QString, QString>>> m_children; // parent -> (position, child)
    QHash<QString, QString> m_overrideKeys; // override key -> node key
    QHash<QString, NodeId> m_local;
    QHash<QString, QString> m_styles;      // style node key/guid -> drav style id
    QHash<QString, QString> m_imageHashes; // figma hash (hex) -> drav image hash
    std::vector<std::pair<NodeId, QString>> m_pendingInstances; // instance -> main key
    std::vector<std::pair<NodeId, QString>> m_instances;        // instance -> its fig node key
};

} // namespace

bool import(Document& doc, const QByteArray& file, Result* result, QString* error)
{
    QByteArray schemaData, data;
    QHash<QString, QByteArray> images;
    if (!unpack(file, &schemaData, &data, &images, error))
        return false;
    Schema schema;
    if (!parseSchema(schemaData, &schema)) {
        if (error)
            *error = QStringLiteral("Cannot read the .fig schema");
        return false;
    }
    const QVariantMap msg = decode(schema, data, QStringLiteral("Message"), error);
    if (msg.isEmpty())
        return false;
    Result local;
    Converter c(doc, msg, images, result ? result : &local);
    return c.run(error);
}

} // namespace drav::fig
