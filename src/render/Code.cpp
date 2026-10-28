#include "Code.h"

#include "core/Library.h"

#include <cmath>

namespace drav::code {

namespace {

QString num(float v)
{
    const float r = std::round(v * 100) / 100;
    return std::fabs(r - std::round(r)) < 0.001f ? QString::number(int(std::round(r))) : QString::number(r, 'f', 2);
}

QString px(float v) { return v == 0 ? QStringLiteral("0") : num(v) + QStringLiteral("px"); }

QString color(SkColor4f c, float opacity = 1)
{
    const float a = c.fA * opacity;
    auto ch = [](float v) { return int(std::round(std::clamp(v, 0.f, 1.f) * 255)); };
    if (a >= 0.999f)
        return QStringLiteral("#%1%2%3")
            .arg(ch(c.fR), 2, 16, QLatin1Char('0'))
            .arg(ch(c.fG), 2, 16, QLatin1Char('0'))
            .arg(ch(c.fB), 2, 16, QLatin1Char('0'))
            .toUpper();
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(ch(c.fR)).arg(ch(c.fG)).arg(ch(c.fB)).arg(num(a));
}

QString blendName(BlendMode m)
{
    static const char* const names[] = {"normal", "normal", "darken", "multiply", "color-burn", "lighten", "screen",
        "color-dodge", "overlay", "soft-light", "hard-light", "difference", "exclusion", "hue", "saturation", "color",
        "luminosity"};
    return QString::fromLatin1(names[int(m)]);
}

} // namespace

QString cssVarName(const QString& name)
{
    QString s = name.toLower();
    for (QChar& c : s)
        if (!c.isLetterOrNumber())
            c = QLatin1Char('-');
    while (s.contains(QLatin1String("--")))
        s.replace(QLatin1String("--"), QLatin1String("-"));
    if (s.startsWith(QLatin1Char('-')))
        s.remove(0, 1);
    if (s.endsWith(QLatin1Char('-')))
        s.chop(1);
    return QStringLiteral("--") + s;
}

QString css(const Document& doc, NodeId id)
{
    const Node& n = doc.at(id);
    const Props& p = n.p;
    const Library& lib = doc.library();
    QStringList out;
    auto add = [&](const QString& k, const QString& v) { out << k + QStringLiteral(": ") + v + QLatin1Char(';'); };
    auto bound = [&](const QString& varId, const QString& literal) {
        const Variable* v = lib.variable(varId);
        return v ? QStringLiteral("var(%1, %2)").arg(cssVarName(v->name), literal) : literal;
    };
    auto boundField = [&](const char* field, const QString& literal) {
        return bound(p.bindings.value(QLatin1String(field)), literal);
    };

    const Node* parent = n.parent ? &doc.at(n.parent) : nullptr;
    const bool inFlex = parent && parent->p.layout.mode != LayoutMode::None && !p.layoutChild.absolute;
    const bool isText = p.type == NodeType::Text;

    // Box
    const bool hugW = p.layoutChild.sizingH == LayoutSizing::Hug || (isText && p.textStyle.autoResize == TextAutoResize::WidthAndHeight);
    const bool hugH = p.layoutChild.sizingV == LayoutSizing::Hug
        || (isText && p.textStyle.autoResize != TextAutoResize::None && p.textStyle.autoResize != TextAutoResize::Truncate);
    if (!hugW && p.layoutChild.sizingH != LayoutSizing::Fill)
        add(QStringLiteral("width"), boundField("w", px(p.width)));
    if (!hugH && p.layoutChild.sizingV != LayoutSizing::Fill)
        add(QStringLiteral("height"), boundField("h", px(p.height)));
    if (inFlex && parent->p.layout.mode == LayoutMode::Grid) {
        if (p.layoutChild.colSpan > 1)
            add(QStringLiteral("grid-column"), QStringLiteral("span %1").arg(p.layoutChild.colSpan));
        if (p.layoutChild.rowSpan > 1)
            add(QStringLiteral("grid-row"), QStringLiteral("span %1").arg(p.layoutChild.rowSpan));
    } else if (inFlex) {
        const bool horiz = parent->p.layout.mode == LayoutMode::Horizontal;
        const LayoutSizing primary = horiz ? p.layoutChild.sizingH : p.layoutChild.sizingV;
        const LayoutSizing counter = horiz ? p.layoutChild.sizingV : p.layoutChild.sizingH;
        if (primary == LayoutSizing::Fill)
            add(QStringLiteral("flex"), QStringLiteral("1 0 0"));
        if (counter == LayoutSizing::Fill)
            add(QStringLiteral("align-self"), QStringLiteral("stretch"));
    } else if (parent && parent->type() != NodeType::Page) {
        add(QStringLiteral("position"), QStringLiteral("absolute"));
        add(QStringLiteral("left"), px(p.transform.getTranslateX()));
        add(QStringLiteral("top"), px(p.transform.getTranslateY()));
    }
    const float rot = -std::atan2(p.transform.getSkewY(), p.transform.getScaleX()) * 180.f / 3.14159265f;
    if (std::fabs(rot) > 0.01f)
        add(QStringLiteral("transform"), QStringLiteral("rotate(%1deg)").arg(num(-rot)));
    if (p.layoutChild.minW > 0) add(QStringLiteral("min-width"), px(p.layoutChild.minW));
    if (p.layoutChild.maxW > 0) add(QStringLiteral("max-width"), px(p.layoutChild.maxW));
    if (p.layoutChild.minH > 0) add(QStringLiteral("min-height"), px(p.layoutChild.minH));
    if (p.layoutChild.maxH > 0) add(QStringLiteral("max-height"), px(p.layoutChild.maxH));

    // Auto layout
    if (p.layout.mode == LayoutMode::Grid) {
        const AutoLayout& l = p.layout;
        add(QStringLiteral("display"), QStringLiteral("grid"));
        add(QStringLiteral("grid-template-columns"), QStringLiteral("repeat(%1, %2)").arg(l.gridColumns).arg(p.layoutChild.sizingH == LayoutSizing::Hug ? QStringLiteral("auto") : QStringLiteral("1fr")));
        if (l.gridRows > 0)
            add(QStringLiteral("grid-template-rows"), QStringLiteral("repeat(%1, %2)").arg(l.gridRows).arg(p.layoutChild.sizingV == LayoutSizing::Hug ? QStringLiteral("auto") : QStringLiteral("1fr")));
        if (l.spacing || l.counterSpacing)
            add(QStringLiteral("gap"), px(l.counterSpacing) + QLatin1Char(' ') + px(l.spacing));
        if (l.padL || l.padT || l.padR || l.padB)
            add(QStringLiteral("padding"), QStringList{px(l.padT), px(l.padR), px(l.padB), px(l.padL)}.join(QLatin1Char(' ')));
    } else if (p.layout.mode != LayoutMode::None) {
        const AutoLayout& l = p.layout;
        add(QStringLiteral("display"), QStringLiteral("flex"));
        if (l.mode == LayoutMode::Vertical)
            add(QStringLiteral("flex-direction"), QStringLiteral("column"));
        if (l.wrap)
            add(QStringLiteral("flex-wrap"), QStringLiteral("wrap"));
        auto align = [](LayoutAlign a) {
            switch (a) {
            case LayoutAlign::Center: return QStringLiteral("center");
            case LayoutAlign::Max: return QStringLiteral("flex-end");
            case LayoutAlign::SpaceBetween: return QStringLiteral("space-between");
            case LayoutAlign::Baseline: return QStringLiteral("baseline");
            default: return QStringLiteral("flex-start");
            }
        };
        if (l.primaryAlign != LayoutAlign::Min)
            add(QStringLiteral("justify-content"), align(l.primaryAlign));
        if (l.counterAlign != LayoutAlign::Min)
            add(QStringLiteral("align-items"), align(l.counterAlign));
        if (l.primaryAlign != LayoutAlign::SpaceBetween && l.spacing)
            add(l.wrap ? QStringLiteral("column-gap") : QStringLiteral("gap"), boundField("spacing", px(l.spacing)));
        if (l.wrap && l.counterSpacing)
            add(QStringLiteral("row-gap"), boundField("counterSpacing", px(l.counterSpacing)));
        if (l.padL || l.padT || l.padR || l.padB) {
            if (l.padL == l.padR && l.padT == l.padB && l.padL == l.padT)
                add(QStringLiteral("padding"), px(l.padT));
            else if (l.padL == l.padR && l.padT == l.padB)
                add(QStringLiteral("padding"), px(l.padT) + QLatin1Char(' ') + px(l.padL));
            else
                add(QStringLiteral("padding"), QStringList{px(l.padT), px(l.padR), px(l.padB), px(l.padL)}.join(QLatin1Char(' ')));
        }
    }
    if (isFrameLike(p.type) && p.clipsContent)
        add(QStringLiteral("overflow"), QStringLiteral("hidden"));

    // Fills (CSS backgrounds list the top layer first)
    QStringList backgrounds;
    QString textColor;
    for (auto it = p.fills.rbegin(); it != p.fills.rend(); ++it) {
        const Paint& f = *it;
        if (!f.visible)
            continue;
        if (f.type == PaintType::Solid) {
            const QString c = bound(f.colorVar, color(f.color, f.opacity));
            if (isText && textColor.isEmpty())
                textColor = c;
            else
                backgrounds << (backgrounds.isEmpty() && p.fills.size() == 1 ? c : QStringLiteral("linear-gradient(%1, %1)").arg(c));
        } else if (f.type == PaintType::Image) {
            backgrounds << QStringLiteral("url(%1.png) center / %2 no-repeat")
                               .arg(f.imageHash.left(8), f.scaleMode == ImageScaleMode::Fit ? QStringLiteral("contain") : QStringLiteral("cover"));
        } else {
            QStringList stops;
            for (const ColorStop& s : f.stops)
                stops << color(s.color, f.opacity) + QLatin1Char(' ') + num(s.pos * 100) + QLatin1Char('%');
            if (f.type == PaintType::LinearGradient) {
                const float dx = (f.gradEnd.x() - f.gradStart.x()) * p.width, dy = (f.gradEnd.y() - f.gradStart.y()) * p.height;
                const float deg = std::atan2(dx, -dy) * 180.f / 3.14159265f;
                backgrounds << QStringLiteral("linear-gradient(%1deg, %2)").arg(num(deg), stops.join(QStringLiteral(", ")));
            } else if (f.type == PaintType::AngularGradient) {
                backgrounds << QStringLiteral("conic-gradient(from 90deg at %1% %2%, %3)")
                                   .arg(num(f.gradStart.x() * 100), num(f.gradStart.y() * 100), stops.join(QStringLiteral(", ")));
            } else {
                backgrounds << QStringLiteral("radial-gradient(circle at %1% %2%, %3)")
                                   .arg(num(f.gradStart.x() * 100), num(f.gradStart.y() * 100), stops.join(QStringLiteral(", ")));
            }
        }
        if (f.blend != BlendMode::Normal)
            add(QStringLiteral("background-blend-mode"), blendName(f.blend));
    }
    if (!backgrounds.isEmpty())
        add(QStringLiteral("background"), backgrounds.join(QStringLiteral(", ")));

    // Corners
    if (p.type != NodeType::Text && p.type != NodeType::Line) {
        const CornerRadii& r = p.corners;
        if (p.type == NodeType::Ellipse)
            add(QStringLiteral("border-radius"), QStringLiteral("50%"));
        else if (r.uniform() && r.tl > 0)
            add(QStringLiteral("border-radius"), boundField("cornerRadius", px(r.tl)));
        else if (!r.uniform())
            add(QStringLiteral("border-radius"), QStringList{px(r.tl), px(r.tr), px(r.br), px(r.bl)}.join(QLatin1Char(' ')));
    }

    // Strokes: inside -> border (box-sizing), center/outside -> outline.
    for (const Paint& s : p.strokes) {
        if (!s.visible || s.type != PaintType::Solid || p.stroke.weight <= 0)
            continue;
        const QString style = p.stroke.dashes.empty() ? QStringLiteral("solid") : QStringLiteral("dashed");
        const QString v = QStringLiteral("%1 %2 %3").arg(boundField("strokeWeight", px(p.stroke.weight)), style, bound(s.colorVar, color(s.color, s.opacity)));
        if (p.stroke.align == StrokeAlign::Inside) {
            add(QStringLiteral("box-sizing"), QStringLiteral("border-box"));
            add(QStringLiteral("border"), v);
        } else {
            add(QStringLiteral("outline"), v);
            add(QStringLiteral("outline-offset"), p.stroke.align == StrokeAlign::Center ? px(-p.stroke.weight / 2) : QStringLiteral("0"));
        }
        break;
    }

    // Effects
    QStringList shadows;
    for (const Effect& e : p.effects) {
        if (!e.visible)
            continue;
        const QString c = bound(e.colorVar, color(e.color));
        switch (e.type) {
        case EffectType::DropShadow:
            shadows << (isText ? QStringLiteral("%1 %2 %3 %4") : QStringLiteral("%1 %2 %3 %5 %4"))
                           .arg(px(e.offset.x()), px(e.offset.y()), px(e.radius), c, px(e.spread));
            break;
        case EffectType::InnerShadow:
            if (!isText)
                shadows << QStringLiteral("inset %1 %2 %3 %4 %5").arg(px(e.offset.x()), px(e.offset.y()), px(e.radius), px(e.spread), c);
            break;
        case EffectType::LayerBlur: add(QStringLiteral("filter"), QStringLiteral("blur(%1)").arg(px(e.radius / 2))); break;
        case EffectType::BackgroundBlur: add(QStringLiteral("backdrop-filter"), QStringLiteral("blur(%1)").arg(px(e.radius / 2))); break;
        }
    }
    if (!shadows.isEmpty())
        add(isText ? QStringLiteral("text-shadow") : QStringLiteral("box-shadow"), shadows.join(QStringLiteral(", ")));

    if (p.opacity < 1)
        add(QStringLiteral("opacity"), boundField("opacity", num(p.opacity)));
    if (p.blend != BlendMode::PassThrough && p.blend != BlendMode::Normal)
        add(QStringLiteral("mix-blend-mode"), blendName(p.blend));

    // Typography
    if (isText) {
        const TextStyle& t = p.textStyle;
        if (!textColor.isEmpty())
            add(QStringLiteral("color"), textColor);
        add(QStringLiteral("font-family"), QStringLiteral("\"%1\"").arg(boundField("fontFamily", t.family)));
        add(QStringLiteral("font-size"), boundField("fontSize", px(t.size)));
        if (t.weight != 400)
            add(QStringLiteral("font-weight"), boundField("fontWeight", QString::number(t.weight)));
        if (t.italic)
            add(QStringLiteral("font-style"), QStringLiteral("italic"));
        if (t.lineHeight > 0)
            add(QStringLiteral("line-height"), t.lineHeightPercent ? num(t.lineHeight) + QLatin1Char('%') : px(t.lineHeight));
        if (t.letterSpacing)
            add(QStringLiteral("letter-spacing"), num(t.letterSpacing / 100) + QStringLiteral("em"));
        static const char* const aligns[] = {"left", "center", "right", "justify"};
        if (t.alignH != TextAlignH::Left)
            add(QStringLiteral("text-align"), QString::fromLatin1(aligns[int(t.alignH)]));
        static const char* const cases[] = {"none", "uppercase", "lowercase", "capitalize"};
        if (t.textCase != TextCase::Original)
            add(QStringLiteral("text-transform"), QString::fromLatin1(cases[int(t.textCase)]));
        if (t.decoration == TextDecoration::Underline)
            add(QStringLiteral("text-decoration"), QStringLiteral("underline"));
        else if (t.decoration == TextDecoration::Strikethrough)
            add(QStringLiteral("text-decoration"), QStringLiteral("line-through"));
        if (!t.features.isEmpty()) {
            QStringList f;
            for (auto it = t.features.begin(); it != t.features.end(); ++it)
                f << QStringLiteral("\"%1\" %2").arg(it.key()).arg(it.value());
            add(QStringLiteral("font-feature-settings"), f.join(QStringLiteral(", ")));
        }
        if (t.autoResize == TextAutoResize::Truncate || t.maxLines > 0) {
            add(QStringLiteral("overflow"), QStringLiteral("hidden"));
            add(QStringLiteral("text-overflow"), QStringLiteral("ellipsis"));
            if (t.maxLines > 1) {
                add(QStringLiteral("display"), QStringLiteral("-webkit-box"));
                add(QStringLiteral("-webkit-line-clamp"), QString::number(t.maxLines));
                add(QStringLiteral("-webkit-box-orient"), QStringLiteral("vertical"));
            } else {
                add(QStringLiteral("white-space"), QStringLiteral("nowrap"));
            }
        }
    }
    if (!p.visible)
        add(QStringLiteral("display"), QStringLiteral("none"));
    return QStringLiteral("/* %1 */\n").arg(p.name) + out.join(QLatin1Char('\n'));
}

} // namespace drav::code
