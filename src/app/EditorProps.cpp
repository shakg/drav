#include "Editor.h"

#include "PropMaps.h"
#include "Tools.h"

#include "core/Text.h"
#include "core/Variables.h"

#include <QColor>

#include <cmath>

namespace drav {

using namespace maps;

namespace {

std::uint32_t runField(const QString& k)
{
    static const QHash<QString, std::uint32_t> m = {{QStringLiteral("fontFamily"), RunFamily},
        {QStringLiteral("fontWeight"), RunWeight}, {QStringLiteral("italic"), RunItalic},
        {QStringLiteral("fontSize"), RunSize}, {QStringLiteral("letterSpacing"), RunLetterSpacing},
        {QStringLiteral("decoration"), RunDecoration}, {QStringLiteral("textCase"), RunCase},
        {QStringLiteral("fills"), RunFill}, {QStringLiteral("link"), RunLink}};
    return m.value(k, 0);
}

const char* const kLists[] = {"NONE", "BULLET", "ORDERED"};

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
    set("effects", effectsToList(p.effects));
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
    set("gridColumns", p.layout.gridColumns);
    set("gridRows", p.layout.gridRows);
    set("colSpan", p.layoutChild.colSpan);
    set("rowSpan", p.layoutChild.rowSpan);
    set("parentLayoutMode", n.parent ? enumName(d.at(n.parent).p.layout.mode, kLayoutModes) : QString());
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
        QVariantMap features;
        for (auto it = t.features.begin(); it != t.features.end(); ++it)
            features.insert(it.key(), it.value());
        set("features", features);
        QVariantMap axesMap;
        for (auto it = t.axes.begin(); it != t.axes.end(); ++it)
            axesMap.insert(it.key(), it.value());
        set("axes", axesMap);
        const int paras = int(p.text.count(QLatin1Char('\n'))) + 1;
        ListType lt = p.paragraphs.empty() ? ListType::None : p.paragraphs[0].list;
        bool mixedList = false;
        for (int i = 1; i < paras; ++i)
            mixedList |= (i < int(p.paragraphs.size()) ? p.paragraphs[i].list : ListType::None) != lt;
        set("listType", mixedList ? QVariant() : enumName(lt, kLists));
        bool runs = false;
        for (const TextRun& r : p.textRuns)
            runs |= (r.mask & ~RunLink) != 0;
        set("hasRuns", runs);
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
    set("grids", gridsToList(p.grids));
    QVariantList exports;
    for (const ExportSetting& e : p.exports)
        exports << QVariantMap{{QStringLiteral("format"), e.format}, {QStringLiteral("scale"), e.scale},
            {QStringLiteral("suffix"), e.suffix}};
    set("exports", exports);

    // Styles and variables
    set("fillStyle", p.fillStyle);
    set("strokeStyle", p.strokeStyle);
    set("textStyleId", p.textStyleId);
    set("effectStyle", p.effectStyle);
    set("gridStyle", p.gridStyle);
    QVariantMap bindings;
    for (auto it = p.bindings.begin(); it != p.bindings.end(); ++it)
        bindings.insert(it.key(), it.value());
    set("bindings", bindings);
    QVariantMap modes;
    for (auto it = p.variableModes.begin(); it != p.variableModes.end(); ++it)
        modes.insert(it.key(), it.value());
    set("variableModes", modes);

    // Components
    set("isComponent", p.type == NodeType::Component);
    set("isComponentSet", p.type == NodeType::ComponentSet);
    set("isInstance", p.type == NodeType::Instance);
    set("insideInstance", d.isInsideInstance(id));
    set("description", p.description);
    set("overrideCount", 0);
    // Component properties
    static const char* const kPropTypes[] = {"BOOLEAN", "TEXT", "INSTANCE_SWAP"};
    auto defToMap = [&](const ComponentProp& c) {
        QVariantList preferred;
        for (auto x : c.preferred)
            preferred << QString::number(x);
        return QVariantMap{{QStringLiteral("id"), c.id}, {QStringLiteral("name"), c.name},
            {QStringLiteral("type"), QString::fromLatin1(kPropTypes[int(c.type)])},
            {QStringLiteral("default"), c.defaultValue}, {QStringLiteral("preferred"), preferred}};
    };
    auto swapOptions = [&](const ComponentProp& c) {
        QVariantList opts;
        std::vector<NodeId> ids(c.preferred.begin(), c.preferred.end());
        for (NodeId comp : d.allComponents())
            if (std::find(ids.begin(), ids.end(), comp) == ids.end())
                ids.push_back(comp);
        for (NodeId comp : ids) {
            if (!d.isAttached(comp))
                continue;
            const Node& cn = d.at(comp);
            QString name = cn.p.name;
            if (d.at(cn.parent).type() == NodeType::ComponentSet)
                name = d.at(cn.parent).p.name + QStringLiteral(" / ") + name;
            opts << QVariantMap{{QStringLiteral("value"), QString::number(comp)}, {QStringLiteral("text"), name}};
        }
        return opts;
    };
    if (p.type == NodeType::Component || p.type == NodeType::ComponentSet) {
        QVariantList defs;
        for (const ComponentProp& c : p.componentProps)
            defs << defToMap(c);
        set("componentProps", defs);
    }
    if (!d.isInsideInstance(id) && p.type != NodeType::ComponentSet && d.componentOf(id) && d.componentOf(id) != id) {
        QVariantList linkable;
        for (const ComponentProp* c : d.propDefs(d.componentOf(id)))
            linkable << defToMap(*c);
        set("linkableProps", linkable);
        QVariantMap refs;
        for (auto it = p.propRefs.begin(); it != p.propRefs.end(); ++it)
            refs.insert(it.key(), it.value());
        set("propRefs", refs);
    }
    if (p.type == NodeType::Instance) {
        QVariantList values;
        for (const ComponentProp* c : d.propDefs(id)) {
            QVariantMap m = defToMap(*c);
            m.insert(QStringLiteral("value"), p.propValues.value(c->id, c->defaultValue));
            if (c->type == ComponentPropType::InstanceSwap)
                m.insert(QStringLiteral("options"), swapOptions(*c));
            values << m;
        }
        set("instanceProps", values);
    }
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
    if (textEditing() && sel.size() == 1)
        textRangeProps(first);
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
    if (textEditing() && applyTextRange(key, value))
        return;
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
    // A direct edit replaces whatever drove the field.
    {
        QStringList unbind;
        if (vars::fieldType(k, nullptr))
            unbind << k;
        if (k == QLatin1String("cornerRadius") || k == QLatin1String("corners"))
            unbind << QStringLiteral("cornerRadius") << QStringLiteral("tl") << QStringLiteral("tr")
                   << QStringLiteral("br") << QStringLiteral("bl");
        if (k == QLatin1String("padH"))
            unbind << QStringLiteral("padL") << QStringLiteral("padR");
        if (k == QLatin1String("padV"))
            unbind << QStringLiteral("padT") << QStringLiteral("padB");
        for (const QString& b : unbind)
            if (cur.bindings.contains(b))
                e().bindings.remove(b);
        static const QStringList typo = {QStringLiteral("fontFamily"), QStringLiteral("fontWeight"),
            QStringLiteral("italic"), QStringLiteral("fontSize"), QStringLiteral("lineHeight"),
            QStringLiteral("lineHeightPercent"), QStringLiteral("letterSpacing"), QStringLiteral("paragraphSpacing"),
            QStringLiteral("textCase"), QStringLiteral("decoration")};
        if (!cur.textStyleId.isEmpty() && typo.contains(k))
            e().textStyleId.clear();
        // Setting a character property on the whole layer replaces per-character values.
        if (cur.type == NodeType::Text && runField(k) && k != QLatin1String("link")) {
            bool overridden = false;
            for (const TextRun& r : cur.textRuns)
                overridden |= (r.mask & runField(k)) != 0;
            if (overridden)
                text::clearRange(e(), 0, int(cur.text.size()), runField(k));
        }
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
    } else if (k == QLatin1String("fills") || k == QLatin1String("strokes")) {
        // Editing a styled list detaches the style; changing a bound color detaches the variable.
        const bool fills = k == QLatin1String("fills");
        std::vector<Paint> list = paintsFromList(v);
        const std::vector<Paint>& old = fills ? cur.fills : cur.strokes;
        for (size_t i = 0; i < list.size() && i < old.size(); ++i)
            if (!list[i].colorVar.isEmpty() && list[i].colorVar == old[i].colorVar
                && (list[i].color != old[i].color || list[i].type != PaintType::Solid))
                list[i].colorVar.clear();
        Props& p = e();
        if (list != old)
            (fills ? p.fillStyle : p.strokeStyle).clear();
        (fills ? p.fills : p.strokes) = list;
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
        std::vector<Effect> fx = effectsFromList(v);
        if (fx != cur.effects)
            e().effectStyle.clear();
        for (size_t i = 0; i < fx.size() && i < cur.effects.size(); ++i)
            if (!fx[i].colorVar.isEmpty() && fx[i].colorVar == cur.effects[i].colorVar && fx[i].color != cur.effects[i].color)
                fx[i].colorVar.clear();
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
    } else if (k == QLatin1String("gridColumns")) {
        e().layout.gridColumns = std::clamp(v.toInt(), 1, 64);
    } else if (k == QLatin1String("gridRows")) {
        e().layout.gridRows = std::clamp(v.toInt(), 0, 64);
    } else if (k == QLatin1String("colSpan")) {
        e().layoutChild.colSpan = std::clamp(v.toInt(), 1, 64);
    } else if (k == QLatin1String("rowSpan")) {
        e().layoutChild.rowSpan = std::clamp(v.toInt(), 1, 64);
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
        Props& p = e();
        text::splice(p, 0, int(p.text.size()), v.toString());
    } else if (cur.type == NodeType::Text && k == QLatin1String("listType")) {
        Props& p = e();
        ParagraphAttr a;
        a.list = enumValue(v, kLists, ListType::None);
        text::setParagraphs(p, 0, int(p.text.count(QLatin1Char('\n'))), a);
    } else if (cur.type == NodeType::Text && k == QLatin1String("fontFeature")) {
        const QVariantMap m = v.toMap();
        const QString tag = m.value(QStringLiteral("tag")).toString();
        if (tag.size() == 4) {
            Props& p = e();
            if (m.value(QStringLiteral("value")).isNull())
                p.textStyle.features.remove(tag);
            else
                p.textStyle.features.insert(tag, m.value(QStringLiteral("value")).toInt());
            p.textStyleId.clear();
        }
    } else if (cur.type == NodeType::Text && k == QLatin1String("fontAxis")) {
        const QVariantMap m = v.toMap();
        const QString tag = m.value(QStringLiteral("tag")).toString();
        if (tag.size() == 4 && tag != QLatin1String("wght")) {
            Props& p = e();
            if (m.value(QStringLiteral("value")).isNull())
                p.textStyle.axes.remove(tag);
            else
                p.textStyle.axes.insert(tag, m.value(QStringLiteral("value")).toFloat());
            p.textStyleId.clear();
        }
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
        std::vector<LayoutGrid> grids = gridsFromList(v);
        if (grids != cur.grids)
            e().gridStyle.clear();
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

bool Editor::applyTextRange(const QString& key, const QVariant& v)
{
    auto* tool = static_cast<TextEditTool*>(m_tool);
    const NodeId id = tool->node();
    if (!m_doc->isAttached(id))
        return false;
    const int a = tool->selStart(), b = tool->selEnd();
    const Props& cur = m_doc->at(id).p;
    if (key == QLatin1String("listType")) {
        auto [first, last] = text::paragraphRange(cur.text, a, b);
        m_doc->breakMerge();
        m_doc->begin(tr("List"));
        ParagraphAttr attr;
        attr.list = enumValue(v, kLists, ListType::None);
        text::setParagraphs(m_doc->edit(id), first, last, attr);
        m_doc->commit();
        return true;
    }
    const std::uint32_t field = runField(key);
    if (!field || a == b)
        return false; // caret only: the layer-wide edit applies
    TextRun val;
    switch (field) {
    case RunFamily: val.family = v.toString(); break;
    case RunWeight: val.weight = std::clamp(v.toInt(), 100, 1000); break;
    case RunItalic: val.italic = v.toBool(); break;
    case RunSize: val.size = std::clamp(v.toFloat(), 1.f, 2000.f); break;
    case RunLetterSpacing: val.letterSpacing = v.toFloat(); break;
    case RunDecoration: val.decoration = enumValue(v, kDeco, TextDecoration::None); break;
    case RunCase: val.textCase = enumValue(v, kCase, TextCase::Original); break;
    case RunLink: val.link = v.toString().trimmed(); break;
    case RunFill: {
        const auto paints = paintsFromList(v);
        auto it = std::find_if(paints.begin(), paints.end(), [](const Paint& p) { return p.visible && p.type == PaintType::Solid; });
        if (it == paints.end())
            return true;
        val.fill = it->color;
        val.fill.fA *= it->opacity;
        break;
    }
    default: return false;
    }
    if (!m_live)
        m_doc->breakMerge();
    m_doc->begin(tr("Style text"));
    if (m_live)
        m_doc->setMergeKey(QStringLiteral("live:") + m_liveKey);
    if (field == RunLink && val.link.isEmpty())
        text::clearRange(m_doc->edit(id), a, b, RunLink);
    else
        text::styleRange(m_doc->edit(id), a, b, field, val);
    m_doc->commit();
    return true;
}

void Editor::textRangeProps(QVariantMap& out) const
{
    auto* tool = static_cast<TextEditTool*>(m_tool);
    const NodeId id = tool->node();
    if (!m_doc->isAttached(id))
        return;
    const Props& p = m_doc->at(id).p;
    const int a = tool->selStart(), b = tool->selEnd();
    out.insert(QStringLiteral("textRange"), QVariantList{a, b});
    auto [first, last] = text::paragraphRange(p.text, a, b);
    auto listAt = [&](int i) { return i < int(p.paragraphs.size()) ? p.paragraphs[i].list : ListType::None; };
    bool mixedList = false;
    for (int i = first + 1; i <= last; ++i)
        mixedList |= listAt(i) != listAt(first);
    out.insert(QStringLiteral("listType"), mixedList ? QVariant() : enumName(listAt(first), kLists));
    if (a == b)
        return;
    // Effective character style over the range; differing values show as mixed.
    TextRun s0 = text::styleAt(p, a);
    std::uint32_t mixed = 0;
    for (int i = a + 1; i < b; ++i) {
        TextRun s = text::styleAt(p, i);
        if (s.family != s0.family) mixed |= RunFamily;
        if (s.weight != s0.weight) mixed |= RunWeight;
        if (s.italic != s0.italic) mixed |= RunItalic;
        if (s.size != s0.size) mixed |= RunSize;
        if (s.letterSpacing != s0.letterSpacing) mixed |= RunLetterSpacing;
        if (s.decoration != s0.decoration) mixed |= RunDecoration;
        if (s.textCase != s0.textCase) mixed |= RunCase;
        if (s.link != s0.link) mixed |= RunLink;
    }
    auto put = [&](const char* k, std::uint32_t f, const QVariant& v) { out.insert(QLatin1String(k), mixed & f ? QVariant() : v); };
    put("fontFamily", RunFamily, s0.family);
    put("fontWeight", RunWeight, s0.weight);
    put("italic", RunItalic, s0.italic);
    put("fontSize", RunSize, s0.size);
    put("letterSpacing", RunLetterSpacing, s0.letterSpacing);
    put("decoration", RunDecoration, enumName(s0.decoration, kDeco));
    put("textCase", RunCase, enumName(s0.textCase, kCase));
    put("link", RunLink, s0.link);
}

QVariantList Editor::fontAxes(const QString& family, int weight, bool italic) const
{
    QVariantList out;
    for (const text::Axis& a : text::axes(family, weight, italic))
        out << QVariantMap{{QStringLiteral("tag"), a.tag}, {QStringLiteral("min"), a.min}, {QStringLiteral("max"), a.max},
            {QStringLiteral("default"), a.def}};
    return out;
}

QStringList Editor::missingFonts() const
{
    QSet<QString> used;
    m_doc->walk(m_doc->root(), [&](const Node& n) {
        if (n.type() == NodeType::Text) {
            used.insert(n.p.textStyle.family);
            for (const TextRun& r : n.p.textRuns)
                if (r.mask & RunFamily)
                    used.insert(r.family);
        }
        return true;
    });
    for (const Style& st : m_doc->library().styles)
        if (st.type == StyleType::Text)
            used.insert(st.text.family);
    QStringList out;
    for (const QString& f : used)
        if (!text::hasFamily(f))
            out << f;
    out.sort();
    return out;
}

void Editor::replaceFont(const QString& from, const QString& to)
{
    if (from == to || to.isEmpty())
        return;
    std::vector<NodeId> ids;
    m_doc->walk(m_doc->root(), [&](const Node& n) {
        if (n.type() == NodeType::Text)
            ids.push_back(n.id);
        return true;
    });
    m_doc->breakMerge();
    m_doc->begin(tr("Replace font"));
    for (NodeId id : ids) {
        Props p = m_doc->at(id).p;
        if (p.textStyle.family == from)
            p.textStyle.family = to;
        for (TextRun& r : p.textRuns)
            if ((r.mask & RunFamily) && r.family == from)
                r.family = to;
        if (!(p == m_doc->at(id).p))
            m_doc->edit(id) = p;
    }
    bool styles = false;
    for (const Style& st : m_doc->library().styles)
        styles |= st.type == StyleType::Text && st.text.family == from;
    if (styles)
        for (Style& st : m_doc->editLibrary().styles)
            if (st.type == StyleType::Text && st.text.family == from)
                st.text.family = to;
    m_doc->commit();
}

} // namespace drav
