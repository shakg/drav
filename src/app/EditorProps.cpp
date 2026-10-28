#include "Editor.h"

#include "core/Text.h"

#include <QColor>

#include <cmath>

namespace drav {

namespace {

QColor toQColor(SkColor4f c) { return QColor::fromRgbF(c.fR, c.fG, c.fB, c.fA); }
SkColor4f toSk(const QVariant& v)
{
    QColor c = v.value<QColor>();
    if (!c.isValid())
        c = QColor(v.toString());
    return {float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())};
}

template <typename E, size_t N> QString enumName(E v, const char* const (&names)[N])
{
    size_t i = size_t(v);
    return i < N ? QString::fromLatin1(names[i]) : QString();
}
template <typename E, size_t N> E enumValue(const QVariant& v, const char* const (&names)[N], E def)
{
    const QString s = v.toString();
    for (size_t i = 0; i < N; ++i)
        if (s == QLatin1String(names[i]))
            return E(i);
    return def;
}

const char* const kPaintTypes[] = {"SOLID", "GRADIENT_LINEAR", "GRADIENT_RADIAL", "GRADIENT_ANGULAR", "GRADIENT_DIAMOND", "IMAGE"};
const char* const kScaleModes[] = {"FILL", "FIT", "CROP", "TILE"};
const char* const kBlends[] = {"PASS_THROUGH", "NORMAL", "DARKEN", "MULTIPLY", "COLOR_BURN", "LIGHTEN", "SCREEN",
    "COLOR_DODGE", "OVERLAY", "SOFT_LIGHT", "HARD_LIGHT", "DIFFERENCE", "EXCLUSION", "HUE", "SATURATION", "COLOR",
    "LUMINOSITY"};
const char* const kAligns[] = {"INSIDE", "CENTER", "OUTSIDE"};
const char* const kCaps[] = {"NONE", "ROUND", "SQUARE", "LINE_ARROW", "TRIANGLE_ARROW", "CIRCLE_FILLED", "DIAMOND_FILLED"};
const char* const kJoins[] = {"MITER", "BEVEL", "ROUND"};
const char* const kEffects[] = {"DROP_SHADOW", "INNER_SHADOW", "LAYER_BLUR", "BACKGROUND_BLUR"};
const char* const kConsH[] = {"LEFT", "RIGHT", "LEFT_RIGHT", "CENTER", "SCALE"};
const char* const kConsV[] = {"TOP", "BOTTOM", "TOP_BOTTOM", "CENTER", "SCALE"};
const char* const kLayoutModes[] = {"NONE", "HORIZONTAL", "VERTICAL"};
const char* const kLayoutAligns[] = {"MIN", "CENTER", "MAX", "SPACE_BETWEEN", "BASELINE"};
const char* const kSizing[] = {"FIXED", "HUG", "FILL"};
const char* const kTextH[] = {"LEFT", "CENTER", "RIGHT", "JUSTIFIED"};
const char* const kTextV[] = {"TOP", "CENTER", "BOTTOM"};
const char* const kAutoResize[] = {"NONE", "HEIGHT", "WIDTH_AND_HEIGHT", "TRUNCATE"};
const char* const kCase[] = {"ORIGINAL", "UPPER", "LOWER", "TITLE"};
const char* const kDeco[] = {"NONE", "UNDERLINE", "STRIKETHROUGH"};
const char* const kBool[] = {"UNION", "SUBTRACT", "INTERSECT", "EXCLUDE"};
const char* const kGridKinds[] = {"GRID", "COLUMNS", "ROWS"};

QVariantMap paintToMap(const Paint& p)
{
    QVariantList stops;
    for (const ColorStop& s : p.stops)
        stops << QVariantMap{{QStringLiteral("pos"), s.pos}, {QStringLiteral("color"), toQColor(s.color)}};
    return {
        {QStringLiteral("type"), enumName(p.type, kPaintTypes)},
        {QStringLiteral("color"), toQColor(p.color)},
        {QStringLiteral("opacity"), p.opacity},
        {QStringLiteral("visible"), p.visible},
        {QStringLiteral("blend"), enumName(p.blend, kBlends)},
        {QStringLiteral("stops"), stops},
        {QStringLiteral("start"), QVariantList{p.gradStart.x(), p.gradStart.y()}},
        {QStringLiteral("end"), QVariantList{p.gradEnd.x(), p.gradEnd.y()}},
        {QStringLiteral("gradWidth"), p.gradWidth},
        {QStringLiteral("image"), p.imageHash},
        {QStringLiteral("scaleMode"), enumName(p.scaleMode, kScaleModes)},
        {QStringLiteral("imageScale"), p.imageScale},
    };
}

Paint paintFromMap(const QVariantMap& m)
{
    Paint p;
    p.type = enumValue(m.value(QStringLiteral("type")), kPaintTypes, PaintType::Solid);
    if (m.contains(QStringLiteral("color")))
        p.color = toSk(m.value(QStringLiteral("color")));
    p.opacity = m.value(QStringLiteral("opacity"), 1.0).toFloat();
    p.visible = m.value(QStringLiteral("visible"), true).toBool();
    p.blend = enumValue(m.value(QStringLiteral("blend")), kBlends, BlendMode::Normal);
    const QVariantList stops = m.value(QStringLiteral("stops")).toList();
    if (!stops.isEmpty()) {
        p.stops.clear();
        for (const QVariant& s : stops) {
            QVariantMap sm = s.toMap();
            p.stops.push_back({sm.value(QStringLiteral("pos")).toFloat(), toSk(sm.value(QStringLiteral("color")))});
        }
        std::sort(p.stops.begin(), p.stops.end(), [](const ColorStop& a, const ColorStop& b) { return a.pos < b.pos; });
    }
    auto pt = [&](const char* k, SkPoint def) {
        QVariantList l = m.value(QLatin1String(k)).toList();
        return l.size() == 2 ? SkPoint{l[0].toFloat(), l[1].toFloat()} : def;
    };
    p.gradStart = pt("start", p.gradStart);
    p.gradEnd = pt("end", p.gradEnd);
    p.gradWidth = m.value(QStringLiteral("gradWidth"), 1.0).toFloat();
    p.imageHash = m.value(QStringLiteral("image")).toString();
    p.scaleMode = enumValue(m.value(QStringLiteral("scaleMode")), kScaleModes, ImageScaleMode::Fill);
    p.imageScale = m.value(QStringLiteral("imageScale"), 1.0).toFloat();
    return p;
}

QVariantList paintsToList(const std::vector<Paint>& v)
{
    QVariantList l;
    for (const Paint& p : v)
        l << paintToMap(p);
    return l;
}

std::vector<Paint> paintsFromList(const QVariant& v)
{
    std::vector<Paint> out;
    for (const QVariant& p : v.toList())
        out.push_back(paintFromMap(p.toMap()));
    return out;
}

QVariantMap effectToMap(const Effect& e)
{
    return {
        {QStringLiteral("type"), enumName(e.type, kEffects)},
        {QStringLiteral("visible"), e.visible},
        {QStringLiteral("color"), toQColor(e.color)},
        {QStringLiteral("x"), e.offset.x()},
        {QStringLiteral("y"), e.offset.y()},
        {QStringLiteral("radius"), e.radius},
        {QStringLiteral("spread"), e.spread},
        {QStringLiteral("behind"), e.showBehindTransparent},
    };
}

Effect effectFromMap(const QVariantMap& m)
{
    Effect e;
    e.type = enumValue(m.value(QStringLiteral("type")), kEffects, EffectType::DropShadow);
    e.visible = m.value(QStringLiteral("visible"), true).toBool();
    if (m.contains(QStringLiteral("color")))
        e.color = toSk(m.value(QStringLiteral("color")));
    e.offset = {m.value(QStringLiteral("x"), 0).toFloat(), m.value(QStringLiteral("y"), 4).toFloat()};
    e.radius = m.value(QStringLiteral("radius"), 4).toFloat();
    e.spread = m.value(QStringLiteral("spread"), 0).toFloat();
    e.showBehindTransparent = m.value(QStringLiteral("behind"), false).toBool();
    return e;
}

float rotationOf(const SkMatrix& m)
{
    return -std::atan2(m.getSkewY(), m.getScaleX()) * 180.f / 3.14159265f;
}

} // namespace

static QVariantMap nodeProps(const Document& d, NodeId id)
{
    const Node& n = d.at(id);
    const Props& p = n.p;
    QVariantMap m;
    auto set = [&](const char* k, const QVariant& v) { m.insert(QLatin1String(k), v); };
    set("id", qint64(id));
    set("type", QString::fromLatin1(nodeTypeName(p.type)));
    set("name", p.name);
    set("visible", p.visible);
    set("locked", p.locked);
    set("opacity", p.opacity);
    set("blend", enumName(p.blend, kBlends));
    set("isMask", p.isMask);
    set("x", std::round(p.transform.getTranslateX() * 100) / 100);
    set("y", std::round(p.transform.getTranslateY() * 100) / 100);
    set("w", std::round(p.width * 100) / 100);
    set("h", std::round(p.height * 100) / 100);
    float rot = rotationOf(p.transform);
    set("rotation", std::fabs(rot) < 0.005f ? 0.f : std::round(rot * 100) / 100);
    set("lockRatio", p.constrainProportions);
    set("cornerRadius", p.corners.uniform() ? QVariant(p.corners.tl) : QVariant());
    set("corners", QVariantList{p.corners.tl, p.corners.tr, p.corners.br, p.corners.bl});
    set("cornerSmoothing", p.cornerSmoothing);
    set("clipsContent", p.clipsContent);
    set("fills", paintsToList(p.fills));
    set("strokes", paintsToList(p.strokes));
    set("strokeWeight", p.stroke.weight);
    set("strokeAlign", enumName(p.stroke.align, kAligns));
    set("strokeJoin", enumName(p.stroke.join, kJoins));
    set("startCap", enumName(p.stroke.startCap, kCaps));
    set("endCap", enumName(p.stroke.endCap, kCaps));
    QVariantList dashes;
    for (float f : p.stroke.dashes)
        dashes << f;
    set("dashes", dashes);
    QVariantList effects;
    for (const Effect& e : p.effects)
        effects << effectToMap(e);
    set("effects", effects);
    set("constraintH", enumName(p.constraintH, kConsH));
    set("constraintV", enumName(p.constraintV, kConsV));
    set("layoutMode", enumName(p.layout.mode, kLayoutModes));
    set("layoutWrap", p.layout.wrap);
    set("spacing", p.layout.spacing);
    set("counterSpacing", p.layout.counterSpacing);
    set("padL", p.layout.padL);
    set("padT", p.layout.padT);
    set("padR", p.layout.padR);
    set("padB", p.layout.padB);
    set("primaryAlign", enumName(p.layout.primaryAlign, kLayoutAligns));
    set("counterAlign", enumName(p.layout.counterAlign, kLayoutAligns));
    set("strokesIncluded", p.layout.strokesIncluded);
    set("reverseZ", p.layout.reverseZ);
    set("sizingH", enumName(p.layoutChild.sizingH, kSizing));
    set("sizingV", enumName(p.layoutChild.sizingV, kSizing));
    set("absolute", p.layoutChild.absolute);
    set("minW", p.layoutChild.minW);
    set("maxW", p.layoutChild.maxW);
    set("minH", p.layoutChild.minH);
    set("maxH", p.layoutChild.maxH);
    const bool parentAL = n.parent && d.at(n.parent).p.layout.mode != LayoutMode::None;
    set("inAutoLayout", parentAL);
    set("parentType", n.parent ? QString::fromLatin1(nodeTypeName(d.at(n.parent).type())) : QString());
    if (p.type == NodeType::Text) {
        const TextStyle& t = p.textStyle;
        set("characters", p.text);
        set("fontFamily", t.family);
        set("fontWeight", t.weight);
        set("italic", t.italic);
        set("fontSize", t.size);
        set("lineHeight", t.lineHeight);
        set("lineHeightPercent", t.lineHeightPercent);
        set("letterSpacing", t.letterSpacing);
        set("paragraphSpacing", t.paragraphSpacing);
        set("textAlignH", enumName(t.alignH, kTextH));
        set("textAlignV", enumName(t.alignV, kTextV));
        set("autoResize", enumName(t.autoResize, kAutoResize));
        set("textCase", enumName(t.textCase, kCase));
        set("decoration", enumName(t.decoration, kDeco));
        set("maxLines", t.maxLines);
    }
    if (p.type == NodeType::Ellipse) {
        set("arcStart", p.arcStart);
        set("arcSweep", p.arcSweep);
        set("arcInner", p.arcInner);
    }
    if (p.type == NodeType::Polygon || p.type == NodeType::Star) {
        set("pointCount", p.pointCount);
        set("starInner", p.starInner);
    }
    if (p.type == NodeType::BooleanOp)
        set("boolOp", enumName(p.boolOp, kBool));
    QVariantList grids;
    for (const LayoutGrid& g : p.grids)
        grids << QVariantMap{{QStringLiteral("kind"), enumName(g.kind, kGridKinds)}, {QStringLiteral("count"), g.count},
            {QStringLiteral("size"), g.size}, {QStringLiteral("gutter"), g.gutter}, {QStringLiteral("margin"), g.margin},
            {QStringLiteral("color"), toQColor(g.color)}, {QStringLiteral("visible"), g.visible}};
    set("grids", grids);
    QVariantList exports;
    for (const ExportSetting& e : p.exports)
        exports << QVariantMap{{QStringLiteral("format"), e.format}, {QStringLiteral("scale"), e.scale},
            {QStringLiteral("suffix"), e.suffix}};
    set("exports", exports);

    // Components
    set("isComponent", p.type == NodeType::Component);
    set("isComponentSet", p.type == NodeType::ComponentSet);
    set("isInstance", p.type == NodeType::Instance);
    set("insideInstance", d.isInsideInstance(id));
    set("description", p.description);
    set("overrideCount", 0);
    if (p.type == NodeType::Instance) {
        const bool mainOk = d.isAttached(p.componentId);
        set("mainMissing", !mainOk);
        int ov = 0;
        d.walk(id, [&](const Node& c) {
            ov += c.p.overrides != 0;
            return true;
        });
        set("overrideCount", ov);
        if (mainOk) {
            const Node& main = d.at(p.componentId);
            set("componentName", main.p.name);
            const Node& set_ = d.at(main.parent);
            if (set_.type() == NodeType::ComponentSet) {
                set("componentName", set_.p.name);
                QMap<QString, QStringList> options;
                for (NodeId c : set_.children) {
                    const auto& vp = d.at(c).p.variantProps;
                    for (auto it = vp.begin(); it != vp.end(); ++it)
                        if (!options[it.key()].contains(it.value()))
                            options[it.key()] << it.value();
                }
                QVariantList variants;
                for (auto it = options.begin(); it != options.end(); ++it)
                    variants << QVariantMap{{QStringLiteral("name"), it.key()},
                        {QStringLiteral("value"), main.p.variantProps.value(it.key())},
                        {QStringLiteral("options"), it.value()}};
                set("variants", variants);
            }
        }
    }
    return m;
}

QVariantMap Editor::props() const
{
    const auto& sel = m_doc->selection();
    QVariantMap out;
    out.insert(QStringLiteral("count"), int(sel.size()));
    out.insert(QStringLiteral("pageBackground"), toQColor(m_doc->at(m_doc->currentPage()).p.pageBackground));
    if (sel.empty())
        return out;
    QVariantMap first = nodeProps(*m_doc, sel[0]);
    bool sameType = true;
    for (size_t i = 1; i < sel.size(); ++i) {
        QVariantMap other = nodeProps(*m_doc, sel[i]);
        if (other.value(QStringLiteral("type")) != first.value(QStringLiteral("type")))
            sameType = false;
        for (auto it = first.begin(); it != first.end(); ++it)
            if (it.key() != QLatin1String("id") && other.value(it.key()) != it.value())
                it.value() = QVariant(); // mixed
    }
    if (sel.size() > 1) {
        SkRect b = selectionBounds();
        first.insert(QStringLiteral("selX"), std::round(b.left() * 100) / 100);
        first.insert(QStringLiteral("selY"), std::round(b.top() * 100) / 100);
        first.insert(QStringLiteral("selW"), std::round(b.width() * 100) / 100);
        first.insert(QStringLiteral("selH"), std::round(b.height() * 100) / 100);
    }
    first.insert(QStringLiteral("sameType"), sameType);
    for (auto it = first.begin(); it != first.end(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

void Editor::setProp(const QString& key, const QVariant& value)
{
    if (key == QLatin1String("pageBackground")) {
        m_doc->begin(tr("Page color"));
        m_doc->edit(m_doc->currentPage()).pageBackground = toSk(value);
        m_doc->commit();
        return;
    }
    auto sel = selected();
    if (sel.empty())
        return;
    if (!m_live)
        m_doc->breakMerge();
    m_doc->begin(tr("Change %1").arg(key));
    if (m_live)
        m_doc->setMergeKey(QStringLiteral("live:") + m_liveKey);
    for (NodeId id : sel)
        applyProp(id, key, value);
    m_doc->commit();
}

void Editor::setPropLive(const QString& key, const QVariant& value)
{
    if (!m_live || m_liveKey != key) {
        m_doc->breakMerge();
        m_live = true;
        m_liveKey = key;
    }
    setProp(key, value);
}

void Editor::endLive()
{
    m_live = false;
    m_liveKey.clear();
    m_doc->breakMerge();
}

void Editor::applyProp(NodeId id, const QString& key, const QVariant& v)
{
    const Node& n = m_doc->at(id);
    const Props& cur = n.p;
    auto e = [&]() -> Props& { return m_doc->edit(id); };
    const float f = v.toFloat();
    const QString k = key;

    if (k == QLatin1String("name")) {
        if (!v.toString().isEmpty())
            renameNode(qint64(id), v.toString());
        return;
    }
    if (k == QLatin1String("x") || k == QLatin1String("y")) {
        const bool x = k == QLatin1String("x");
        m_doc->setPosition(id, x ? f : cur.transform.getTranslateX(), x ? cur.transform.getTranslateY() : f);
    } else if (k == QLatin1String("selX") || k == QLatin1String("selY")) {
        SkRect b = selectionBounds();
        const float dx = k == QLatin1String("selX") ? f - b.left() : 0;
        const float dy = k == QLatin1String("selY") ? f - b.top() : 0;
        SkMatrix w = m_doc->worldTransform(id);
        w.postTranslate(dx, dy);
        m_doc->setWorldTransform(id, w);
    } else if (k == QLatin1String("w") || k == QLatin1String("h")) {
        const bool isW = k == QLatin1String("w");
        float w = isW ? f : cur.width, h = isW ? cur.height : f;
        if (cur.constrainProportions && cur.width > 0 && cur.height > 0) {
            if (isW)
                h = cur.height * f / cur.width;
            else
                w = cur.width * f / cur.height;
        }
        if (isW && cur.layoutChild.sizingH != LayoutSizing::Fixed)
            e().layoutChild.sizingH = LayoutSizing::Fixed;
        if (!isW && cur.layoutChild.sizingV != LayoutSizing::Fixed)
            e().layoutChild.sizingV = LayoutSizing::Fixed;
        if (cur.type == NodeType::Text) {
            TextAutoResize& ar = e().textStyle.autoResize;
            if (isW && ar == TextAutoResize::WidthAndHeight)
                ar = TextAutoResize::Height;
            else if (!isW)
                ar = TextAutoResize::None;
        }
        m_doc->resize(id, std::max(w, 0.01f), std::max(h, 0.01f));
    } else if (k == QLatin1String("rotation")) {
        const float delta = -(f - rotationOf(cur.transform));
        Props& p = e();
        p.transform.preRotate(delta, p.width / 2, p.height / 2);
    } else if (k == QLatin1String("lockRatio")) {
        e().constrainProportions = v.toBool();
    } else if (k == QLatin1String("visible")) {
        e().visible = v.toBool();
    } else if (k == QLatin1String("locked")) {
        e().locked = v.toBool();
    } else if (k == QLatin1String("opacity")) {
        e().opacity = std::clamp(f, 0.f, 1.f);
    } else if (k == QLatin1String("blend")) {
        e().blend = enumValue(v, kBlends, BlendMode::PassThrough);
    } else if (k == QLatin1String("isMask")) {
        e().isMask = v.toBool();
    } else if (k == QLatin1String("cornerRadius")) {
        const float r = std::max(0.f, f);
        e().corners = {r, r, r, r};
    } else if (k == QLatin1String("corners")) {
        QVariantList l = v.toList();
        if (l.size() == 4)
            e().corners = {l[0].toFloat(), l[1].toFloat(), l[2].toFloat(), l[3].toFloat()};
    } else if (k == QLatin1String("cornerSmoothing")) {
        e().cornerSmoothing = std::clamp(f, 0.f, 1.f);
    } else if (k == QLatin1String("clipsContent")) {
        e().clipsContent = v.toBool();
    } else if (k == QLatin1String("fills")) {
        e().fills = paintsFromList(v);
    } else if (k == QLatin1String("strokes")) {
        e().strokes = paintsFromList(v);
    } else if (k == QLatin1String("strokeWeight")) {
        e().stroke.weight = std::max(0.f, f);
    } else if (k == QLatin1String("strokeAlign")) {
        e().stroke.align = enumValue(v, kAligns, StrokeAlign::Inside);
    } else if (k == QLatin1String("strokeJoin")) {
        e().stroke.join = enumValue(v, kJoins, StrokeJoin::Miter);
    } else if (k == QLatin1String("startCap")) {
        e().stroke.startCap = enumValue(v, kCaps, StrokeCap::None);
    } else if (k == QLatin1String("endCap")) {
        e().stroke.endCap = enumValue(v, kCaps, StrokeCap::None);
    } else if (k == QLatin1String("dashes")) {
        std::vector<float> d;
        for (const QVariant& x : v.toList())
            if (x.toFloat() > 0)
                d.push_back(x.toFloat());
        e().stroke.dashes = d;
    } else if (k == QLatin1String("effects")) {
        std::vector<Effect> fx;
        for (const QVariant& x : v.toList())
            fx.push_back(effectFromMap(x.toMap()));
        e().effects = fx;
    } else if (k == QLatin1String("constraintH")) {
        e().constraintH = enumValue(v, kConsH, ConstraintH::Left);
    } else if (k == QLatin1String("constraintV")) {
        e().constraintV = enumValue(v, kConsV, ConstraintV::Top);
    } else if (k == QLatin1String("layoutMode")) {
        LayoutMode mode = enumValue(v, kLayoutModes, LayoutMode::None);
        if (mode != LayoutMode::None && cur.layout.mode == LayoutMode::None && isFrameLike(cur.type)) {
            addAutoLayoutTo(id, mode);
            return;
        }
        e().layout.mode = mode;
    } else if (k == QLatin1String("layoutWrap")) {
        e().layout.wrap = v.toBool();
    } else if (k == QLatin1String("spacing")) {
        e().layout.spacing = f;
    } else if (k == QLatin1String("counterSpacing")) {
        e().layout.counterSpacing = f;
    } else if (k == QLatin1String("padL")) {
        e().layout.padL = std::max(0.f, f);
    } else if (k == QLatin1String("padT")) {
        e().layout.padT = std::max(0.f, f);
    } else if (k == QLatin1String("padR")) {
        e().layout.padR = std::max(0.f, f);
    } else if (k == QLatin1String("padB")) {
        e().layout.padB = std::max(0.f, f);
    } else if (k == QLatin1String("padH")) {
        e().layout.padL = e().layout.padR = std::max(0.f, f);
    } else if (k == QLatin1String("padV")) {
        e().layout.padT = e().layout.padB = std::max(0.f, f);
    } else if (k == QLatin1String("primaryAlign")) {
        e().layout.primaryAlign = enumValue(v, kLayoutAligns, LayoutAlign::Min);
    } else if (k == QLatin1String("counterAlign")) {
        e().layout.counterAlign = enumValue(v, kLayoutAligns, LayoutAlign::Min);
    } else if (k == QLatin1String("strokesIncluded")) {
        e().layout.strokesIncluded = v.toBool();
    } else if (k == QLatin1String("reverseZ")) {
        e().layout.reverseZ = v.toBool();
    } else if (k == QLatin1String("sizingH") || k == QLatin1String("sizingV")) {
        LayoutSizing s = enumValue(v, kSizing, LayoutSizing::Fixed);
        const bool h = k == QLatin1String("sizingH");
        if (s == LayoutSizing::Hug && !(cur.layout.mode != LayoutMode::None || cur.type == NodeType::Text))
            return;
        if (s == LayoutSizing::Fill && !(n.parent && m_doc->at(n.parent).p.layout.mode != LayoutMode::None))
            return;
        Props& p = e();
        (h ? p.layoutChild.sizingH : p.layoutChild.sizingV) = s;
        if (cur.type == NodeType::Text) {
            // Hug on text maps to auto width/height.
            if (h && s == LayoutSizing::Hug)
                p.textStyle.autoResize = TextAutoResize::WidthAndHeight;
            else if (h && s != LayoutSizing::Hug && p.textStyle.autoResize == TextAutoResize::WidthAndHeight)
                p.textStyle.autoResize = TextAutoResize::Height;
            else if (!h && s == LayoutSizing::Hug && p.textStyle.autoResize == TextAutoResize::None)
                p.textStyle.autoResize = TextAutoResize::Height;
            else if (!h && s == LayoutSizing::Fixed)
                p.textStyle.autoResize = TextAutoResize::None;
        }
    } else if (k == QLatin1String("absolute")) {
        e().layoutChild.absolute = v.toBool();
    } else if (k == QLatin1String("minW")) {
        e().layoutChild.minW = std::max(0.f, f);
    } else if (k == QLatin1String("maxW")) {
        e().layoutChild.maxW = std::max(0.f, f);
    } else if (k == QLatin1String("minH")) {
        e().layoutChild.minH = std::max(0.f, f);
    } else if (k == QLatin1String("maxH")) {
        e().layoutChild.maxH = std::max(0.f, f);
    } else if (cur.type == NodeType::Text && k == QLatin1String("characters")) {
        e().text = v.toString();
    } else if (cur.type == NodeType::Text && k == QLatin1String("fontFamily")) {
        e().textStyle.family = v.toString();
    } else if (cur.type == NodeType::Text && k == QLatin1String("fontWeight")) {
        e().textStyle.weight = std::clamp(v.toInt(), 100, 1000);
    } else if (cur.type == NodeType::Text && k == QLatin1String("italic")) {
        e().textStyle.italic = v.toBool();
    } else if (cur.type == NodeType::Text && k == QLatin1String("fontSize")) {
        e().textStyle.size = std::clamp(f, 1.f, 2000.f);
    } else if (cur.type == NodeType::Text && k == QLatin1String("lineHeight")) {
        e().textStyle.lineHeight = std::max(0.f, f);
    } else if (cur.type == NodeType::Text && k == QLatin1String("lineHeightPercent")) {
        e().textStyle.lineHeightPercent = v.toBool();
    } else if (cur.type == NodeType::Text && k == QLatin1String("letterSpacing")) {
        e().textStyle.letterSpacing = f;
    } else if (cur.type == NodeType::Text && k == QLatin1String("paragraphSpacing")) {
        e().textStyle.paragraphSpacing = std::max(0.f, f);
    } else if (cur.type == NodeType::Text && k == QLatin1String("textAlignH")) {
        e().textStyle.alignH = enumValue(v, kTextH, TextAlignH::Left);
    } else if (cur.type == NodeType::Text && k == QLatin1String("textAlignV")) {
        e().textStyle.alignV = enumValue(v, kTextV, TextAlignV::Top);
    } else if (cur.type == NodeType::Text && k == QLatin1String("autoResize")) {
        Props& p = e();
        p.textStyle.autoResize = enumValue(v, kAutoResize, TextAutoResize::WidthAndHeight);
        p.layoutChild.sizingH = p.textStyle.autoResize == TextAutoResize::WidthAndHeight ? LayoutSizing::Hug
            : p.layoutChild.sizingH == LayoutSizing::Fill                                   ? LayoutSizing::Fill
                                                                                            : LayoutSizing::Fixed;
        p.layoutChild.sizingV = (p.textStyle.autoResize == TextAutoResize::WidthAndHeight
                                    || p.textStyle.autoResize == TextAutoResize::Height)
            ? LayoutSizing::Hug
            : LayoutSizing::Fixed;
    } else if (cur.type == NodeType::Text && k == QLatin1String("textCase")) {
        e().textStyle.textCase = enumValue(v, kCase, TextCase::Original);
    } else if (cur.type == NodeType::Text && k == QLatin1String("decoration")) {
        e().textStyle.decoration = enumValue(v, kDeco, TextDecoration::None);
    } else if (cur.type == NodeType::Text && k == QLatin1String("maxLines")) {
        e().textStyle.maxLines = std::max(0, v.toInt());
    } else if (k == QLatin1String("arcStart")) {
        e().arcStart = f;
    } else if (k == QLatin1String("arcSweep")) {
        e().arcSweep = std::clamp(f, -360.f, 360.f);
    } else if (k == QLatin1String("arcInner")) {
        e().arcInner = std::clamp(f, 0.f, 1.f);
    } else if (k == QLatin1String("pointCount")) {
        e().pointCount = std::clamp(v.toInt(), 3, 60);
    } else if (k == QLatin1String("starInner")) {
        e().starInner = std::clamp(f, 0.01f, 1.f);
    } else if (k == QLatin1String("boolOp")) {
        e().boolOp = enumValue(v, kBool, BooleanOpType::Union);
    } else if (k == QLatin1String("description")) {
        e().description = v.toString();
    } else if (k == QLatin1String("grids")) {
        std::vector<LayoutGrid> grids;
        for (const QVariant& x : v.toList()) {
            QVariantMap gm = x.toMap();
            LayoutGrid g;
            g.kind = enumValue(gm.value(QStringLiteral("kind")), kGridKinds, LayoutGrid::Kind::Columns);
            g.count = gm.value(QStringLiteral("count"), 12).toInt();
            g.size = gm.value(QStringLiteral("size"), 10).toFloat();
            g.gutter = gm.value(QStringLiteral("gutter"), 20).toFloat();
            g.margin = gm.value(QStringLiteral("margin"), 0).toFloat();
            if (gm.contains(QStringLiteral("color")))
                g.color = toSk(gm.value(QStringLiteral("color")));
            g.visible = gm.value(QStringLiteral("visible"), true).toBool();
            grids.push_back(g);
        }
        e().grids = grids;
    } else if (k == QLatin1String("exports")) {
        std::vector<ExportSetting> ex;
        for (const QVariant& x : v.toList()) {
            QVariantMap em = x.toMap();
            ex.push_back({em.value(QStringLiteral("format"), QStringLiteral("PNG")).toString(),
                em.value(QStringLiteral("scale"), 1).toFloat(), em.value(QStringLiteral("suffix")).toString()});
        }
        e().exports = ex;
    }
}

} // namespace drav
