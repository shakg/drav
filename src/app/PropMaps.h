#pragma once

// Conversions between core types and the QVariant maps the QML inspector works with.

#include "core/Types.h"

#include <QColor>
#include <QVariant>

#include <algorithm>

namespace drav::maps {

inline QColor toQColor(SkColor4f c) { return QColor::fromRgbF(c.fR, c.fG, c.fB, c.fA); }
inline SkColor4f toSk(const QVariant& v)
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

inline const char* const kPaintTypes[] = {"SOLID", "GRADIENT_LINEAR", "GRADIENT_RADIAL", "GRADIENT_ANGULAR", "GRADIENT_DIAMOND", "IMAGE"};
inline const char* const kScaleModes[] = {"FILL", "FIT", "CROP", "TILE"};
inline const char* const kBlends[] = {"PASS_THROUGH", "NORMAL", "DARKEN", "MULTIPLY", "COLOR_BURN", "LIGHTEN", "SCREEN",
    "COLOR_DODGE", "OVERLAY", "SOFT_LIGHT", "HARD_LIGHT", "DIFFERENCE", "EXCLUSION", "HUE", "SATURATION", "COLOR",
    "LUMINOSITY"};
inline const char* const kAligns[] = {"INSIDE", "CENTER", "OUTSIDE"};
inline const char* const kCaps[] = {"NONE", "ROUND", "SQUARE", "LINE_ARROW", "TRIANGLE_ARROW", "CIRCLE_FILLED", "DIAMOND_FILLED"};
inline const char* const kJoins[] = {"MITER", "BEVEL", "ROUND"};
inline const char* const kEffects[] = {"DROP_SHADOW", "INNER_SHADOW", "LAYER_BLUR", "BACKGROUND_BLUR"};
inline const char* const kConsH[] = {"LEFT", "RIGHT", "LEFT_RIGHT", "CENTER", "SCALE"};
inline const char* const kConsV[] = {"TOP", "BOTTOM", "TOP_BOTTOM", "CENTER", "SCALE"};
inline const char* const kLayoutModes[] = {"NONE", "HORIZONTAL", "VERTICAL"};
inline const char* const kLayoutAligns[] = {"MIN", "CENTER", "MAX", "SPACE_BETWEEN", "BASELINE"};
inline const char* const kSizing[] = {"FIXED", "HUG", "FILL"};
inline const char* const kTextH[] = {"LEFT", "CENTER", "RIGHT", "JUSTIFIED"};
inline const char* const kTextV[] = {"TOP", "CENTER", "BOTTOM"};
inline const char* const kAutoResize[] = {"NONE", "HEIGHT", "WIDTH_AND_HEIGHT", "TRUNCATE"};
inline const char* const kCase[] = {"ORIGINAL", "UPPER", "LOWER", "TITLE"};
inline const char* const kDeco[] = {"NONE", "UNDERLINE", "STRIKETHROUGH"};
inline const char* const kBool[] = {"UNION", "SUBTRACT", "INTERSECT", "EXCLUDE"};
inline const char* const kGridKinds[] = {"GRID", "COLUMNS", "ROWS"};

inline QVariantMap paintToMap(const Paint& p)
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
        {QStringLiteral("colorVar"), p.colorVar},
        {QStringLiteral("exposure"), p.exposure},
        {QStringLiteral("contrast"), p.contrast},
        {QStringLiteral("saturation"), p.saturation},
        {QStringLiteral("temperature"), p.temperature},
        {QStringLiteral("tint"), p.tint},
        {QStringLiteral("highlights"), p.highlights},
        {QStringLiteral("shadows"), p.shadows},
        {QStringLiteral("crop"), QVariantList{p.crop.left(), p.crop.top(), p.crop.width(), p.crop.height()}},
    };
}

inline Paint paintFromMap(const QVariantMap& m)
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
    p.colorVar = m.value(QStringLiteral("colorVar")).toString();
    p.exposure = m.value(QStringLiteral("exposure"), 0).toFloat();
    p.contrast = m.value(QStringLiteral("contrast"), 0).toFloat();
    p.saturation = m.value(QStringLiteral("saturation"), 0).toFloat();
    p.temperature = m.value(QStringLiteral("temperature"), 0).toFloat();
    p.tint = m.value(QStringLiteral("tint"), 0).toFloat();
    p.highlights = m.value(QStringLiteral("highlights"), 0).toFloat();
    p.shadows = m.value(QStringLiteral("shadows"), 0).toFloat();
    const QVariantList crop = m.value(QStringLiteral("crop")).toList();
    if (crop.size() == 4)
        p.crop = SkRect::MakeXYWH(crop[0].toFloat(), crop[1].toFloat(), crop[2].toFloat(), crop[3].toFloat());
    return p;
}

inline QVariantList paintsToList(const std::vector<Paint>& v)
{
    QVariantList l;
    for (const Paint& p : v)
        l << paintToMap(p);
    return l;
}

inline std::vector<Paint> paintsFromList(const QVariant& v)
{
    std::vector<Paint> out;
    for (const QVariant& p : v.toList())
        out.push_back(paintFromMap(p.toMap()));
    return out;
}

inline QVariantMap effectToMap(const Effect& e)
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
        {QStringLiteral("colorVar"), e.colorVar},
    };
}

inline Effect effectFromMap(const QVariantMap& m)
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
    e.colorVar = m.value(QStringLiteral("colorVar")).toString();
    return e;
}

inline QVariantList effectsToList(const std::vector<Effect>& v)
{
    QVariantList l;
    for (const Effect& e : v)
        l << effectToMap(e);
    return l;
}

inline std::vector<Effect> effectsFromList(const QVariant& v)
{
    std::vector<Effect> out;
    for (const QVariant& x : v.toList())
        out.push_back(effectFromMap(x.toMap()));
    return out;
}

inline QVariantMap gridToMap(const LayoutGrid& g)
{
    return {{QStringLiteral("kind"), enumName(g.kind, kGridKinds)}, {QStringLiteral("count"), g.count},
        {QStringLiteral("size"), g.size}, {QStringLiteral("gutter"), g.gutter}, {QStringLiteral("margin"), g.margin},
        {QStringLiteral("color"), toQColor(g.color)}, {QStringLiteral("visible"), g.visible}};
}

inline LayoutGrid gridFromMap(const QVariantMap& gm)
{
    LayoutGrid g;
    g.kind = enumValue(gm.value(QStringLiteral("kind")), kGridKinds, LayoutGrid::Kind::Columns);
    g.count = gm.value(QStringLiteral("count"), 12).toInt();
    g.size = gm.value(QStringLiteral("size"), 10).toFloat();
    g.gutter = gm.value(QStringLiteral("gutter"), 20).toFloat();
    g.margin = gm.value(QStringLiteral("margin"), 0).toFloat();
    if (gm.contains(QStringLiteral("color")))
        g.color = toSk(gm.value(QStringLiteral("color")));
    g.visible = gm.value(QStringLiteral("visible"), true).toBool();
    return g;
}

inline QVariantList gridsToList(const std::vector<LayoutGrid>& v)
{
    QVariantList l;
    for (const LayoutGrid& g : v)
        l << gridToMap(g);
    return l;
}

inline std::vector<LayoutGrid> gridsFromList(const QVariant& v)
{
    std::vector<LayoutGrid> out;
    for (const QVariant& x : v.toList())
        out.push_back(gridFromMap(x.toMap()));
    return out;
}

// Typography only (what a text style carries). Keys match the inspector's text properties.
inline QVariantMap typographyToMap(const TextStyle& t)
{
    return {{QStringLiteral("fontFamily"), t.family}, {QStringLiteral("fontWeight"), t.weight},
        {QStringLiteral("italic"), t.italic}, {QStringLiteral("fontSize"), t.size},
        {QStringLiteral("lineHeight"), t.lineHeight}, {QStringLiteral("lineHeightPercent"), t.lineHeightPercent},
        {QStringLiteral("letterSpacing"), t.letterSpacing}, {QStringLiteral("paragraphSpacing"), t.paragraphSpacing},
        {QStringLiteral("textCase"), enumName(t.textCase, kCase)},
        {QStringLiteral("decoration"), enumName(t.decoration, kDeco)}};
}

// Applies the keys present in `m` on top of `t`.
inline void typographyFromMap(TextStyle& t, const QVariantMap& m)
{
    auto has = [&](const char* k) { return m.contains(QLatin1String(k)); };
    auto val = [&](const char* k) { return m.value(QLatin1String(k)); };
    if (has("fontFamily")) t.family = val("fontFamily").toString();
    if (has("fontWeight")) t.weight = std::clamp(val("fontWeight").toInt(), 100, 1000);
    if (has("italic")) t.italic = val("italic").toBool();
    if (has("fontSize")) t.size = std::clamp(val("fontSize").toFloat(), 1.f, 2000.f);
    if (has("lineHeight")) t.lineHeight = std::max(0.f, val("lineHeight").toFloat());
    if (has("lineHeightPercent")) t.lineHeightPercent = val("lineHeightPercent").toBool();
    if (has("letterSpacing")) t.letterSpacing = val("letterSpacing").toFloat();
    if (has("paragraphSpacing")) t.paragraphSpacing = std::max(0.f, val("paragraphSpacing").toFloat());
    if (has("textCase")) t.textCase = enumValue(val("textCase"), kCase, TextCase::Original);
    if (has("decoration")) t.decoration = enumValue(val("decoration"), kDeco, TextDecoration::None);
}

} // namespace drav::maps
