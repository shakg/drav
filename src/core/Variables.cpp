#include "Variables.h"

#include "Document.h"
#include "Text.h"

namespace drav::vars {

namespace {

struct FieldDef {
    const char* key;
    VarType type;
};
const FieldDef kFields[] = {
    {"w", VarType::Number}, {"h", VarType::Number}, {"cornerRadius", VarType::Number}, {"tl", VarType::Number},
    {"tr", VarType::Number}, {"br", VarType::Number}, {"bl", VarType::Number}, {"spacing", VarType::Number},
    {"counterSpacing", VarType::Number}, {"padL", VarType::Number}, {"padT", VarType::Number},
    {"padR", VarType::Number}, {"padB", VarType::Number}, {"opacity", VarType::Number},
    {"strokeWeight", VarType::Number}, {"minW", VarType::Number}, {"maxW", VarType::Number},
    {"minH", VarType::Number}, {"maxH", VarType::Number}, {"fontSize", VarType::Number},
    {"lineHeight", VarType::Number}, {"letterSpacing", VarType::Number}, {"paragraphSpacing", VarType::Number},
    {"fontWeight", VarType::Number}, {"characters", VarType::String}, {"fontFamily", VarType::String},
    {"visible", VarType::Boolean},
};

float* numberField(Props& p, const QString& k)
{
    static const QHash<QString, int> idx = [] {
        QHash<QString, int> h;
        int i = 0;
        for (const char* k : {"cornerRadius", "tl", "tr", "br", "bl", "spacing", "counterSpacing", "padL", "padT", "padR",
                 "padB", "opacity", "strokeWeight", "minW", "maxW", "minH", "maxH", "fontSize", "lineHeight",
                 "letterSpacing", "paragraphSpacing", "w", "h"})
            h.insert(QLatin1String(k), i++);
        return h;
    }();
    switch (idx.value(k, -1)) {
    case 0:
    case 1: return &p.corners.tl;
    case 2: return &p.corners.tr;
    case 3: return &p.corners.br;
    case 4: return &p.corners.bl;
    case 5: return &p.layout.spacing;
    case 6: return &p.layout.counterSpacing;
    case 7: return &p.layout.padL;
    case 8: return &p.layout.padT;
    case 9: return &p.layout.padR;
    case 10: return &p.layout.padB;
    case 11: return &p.opacity;
    case 12: return &p.stroke.weight;
    case 13: return &p.layoutChild.minW;
    case 14: return &p.layoutChild.maxW;
    case 15: return &p.layoutChild.minH;
    case 16: return &p.layoutChild.maxH;
    case 17: return &p.textStyle.size;
    case 18: return &p.textStyle.lineHeight;
    case 19: return &p.textStyle.letterSpacing;
    case 20: return &p.textStyle.paragraphSpacing;
    case 21: return &p.width;
    case 22: return &p.height;
    default: return nullptr;
    }
}

void setNumber(Props& p, const QString& k, float v)
{
    if (k == QLatin1String("cornerRadius")) {
        v = std::max(0.f, v);
        p.corners = {v, v, v, v};
    } else if (k == QLatin1String("opacity")) {
        p.opacity = std::clamp(v / 100.f, 0.f, 1.f); // opacity variables are percentages
    } else if (k == QLatin1String("fontWeight")) {
        p.textStyle.weight = std::clamp(int(std::lround(v)), 100, 1000);
    } else if (k == QLatin1String("fontSize")) {
        p.textStyle.size = std::clamp(v, 1.f, 2000.f);
    } else if (float* f = numberField(p, k)) {
        *f = v;
    }
}

void copyField(Props& dst, const Props& src, const QString& k)
{
    if (k == QLatin1String("w") || k == QLatin1String("h")) {
        dst.width = src.width;
        dst.height = src.height;
        dst.vector = src.vector;
    } else if (k == QLatin1String("cornerRadius")) {
        dst.corners = src.corners;
    } else if (k == QLatin1String("fontWeight")) {
        dst.textStyle.weight = src.textStyle.weight;
    } else if (k == QLatin1String("characters")) {
        dst.text = src.text;
    } else if (k == QLatin1String("fontFamily")) {
        dst.textStyle.family = src.textStyle.family;
    } else if (k == QLatin1String("visible")) {
        dst.visible = src.visible;
    } else if (float* f = numberField(dst, k)) {
        *f = *numberField(const_cast<Props&>(src), k);
    }
}

} // namespace

bool fieldType(const QString& key, VarType* type)
{
    for (const FieldDef& f : kFields)
        if (key == QLatin1String(f.key)) {
            if (type)
                *type = f.type;
            return true;
        }
    return false;
}

QStringList bindableFields()
{
    QStringList l;
    for (const FieldDef& f : kFields)
        l << QLatin1String(f.key);
    return l;
}

std::optional<VarValue> resolve(const Library& lib, const QString& varId, const ModeFn& mode, VarType* type)
{
    QString cur = varId;
    for (int depth = 0; depth < 16; ++depth) {
        const Variable* v = lib.variable(cur);
        if (!v)
            return std::nullopt;
        if (type && depth == 0)
            *type = v->type;
        QString m = mode ? mode(v->collection) : QString();
        if (m.isEmpty() || !v->values.contains(m)) {
            const VarCollection* c = lib.collection(v->collection);
            m = c && !c->modes.empty() ? c->modes.front().id : QString();
        }
        auto it = v->values.constFind(m);
        if (it == v->values.constEnd()) {
            if (v->values.isEmpty())
                return std::nullopt;
            it = v->values.constBegin();
        }
        if (it->alias.isEmpty())
            return *it;
        cur = it->alias;
    }
    return std::nullopt; // alias cycle
}

QString modeFor(const Document& doc, NodeId id, const QString& collection)
{
    for (const Node* n = doc.find(id); n; n = n->parent ? doc.find(n->parent) : nullptr) {
        auto it = n->p.variableModes.constFind(collection);
        if (it != n->p.variableModes.constEnd())
            return *it;
    }
    return {};
}

bool hasRefs(const Props& p)
{
    if (!p.fillStyle.isEmpty() || !p.strokeStyle.isEmpty() || !p.textStyleId.isEmpty() || !p.effectStyle.isEmpty()
        || !p.gridStyle.isEmpty() || !p.bindings.isEmpty())
        return true;
    for (const Paint& x : p.fills)
        if (!x.colorVar.isEmpty())
            return true;
    for (const Paint& x : p.strokes)
        if (!x.colorVar.isEmpty())
            return true;
    for (const Effect& e : p.effects)
        if (!e.colorVar.isEmpty())
            return true;
    return false;
}

void apply(const Document& doc, NodeId id, Props& p, std::optional<SkSize>* size)
{
    const Library& lib = doc.library();
    if (const Style* s = lib.style(p.fillStyle); s && s->type == StyleType::Fill)
        p.fills = s->paints;
    if (const Style* s = lib.style(p.strokeStyle); s && s->type == StyleType::Fill)
        p.strokes = s->paints;
    if (const Style* s = lib.style(p.textStyleId); s && s->type == StyleType::Text)
        applyTypography(p.textStyle, s->text);
    if (const Style* s = lib.style(p.effectStyle); s && s->type == StyleType::Effect)
        p.effects = s->effects;
    if (const Style* s = lib.style(p.gridStyle); s && s->type == StyleType::Grid)
        p.grids = s->grids;

    QHash<QString, QString> modes; // per-call cache: collection -> mode
    const ModeFn mode = [&](const QString& c) {
        auto it = modes.constFind(c);
        if (it != modes.constEnd())
            return *it;
        return *modes.insert(c, modeFor(doc, id, c));
    };
    auto color = [&](const QString& var, SkColor4f& out) {
        if (var.isEmpty())
            return;
        VarType t;
        if (auto v = resolve(lib, var, mode, &t); v && t == VarType::Color)
            out = v->color;
    };
    for (Paint& x : p.fills)
        color(x.colorVar, x.color);
    for (Paint& x : p.strokes)
        color(x.colorVar, x.color);
    for (Effect& e : p.effects)
        color(e.colorVar, e.color);

    std::optional<float> w, h;
    for (auto it = p.bindings.constBegin(); it != p.bindings.constEnd(); ++it) {
        VarType want, got;
        if (!fieldType(it.key(), &want))
            continue;
        auto v = resolve(lib, it.value(), mode, &got);
        if (!v || got != want)
            continue;
        const QString& k = it.key();
        switch (want) {
        case VarType::Number:
            if (k == QLatin1String("w"))
                w = std::max(0.01f, v->number);
            else if (k == QLatin1String("h"))
                h = std::max(0.01f, v->number);
            else
                setNumber(p, k, v->number);
            break;
        case VarType::String:
            if (k == QLatin1String("characters") && p.text != v->string)
                text::splice(p, 0, int(p.text.size()), v->string);
            else if (k == QLatin1String("fontFamily") && !v->string.isEmpty())
                p.textStyle.family = v->string;
            break;
        case VarType::Boolean:
            if (k == QLatin1String("visible"))
                p.visible = v->boolean;
            break;
        case VarType::Color: break;
        }
    }
    if (size && (w || h))
        *size = SkSize::Make(w.value_or(p.width), h.value_or(p.height));
}

void alignDriven(Props& node, const Props& main)
{
    auto same = [](const QString& a, const QString& b) { return !a.isEmpty() && a == b; };
    if (same(node.fillStyle, main.fillStyle))
        node.fills = main.fills;
    if (same(node.strokeStyle, main.strokeStyle))
        node.strokes = main.strokes;
    if (same(node.effectStyle, main.effectStyle))
        node.effects = main.effects;
    if (same(node.textStyleId, main.textStyleId))
        applyTypography(node.textStyle, main.textStyle);
    auto paints = [&](std::vector<Paint>& a, const std::vector<Paint>& b) {
        if (a.size() == b.size())
            for (size_t i = 0; i < a.size(); ++i)
                if (same(a[i].colorVar, b[i].colorVar))
                    a[i].color = b[i].color;
    };
    paints(node.fills, main.fills);
    paints(node.strokes, main.strokes);
    if (node.effects.size() == main.effects.size())
        for (size_t i = 0; i < node.effects.size(); ++i)
            if (same(node.effects[i].colorVar, main.effects[i].colorVar))
                node.effects[i].color = main.effects[i].color;
    for (auto it = node.bindings.constBegin(); it != node.bindings.constEnd(); ++it)
        if (main.bindings.value(it.key()) == it.value())
            copyField(node, main, it.key());
}

} // namespace drav::vars
