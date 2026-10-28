#include "SvgImport.h"

#include "core/Geometry.h"

#include <include/core/SkPathBuilder.h>
#include <include/utils/SkParsePath.h>

#include <QColor>
#include <QHash>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <cmath>
#include <optional>

namespace drav::svg {

namespace {

constexpr float kPi = 3.14159265358979f;

struct Style {
    std::optional<QString> fill = QStringLiteral("black");
    std::optional<QString> stroke;
    float strokeWidth = 1;
    float opacity = 1;
    float fillOpacity = 1;
    float strokeOpacity = 1;
    QString lineCap, lineJoin, dashArray, fillRule;
    float fontSize = 16;
    QString fontFamily;
    int fontWeight = 400;
};

struct GradientDef {
    PaintType type = PaintType::LinearGradient;
    bool userSpace = false;
    SkPoint p1{0, 0}, p2{1, 0};
    SkPoint center{0.5f, 0.5f};
    float r = 0.5f;
    SkMatrix transform = SkMatrix::I();
    std::vector<ColorStop> stops;
    QString href;
};

float num(const QString& s, float def = 0)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([-+]?[0-9]*\\.?[0-9]+(?:[eE][-+]?[0-9]+)?)"));
    auto m = re.match(s);
    if (!m.hasMatch())
        return def;
    float v = m.captured(1).toFloat();
    if (s.trimmed().endsWith(QLatin1Char('%')))
        v /= 100;
    return v;
}

std::vector<float> numbers(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("[-+]?(?:[0-9]*\\.[0-9]+|[0-9]+\\.?)(?:[eE][-+]?[0-9]+)?"));
    std::vector<float> out;
    auto it = re.globalMatch(s);
    while (it.hasNext())
        out.push_back(it.next().captured(0).toFloat());
    return out;
}

SkMatrix parseTransform(const QString& s)
{
    SkMatrix m = SkMatrix::I();
    static const QRegularExpression re(QStringLiteral("(matrix|translate|scale|rotate|skewX|skewY)\\s*\\(([^)]*)\\)"));
    auto it = re.globalMatch(s);
    while (it.hasNext()) {
        auto match = it.next();
        const QString fn = match.captured(1);
        auto a = numbers(match.captured(2));
        SkMatrix t = SkMatrix::I();
        if (fn == QLatin1String("matrix") && a.size() == 6)
            t = SkMatrix::MakeAll(a[0], a[2], a[4], a[1], a[3], a[5], 0, 0, 1);
        else if (fn == QLatin1String("translate") && !a.empty())
            t = SkMatrix::Translate(a[0], a.size() > 1 ? a[1] : 0);
        else if (fn == QLatin1String("scale") && !a.empty())
            t = SkMatrix::Scale(a[0], a.size() > 1 ? a[1] : a[0]);
        else if (fn == QLatin1String("rotate") && !a.empty())
            t = a.size() >= 3 ? SkMatrix::RotateDeg(a[0], {a[1], a[2]}) : SkMatrix::RotateDeg(a[0]);
        else if (fn == QLatin1String("skewX") && !a.empty())
            t = SkMatrix::Skew(std::tan(a[0] * kPi / 180), 0);
        else if (fn == QLatin1String("skewY") && !a.empty())
            t = SkMatrix::Skew(0, std::tan(a[0] * kPi / 180));
        m.preConcat(t);
    }
    return m;
}

std::optional<SkColor4f> parseColor(QString s, float opacity)
{
    s = s.trimmed();
    if (s.isEmpty() || s == QLatin1String("none") || s == QLatin1String("transparent"))
        return std::nullopt;
    QColor c;
    if (s.startsWith(QLatin1String("rgb"))) {
        auto n = numbers(s);
        if (n.size() >= 3)
            c = QColor::fromRgbF(n[0] / 255.f, n[1] / 255.f, n[2] / 255.f, n.size() > 3 ? n[3] : 1.f);
    } else if (s.size() == 4 && s.startsWith(QLatin1Char('#'))) {
        c = QColor(QStringLiteral("#%1%1%2%2%3%3").arg(s[1]).arg(s[2]).arg(s[3]));
    } else {
        c = QColor::fromString(s);
    }
    if (!c.isValid())
        return std::nullopt;
    return SkColor4f{float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF()) * opacity};
}

class Importer {
public:
    Importer(Document& doc, const QByteArray& data)
        : m_doc(doc)
        , m_xml(data)
    {
    }

    NodeId run(NodeId parent, const QString& name);

private:
    void readStyle(Style& st);
    void element(NodeId parent, Style st, const SkMatrix& ctm);
    void skipElement();
    void readDefs();
    void readGradient(PaintType type);
    NodeId addShape(NodeId parent, const SkPath& localPath, const Style& st, const SkMatrix& ctm, const QString& name);
    std::vector<Paint> paintFor(const std::optional<QString>& spec, float opacity, const SkRect& bounds);

    Document& m_doc;
    QXmlStreamReader m_xml;
    QHash<QString, GradientDef> m_gradients;
    int m_count = 0;
};

void Importer::readStyle(Style& st)
{
    const auto attrs = m_xml.attributes();
    auto apply = [&](const QString& key, const QString& value) {
        const QString v = value.trimmed();
        if (key == QLatin1String("fill")) st.fill = v;
        else if (key == QLatin1String("stroke")) st.stroke = v;
        else if (key == QLatin1String("stroke-width")) st.strokeWidth = num(v, 1);
        else if (key == QLatin1String("opacity")) st.opacity = num(v, 1);
        else if (key == QLatin1String("fill-opacity")) st.fillOpacity = num(v, 1);
        else if (key == QLatin1String("stroke-opacity")) st.strokeOpacity = num(v, 1);
        else if (key == QLatin1String("stroke-linecap")) st.lineCap = v;
        else if (key == QLatin1String("stroke-linejoin")) st.lineJoin = v;
        else if (key == QLatin1String("stroke-dasharray")) st.dashArray = v;
        else if (key == QLatin1String("fill-rule")) st.fillRule = v;
        else if (key == QLatin1String("font-size")) st.fontSize = num(v, 16);
        else if (key == QLatin1String("font-family")) st.fontFamily = v.section(QLatin1Char(','), 0, 0).remove(QLatin1Char('\'')).remove(QLatin1Char('"')).trimmed();
        else if (key == QLatin1String("font-weight")) st.fontWeight = v == QLatin1String("bold") ? 700 : int(num(v, 400));
    };
    for (const auto& a : attrs)
        apply(a.name().toString(), a.value().toString());
    const QString style = attrs.value(QLatin1String("style")).toString();
    for (const QString& decl : style.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        int c = decl.indexOf(QLatin1Char(':'));
        if (c > 0)
            apply(decl.left(c).trimmed(), decl.mid(c + 1));
    }
}

void Importer::skipElement()
{
    int depth = 1;
    while (depth > 0 && !m_xml.atEnd()) {
        auto t = m_xml.readNext();
        if (t == QXmlStreamReader::StartElement)
            ++depth;
        else if (t == QXmlStreamReader::EndElement)
            --depth;
    }
}

void Importer::readGradient(PaintType type)
{
    const auto a = m_xml.attributes();
    GradientDef g;
    g.type = type;
    g.userSpace = a.value(QLatin1String("gradientUnits")) == QLatin1String("userSpaceOnUse");
    g.transform = parseTransform(a.value(QLatin1String("gradientTransform")).toString());
    g.href = a.value(QLatin1String("xlink:href")).toString();
    if (g.href.isEmpty())
        g.href = a.value(QLatin1String("href")).toString();
    if (type == PaintType::LinearGradient) {
        g.p1 = {num(a.value(QLatin1String("x1")).toString(), 0), num(a.value(QLatin1String("y1")).toString(), 0)};
        g.p2 = {a.hasAttribute(QLatin1String("x2")) ? num(a.value(QLatin1String("x2")).toString(), 1) : 1.f,
            num(a.value(QLatin1String("y2")).toString(), 0)};
    } else {
        g.center = {a.hasAttribute(QLatin1String("cx")) ? num(a.value(QLatin1String("cx")).toString()) : 0.5f,
            a.hasAttribute(QLatin1String("cy")) ? num(a.value(QLatin1String("cy")).toString()) : 0.5f};
        g.r = a.hasAttribute(QLatin1String("r")) ? num(a.value(QLatin1String("r")).toString()) : 0.5f;
    }
    const QString id = a.value(QLatin1String("id")).toString();
    while (!m_xml.atEnd()) {
        auto t = m_xml.readNext();
        if (t == QXmlStreamReader::EndElement)
            break;
        if (t != QXmlStreamReader::StartElement)
            continue;
        if (m_xml.name() == QLatin1String("stop")) {
            const auto sa = m_xml.attributes();
            QString color = sa.value(QLatin1String("stop-color")).toString();
            float op = sa.hasAttribute(QLatin1String("stop-opacity")) ? num(sa.value(QLatin1String("stop-opacity")).toString(), 1) : 1;
            for (const QString& decl : sa.value(QLatin1String("style")).toString().split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                int c = decl.indexOf(QLatin1Char(':'));
                if (c <= 0)
                    continue;
                const QString k = decl.left(c).trimmed(), v = decl.mid(c + 1).trimmed();
                if (k == QLatin1String("stop-color")) color = v;
                if (k == QLatin1String("stop-opacity")) op = num(v, 1);
            }
            auto col = parseColor(color.isEmpty() ? QStringLiteral("black") : color, op);
            g.stops.push_back({std::clamp(num(sa.value(QLatin1String("offset")).toString()), 0.f, 1.f), col.value_or(SkColors::kTransparent)});
        }
        skipElement();
    }
    if (!id.isEmpty())
        m_gradients.insert(id, g);
}

void Importer::readDefs()
{
    while (!m_xml.atEnd()) {
        auto t = m_xml.readNext();
        if (t == QXmlStreamReader::EndElement)
            return;
        if (t != QXmlStreamReader::StartElement)
            continue;
        if (m_xml.name() == QLatin1String("linearGradient"))
            readGradient(PaintType::LinearGradient);
        else if (m_xml.name() == QLatin1String("radialGradient"))
            readGradient(PaintType::RadialGradient);
        else
            skipElement();
    }
}

std::vector<Paint> Importer::paintFor(const std::optional<QString>& spec, float opacity, const SkRect& bounds)
{
    if (!spec)
        return {};
    static const QRegularExpression urlRe(QStringLiteral("url\\(\\s*#([^)\\s]+)\\s*\\)"));
    auto m = urlRe.match(*spec);
    if (m.hasMatch() && m_gradients.contains(m.captured(1))) {
        GradientDef g = m_gradients.value(m.captured(1));
        if (g.stops.empty() && !g.href.isEmpty())
            g.stops = m_gradients.value(g.href.mid(1)).stops;
        Paint p;
        p.type = g.type;
        p.opacity = opacity;
        p.stops = g.stops;
        if (p.stops.size() < 2)
            p.stops = {{0, SkColors::kBlack}, {1, SkColors::kBlack}};
        auto toUnit = [&](SkPoint pt) {
            pt = g.transform.mapPoint(pt);
            if (g.userSpace && bounds.width() > 0 && bounds.height() > 0)
                return SkPoint{(pt.x() - bounds.left()) / bounds.width(), (pt.y() - bounds.top()) / bounds.height()};
            return pt;
        };
        if (g.type == PaintType::LinearGradient) {
            p.gradStart = toUnit(g.p1);
            p.gradEnd = toUnit(g.p2);
        } else {
            p.gradStart = toUnit(g.center);
            p.gradEnd = toUnit({g.center.x() + g.r, g.center.y()});
            SkPoint ry = toUnit({g.center.x(), g.center.y() + g.r});
            const float rx = SkPoint::Distance(p.gradStart, p.gradEnd);
            p.gradWidth = rx > 0 ? SkPoint::Distance(p.gradStart, ry) / rx : 1;
        }
        return {p};
    }
    auto c = parseColor(*spec, 1);
    if (!c)
        return {};
    Paint p = Paint::solid(*c);
    p.opacity = opacity;
    return {p};
}

NodeId Importer::addShape(NodeId parent, const SkPath& localPath, const Style& st, const SkMatrix& ctm, const QString& name)
{
    SkPath path = localPath.makeTransform(ctm);
    if (st.fillRule == QLatin1String("evenodd"))
        path.setFillType(SkPathFillType::kEvenOdd);
    SkRect b = path.computeTightBounds();
    Props p;
    p.type = NodeType::Vector;
    p.name = name.isEmpty() ? QStringLiteral("Vector %1").arg(++m_count) : name;
    p.fills = paintFor(st.fill, st.fillOpacity, b);
    p.strokes = paintFor(st.stroke, st.strokeOpacity, b);
    const float scale = std::sqrt(std::fabs(ctm.getScaleX() * ctm.getScaleY() - ctm.getSkewX() * ctm.getSkewY()));
    p.stroke.weight = st.strokeWidth * (scale > 0 ? scale : 1);
    p.stroke.align = StrokeAlign::Center;
    if (st.lineCap == QLatin1String("round")) p.stroke.startCap = p.stroke.endCap = StrokeCap::Round;
    if (st.lineCap == QLatin1String("square")) p.stroke.startCap = p.stroke.endCap = StrokeCap::Square;
    if (st.lineJoin == QLatin1String("round")) p.stroke.join = StrokeJoin::Round;
    if (st.lineJoin == QLatin1String("bevel")) p.stroke.join = StrokeJoin::Bevel;
    for (float d : numbers(st.dashArray))
        p.stroke.dashes.push_back(d * scale);
    p.opacity = st.opacity;
    p.vector = vectorFromPath(path);
    p.vector.transform(SkMatrix::Translate(-b.left(), -b.top()));
    p.transform = SkMatrix::Translate(b.left(), b.top());
    p.width = std::max(b.width(), 0.01f);
    p.height = std::max(b.height(), 0.01f);
    return m_doc.create(p, parent);
}

void Importer::element(NodeId parent, Style st, const SkMatrix& parentCtm)
{
    const QString tag = m_xml.name().toString();
    const auto a = m_xml.attributes();
    readStyle(st);
    SkMatrix ctm = SkMatrix::Concat(parentCtm, parseTransform(a.value(QLatin1String("transform")).toString()));
    const QString id = a.value(QLatin1String("id")).toString();
    auto attr = [&](const char* k, float def = 0) { return num(a.value(QLatin1String(k)).toString(), def); };

    if (tag == QLatin1String("defs")) {
        readDefs();
        return;
    }
    if (tag == QLatin1String("linearGradient")) {
        readGradient(PaintType::LinearGradient);
        return;
    }
    if (tag == QLatin1String("radialGradient")) {
        readGradient(PaintType::RadialGradient);
        return;
    }
    if (tag == QLatin1String("g") || tag == QLatin1String("a") || tag == QLatin1String("switch")) {
        Props gp;
        gp.type = NodeType::Group;
        gp.name = id.isEmpty() ? QStringLiteral("Group") : id;
        gp.opacity = st.opacity;
        NodeId g = m_doc.create(gp, parent);
        Style inner = st;
        inner.opacity = 1;
        while (!m_xml.atEnd()) {
            auto t = m_xml.readNext();
            if (t == QXmlStreamReader::EndElement)
                break;
            if (t == QXmlStreamReader::StartElement)
                element(g, inner, ctm);
        }
        if (m_doc.at(g).children.empty())
            m_doc.remove(g);
        return;
    }

    SkPath path;
    bool shape = true;
    if (tag == QLatin1String("path")) {
        SkParsePath::FromSVGString(a.value(QLatin1String("d")).toString().toUtf8().constData(), &path);
    } else if (tag == QLatin1String("rect")) {
        SkRect r = SkRect::MakeXYWH(attr("x"), attr("y"), attr("width"), attr("height"));
        float rx = a.hasAttribute(QLatin1String("rx")) ? attr("rx") : attr("ry");
        float ry = a.hasAttribute(QLatin1String("ry")) ? attr("ry") : rx;
        path = SkPath::RRect(r, rx, ry);
    } else if (tag == QLatin1String("circle")) {
        float r = attr("r");
        path = SkPath::Circle(attr("cx"), attr("cy"), r);
    } else if (tag == QLatin1String("ellipse")) {
        float cx = attr("cx"), cy = attr("cy"), rx = attr("rx"), ry = attr("ry");
        path = SkPath::Oval(SkRect::MakeLTRB(cx - rx, cy - ry, cx + rx, cy + ry));
    } else if (tag == QLatin1String("line")) {
        path = SkPath::Line({attr("x1"), attr("y1")}, {attr("x2"), attr("y2")});
        if (!st.stroke)
            st.stroke = QStringLiteral("black");
    } else if (tag == QLatin1String("polyline") || tag == QLatin1String("polygon")) {
        auto n = numbers(a.value(QLatin1String("points")).toString());
        std::vector<SkPoint> pts;
        for (size_t i = 0; i + 1 < n.size(); i += 2)
            pts.push_back({n[i], n[i + 1]});
        path = SkPath::Polygon(pts.data(), int(pts.size()), tag == QLatin1String("polygon"));
    } else if (tag == QLatin1String("text")) {
        shape = false;
        const float x = attr("x"), y = attr("y");
        QString content = m_xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
        if (content.isEmpty())
            return;
        Props p;
        p.type = NodeType::Text;
        p.name = content.left(40);
        p.text = content;
        p.fills = paintFor(st.fill, st.fillOpacity, {});
        p.textStyle.size = st.fontSize * std::sqrt(std::fabs(ctm.getScaleX() * ctm.getScaleY()));
        if (!st.fontFamily.isEmpty())
            p.textStyle.family = st.fontFamily;
        p.textStyle.weight = st.fontWeight;
        p.opacity = st.opacity;
        SkPoint o = ctm.mapXY(x, y - st.fontSize * 0.9f);
        p.transform = SkMatrix::Translate(o.x(), o.y());
        m_doc.create(p, parent);
        return;
    } else {
        shape = false;
    }
    if (shape && !path.isEmpty())
        addShape(parent, path, st, ctm, id);
    skipElement();
}

NodeId Importer::run(NodeId parent, const QString& name)
{
    while (!m_xml.atEnd() && !(m_xml.readNext() == QXmlStreamReader::StartElement && m_xml.name() == QLatin1String("svg"))) {
    }
    if (m_xml.atEnd() || m_xml.hasError())
        return kNoNode;
    const auto a = m_xml.attributes();
    auto vb = numbers(a.value(QLatin1String("viewBox")).toString());
    float w = num(a.value(QLatin1String("width")).toString(), vb.size() == 4 ? vb[2] : 100);
    float h = num(a.value(QLatin1String("height")).toString(), vb.size() == 4 ? vb[3] : 100);
    if (a.value(QLatin1String("width")).endsWith(QLatin1Char('%')) && vb.size() == 4)
        w = vb[2];
    if (a.value(QLatin1String("height")).endsWith(QLatin1Char('%')) && vb.size() == 4)
        h = vb[3];
    SkMatrix root = SkMatrix::I();
    if (vb.size() == 4 && vb[2] > 0 && vb[3] > 0) {
        root = SkMatrix::Scale(w / vb[2], h / vb[3]);
        root.preTranslate(-vb[0], -vb[1]);
    }
    Props fp;
    fp.type = NodeType::Frame;
    fp.name = name.isEmpty() ? QStringLiteral("SVG") : name;
    fp.clipsContent = false;
    NodeId frame = m_doc.create(fp, parent);
    m_doc.resize(frame, std::max(w, 1.f), std::max(h, 1.f));
    Style st;
    readStyle(st);
    while (!m_xml.atEnd()) {
        auto t = m_xml.readNext();
        if (t == QXmlStreamReader::EndElement && m_xml.name() == QLatin1String("svg"))
            break;
        if (t == QXmlStreamReader::StartElement)
            element(frame, st, root);
    }
    return frame;
}

} // namespace

NodeId import(Document& doc, const QByteArray& data, NodeId parent, const QString& name)
{
    Importer imp(doc, data);
    return imp.run(parent, name);
}

} // namespace drav::svg
