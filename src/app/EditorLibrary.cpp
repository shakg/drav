// Styles and variables: the editor commands behind the asset panel, style pickers and
// variable binding in the inspector.

#include "Editor.h"
#include "PropMaps.h"

#include "core/Variables.h"

namespace drav {

using namespace maps;

namespace {

QVariant valueToVariant(const VarValue& v, VarType t)
{
    switch (t) {
    case VarType::Color: return toQColor(v.color);
    case VarType::Number: return v.number;
    case VarType::String: return v.string;
    case VarType::Boolean: return v.boolean;
    }
    return {};
}

VarValue defaultValue(VarType t)
{
    VarValue v;
    v.color = SkColors::kWhite;
    v.boolean = true;
    (void)t;
    return v;
}

QString uniqueName(const QString& base, const QStringList& taken)
{
    for (int i = 1;; ++i) {
        QString n = base + QLatin1Char(' ') + QString::number(i);
        if (!taken.contains(n))
            return n;
    }
}

} // namespace

QVariantMap Editor::library() const
{
    const Library& lib = m_doc->library();
    QHash<QString, int> users;
    m_doc->walk(m_doc->root(), [&](const Node& n) {
        for (const QString* s : {&n.p.fillStyle, &n.p.strokeStyle, &n.p.textStyleId, &n.p.effectStyle, &n.p.gridStyle})
            if (!s->isEmpty())
                ++users[*s];
        return true;
    });
    QVariantList styles;
    for (const Style& st : lib.styles)
        styles << QVariantMap{{QStringLiteral("id"), st.id}, {QStringLiteral("name"), st.name},
            {QStringLiteral("type"), QString::fromLatin1(styleTypeName(st.type))},
            {QStringLiteral("description"), st.description}, {QStringLiteral("paints"), paintsToList(st.paints)},
            {QStringLiteral("text"), typographyToMap(st.text)}, {QStringLiteral("effects"), effectsToList(st.effects)},
            {QStringLiteral("grids"), gridsToList(st.grids)}, {QStringLiteral("users"), users.value(st.id)}};
    QVariantList collections;
    for (const VarCollection& c : lib.collections) {
        QVariantList modes;
        for (const VarMode& m : c.modes)
            modes << QVariantMap{{QStringLiteral("id"), m.id}, {QStringLiteral("name"), m.name}};
        collections << QVariantMap{{QStringLiteral("id"), c.id}, {QStringLiteral("name"), c.name},
            {QStringLiteral("modes"), modes}};
    }
    QVariantList variables;
    for (const Variable& v : lib.variables) {
        QVariantMap values;
        if (const VarCollection* c = lib.collection(v.collection))
            for (const VarMode& m : c->modes) {
                const VarValue x = v.values.value(m.id, defaultValue(v.type));
                const QString modeId = m.id;
                auto r = vars::resolve(lib, v.id, [&](const QString&) { return modeId; });
                values.insert(m.id, QVariantMap{{QStringLiteral("value"), valueToVariant(x, v.type)},
                                        {QStringLiteral("alias"), x.alias},
                                        {QStringLiteral("resolved"), r ? valueToVariant(*r, v.type) : QVariant()}});
            }
        variables << QVariantMap{{QStringLiteral("id"), v.id}, {QStringLiteral("name"), v.name},
            {QStringLiteral("collection"), v.collection}, {QStringLiteral("type"), QString::fromLatin1(varTypeName(v.type))},
            {QStringLiteral("description"), v.description}, {QStringLiteral("values"), values}};
    }
    return {{QStringLiteral("styles"), styles}, {QStringLiteral("collections"), collections},
        {QStringLiteral("variables"), variables}};
}

// ---------------------------------------------------------------- styles

static QString* styleRef(Props& p, const QString& slot)
{
    if (slot == QLatin1String("fills")) return &p.fillStyle;
    if (slot == QLatin1String("strokes")) return &p.strokeStyle;
    if (slot == QLatin1String("text")) return p.type == NodeType::Text ? &p.textStyleId : nullptr;
    if (slot == QLatin1String("effects")) return &p.effectStyle;
    if (slot == QLatin1String("grids")) return isFrameLike(p.type) ? &p.gridStyle : nullptr;
    return nullptr;
}

QString Editor::createStyle(const QString& slot, const QString& name)
{
    auto sel = selected();
    if (sel.empty())
        return {};
    const Props& src = m_doc->at(sel.front()).p;
    Style st;
    st.id = Library::newId();
    if (slot == QLatin1String("fills") || slot == QLatin1String("strokes")) {
        st.type = StyleType::Fill;
        st.paints = slot == QLatin1String("fills") ? src.fills : src.strokes;
    } else if (slot == QLatin1String("text")) {
        st.type = StyleType::Text;
        applyTypography(st.text, src.textStyle);
    } else if (slot == QLatin1String("effects")) {
        st.type = StyleType::Effect;
        st.effects = src.effects;
    } else if (slot == QLatin1String("grids")) {
        st.type = StyleType::Grid;
        st.grids = src.grids;
    } else {
        return {};
    }
    QStringList taken;
    for (const Style& s : m_doc->library().styles)
        taken << s.name;
    static const char* const bases[] = {"Color", "Text", "Effect", "Grid"};
    st.name = name.trimmed().isEmpty() ? uniqueName(QLatin1String(bases[int(st.type)]), taken) : name.trimmed();
    m_doc->breakMerge();
    m_doc->begin(tr("Create style"));
    m_doc->editLibrary().styles.push_back(st);
    for (NodeId id : sel)
        if (QString* ref = styleRef(m_doc->edit(id), slot))
            *ref = st.id;
    m_doc->commit();
    return st.id;
}

void Editor::applyStyle(const QString& slot, const QString& styleId)
{
    auto sel = selected();
    if (sel.empty())
        return;
    m_doc->breakMerge();
    m_doc->begin(styleId.isEmpty() ? tr("Detach style") : tr("Apply style"));
    for (NodeId id : sel) {
        Props probe = m_doc->at(id).p;
        QString* ref = styleRef(probe, slot);
        if (ref && *ref != styleId)
            *styleRef(m_doc->edit(id), slot) = styleId;
    }
    m_doc->commit();
}

void Editor::editStyle(const QString& id, const QVariantMap& changes, bool live)
{
    if (!m_doc->library().style(id))
        return;
    if (!live)
        m_doc->breakMerge();
    m_doc->begin(tr("Edit style"));
    if (live)
        m_doc->setMergeKey(QStringLiteral("style:") + id);
    Style& st = *m_doc->editLibrary().style(id);
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        const QString& k = it.key();
        if (k == QLatin1String("name") && !it.value().toString().trimmed().isEmpty())
            st.name = it.value().toString().trimmed();
        else if (k == QLatin1String("description"))
            st.description = it.value().toString();
        else if (k == QLatin1String("paints"))
            st.paints = paintsFromList(it.value());
        else if (k == QLatin1String("text"))
            typographyFromMap(st.text, it.value().toMap());
        else if (k == QLatin1String("effects"))
            st.effects = effectsFromList(it.value());
        else if (k == QLatin1String("grids"))
            st.grids = gridsFromList(it.value());
    }
    m_doc->commit();
}

void Editor::updateStyleFromSelection(const QString& id)
{
    const Style* cur = m_doc->library().style(id);
    auto sel = selected();
    if (!cur || sel.empty())
        return;
    const Props& src = m_doc->at(sel.front()).p;
    m_doc->breakMerge();
    m_doc->begin(tr("Update style"));
    Style& st = *m_doc->editLibrary().style(id);
    switch (st.type) {
    case StyleType::Fill: st.paints = src.strokeStyle == id && src.fillStyle != id ? src.strokes : src.fills; break;
    case StyleType::Text: applyTypography(st.text, src.textStyle); break;
    case StyleType::Effect: st.effects = src.effects; break;
    case StyleType::Grid: st.grids = src.grids; break;
    }
    // Keep the source layer attached to the style it just defined.
    if (QString* ref = styleRef(m_doc->edit(sel.front()),
            st.type == StyleType::Fill ? (src.strokeStyle == id && src.fillStyle != id ? QStringLiteral("strokes") : QStringLiteral("fills"))
            : st.type == StyleType::Text ? QStringLiteral("text")
            : st.type == StyleType::Effect ? QStringLiteral("effects")
                                           : QStringLiteral("grids")))
        *ref = id;
    m_doc->commit();
}

QString Editor::duplicateStyle(const QString& id)
{
    const Style* cur = m_doc->library().style(id);
    if (!cur)
        return {};
    Style copy = *cur;
    copy.id = Library::newId();
    copy.name = cur->name + tr(" copy");
    m_doc->breakMerge();
    m_doc->begin(tr("Duplicate style"));
    m_doc->editLibrary().styles.push_back(copy);
    m_doc->commit();
    return copy.id;
}

void Editor::detachRefs(const std::function<bool(const QString&)>& gone)
{
    std::vector<NodeId> ids;
    m_doc->walk(m_doc->root(), [&](const Node& n) {
        if (vars::hasRefs(n.p))
            ids.push_back(n.id);
        return true;
    });
    for (NodeId id : ids) {
        Props p = m_doc->at(id).p;
        for (QString* s : {&p.fillStyle, &p.strokeStyle, &p.textStyleId, &p.effectStyle, &p.gridStyle})
            if (!s->isEmpty() && gone(*s))
                s->clear();
        for (auto it = p.bindings.begin(); it != p.bindings.end();)
            it = gone(it.value()) ? p.bindings.erase(it) : std::next(it);
        for (auto* list : {&p.fills, &p.strokes})
            for (Paint& x : *list)
                if (!x.colorVar.isEmpty() && gone(x.colorVar))
                    x.colorVar.clear();
        for (Effect& e : p.effects)
            if (!e.colorVar.isEmpty() && gone(e.colorVar))
                e.colorVar.clear();
        if (!(p == m_doc->at(id).p))
            m_doc->edit(id) = p;
    }
}

void Editor::deleteStyle(const QString& id)
{
    if (!m_doc->library().style(id))
        return;
    m_doc->breakMerge();
    m_doc->begin(tr("Delete style"));
    std::erase_if(m_doc->editLibrary().styles, [&](const Style& s) { return s.id == id; });
    detachRefs([&](const QString& r) { return r == id; }); // layers keep the last values
    m_doc->commit();
}

void Editor::selectStyleUsers(const QString& id)
{
    std::vector<NodeId> ids;
    m_doc->walk(m_doc->currentPage(), [&](const Node& n) {
        const Props& p = n.p;
        if (p.fillStyle == id || p.strokeStyle == id || p.textStyleId == id || p.effectStyle == id || p.gridStyle == id)
            ids.push_back(n.id);
        return true;
    });
    select(ids);
}

// ---------------------------------------------------------------- variables

QString Editor::addCollection(const QString& name)
{
    QStringList taken;
    for (const VarCollection& c : m_doc->library().collections)
        taken << c.name;
    VarCollection c;
    c.id = Library::newId();
    c.name = name.trimmed().isEmpty() ? uniqueName(tr("Collection"), taken) : name.trimmed();
    c.modes.push_back({Library::newId(), tr("Mode 1")});
    m_doc->breakMerge();
    m_doc->begin(tr("Add collection"));
    m_doc->editLibrary().collections.push_back(c);
    m_doc->commit();
    return c.id;
}

void Editor::renameCollection(const QString& id, const QString& name)
{
    if (!m_doc->library().collection(id) || name.trimmed().isEmpty())
        return;
    m_doc->breakMerge();
    m_doc->begin(tr("Rename collection"));
    m_doc->editLibrary().collection(id)->name = name.trimmed();
    m_doc->commit();
}

void Editor::deleteCollection(const QString& id)
{
    if (!m_doc->library().collection(id))
        return;
    m_doc->breakMerge();
    m_doc->begin(tr("Delete collection"));
    Library& lib = m_doc->editLibrary();
    QSet<QString> removed;
    for (const Variable& v : lib.variables)
        if (v.collection == id)
            removed.insert(v.id);
    std::erase_if(lib.variables, [&](const Variable& v) { return v.collection == id; });
    std::erase_if(lib.collections, [&](const VarCollection& c) { return c.id == id; });
    detachRefs([&](const QString& r) { return removed.contains(r); });
    std::vector<NodeId> withMode;
    m_doc->walk(m_doc->root(), [&](const Node& n) {
        if (n.p.variableModes.contains(id))
            withMode.push_back(n.id);
        return true;
    });
    for (NodeId n : withMode)
        m_doc->edit(n).variableModes.remove(id);
    m_doc->commit();
}

QString Editor::addMode(const QString& collection, const QString& name)
{
    const VarCollection* c = m_doc->library().collection(collection);
    if (!c)
        return {};
    QStringList taken;
    for (const VarMode& m : c->modes)
        taken << m.name;
    const VarMode mode{Library::newId(), name.trimmed().isEmpty() ? uniqueName(tr("Mode"), taken) : name.trimmed()};
    const QString first = c->modes.front().id;
    m_doc->breakMerge();
    m_doc->begin(tr("Add mode"));
    Library& lib = m_doc->editLibrary();
    lib.collection(collection)->modes.push_back(mode);
    for (Variable& v : lib.variables)
        if (v.collection == collection)
            v.values.insert(mode.id, v.values.value(first, defaultValue(v.type)));
    m_doc->commit();
    return mode.id;
}

void Editor::renameMode(const QString& collection, const QString& mode, const QString& name)
{
    const VarCollection* c = m_doc->library().collection(collection);
    if (!c || name.trimmed().isEmpty())
        return;
    m_doc->breakMerge();
    m_doc->begin(tr("Rename mode"));
    for (VarMode& m : m_doc->editLibrary().collection(collection)->modes)
        if (m.id == mode)
            m.name = name.trimmed();
    m_doc->commit();
}

void Editor::deleteMode(const QString& collection, const QString& mode)
{
    const VarCollection* c = m_doc->library().collection(collection);
    if (!c || c->modes.size() < 2)
        return;
    m_doc->breakMerge();
    m_doc->begin(tr("Delete mode"));
    Library& lib = m_doc->editLibrary();
    std::erase_if(lib.collection(collection)->modes, [&](const VarMode& m) { return m.id == mode; });
    for (Variable& v : lib.variables)
        if (v.collection == collection)
            v.values.remove(mode);
    std::vector<NodeId> withMode;
    m_doc->walk(m_doc->root(), [&](const Node& n) {
        if (n.p.variableModes.value(collection) == mode)
            withMode.push_back(n.id);
        return true;
    });
    for (NodeId n : withMode)
        m_doc->edit(n).variableModes.remove(collection);
    m_doc->commit();
}

QString Editor::addVariable(const QString& collection, const QString& type, const QString& name)
{
    const VarCollection* c = m_doc->library().collection(collection);
    if (!c)
        return {};
    Variable v;
    v.id = Library::newId();
    v.collection = collection;
    v.type = varTypeFromName(type);
    QStringList taken;
    for (const Variable& o : m_doc->library().variables)
        if (o.collection == collection)
            taken << o.name;
    static const char* const bases[] = {"Color", "Number", "String", "Boolean"};
    v.name = name.trimmed().isEmpty() ? uniqueName(QLatin1String(bases[int(v.type)]), taken) : name.trimmed();
    for (const VarMode& m : c->modes)
        v.values.insert(m.id, defaultValue(v.type));
    m_doc->breakMerge();
    m_doc->begin(tr("Add variable"));
    m_doc->editLibrary().variables.push_back(v);
    m_doc->commit();
    return v.id;
}

void Editor::renameVariable(const QString& id, const QString& name)
{
    if (!m_doc->library().variable(id) || name.trimmed().isEmpty())
        return;
    m_doc->breakMerge();
    m_doc->begin(tr("Rename variable"));
    m_doc->editLibrary().variable(id)->name = name.trimmed();
    m_doc->commit();
}

void Editor::setVariableDescription(const QString& id, const QString& text)
{
    if (!m_doc->library().variable(id))
        return;
    m_doc->breakMerge();
    m_doc->begin(tr("Edit variable"));
    m_doc->editLibrary().variable(id)->description = text;
    m_doc->commit();
}

void Editor::setVariableValue(const QString& id, const QString& mode, const QVariant& value, bool live)
{
    const Variable* cur = m_doc->library().variable(id);
    if (!cur)
        return;
    VarValue x;
    switch (cur->type) {
    case VarType::Color: x.color = toSk(value); break;
    case VarType::Number: x.number = value.toFloat(); break;
    case VarType::String: x.string = value.toString(); break;
    case VarType::Boolean: x.boolean = value.toBool(); break;
    }
    if (cur->values.value(mode) == x)
        return;
    if (!live)
        m_doc->breakMerge();
    m_doc->begin(tr("Edit variable"));
    if (live)
        m_doc->setMergeKey(QStringLiteral("var:") + id + mode);
    m_doc->editLibrary().variable(id)->values.insert(mode, x);
    m_doc->commit();
}

void Editor::setVariableAlias(const QString& id, const QString& mode, const QString& target)
{
    const Library& lib = m_doc->library();
    const Variable* cur = lib.variable(id);
    const Variable* to = lib.variable(target);
    if (!cur || !to || to->type != cur->type || target == id)
        return;
    // Refuse aliases that would loop back to this variable.
    Library probe = lib;
    VarValue x;
    x.alias = target;
    probe.variable(id)->values.insert(mode, x);
    const QString m = mode;
    if (!vars::resolve(probe, id, [&](const QString&) { return m; }))
        return;
    m_doc->breakMerge();
    m_doc->begin(tr("Alias variable"));
    m_doc->editLibrary().variable(id)->values.insert(mode, x);
    m_doc->commit();
}

void Editor::deleteVariable(const QString& id)
{
    const Library& before = m_doc->library();
    if (!before.variable(id))
        return;
    // Aliases to the deleted variable keep its value in their mode.
    std::vector<std::tuple<QString, QString, VarValue>> literal;
    for (const Variable& v : before.variables)
        for (auto it = v.values.begin(); it != v.values.end(); ++it)
            if (it->alias == id) {
                const QString m = it.key();
                auto r = vars::resolve(before, id, [&](const QString&) { return m; });
                VarValue x = r ? *r : defaultValue(v.type);
                x.alias.clear();
                literal.emplace_back(v.id, it.key(), x);
            }
    m_doc->breakMerge();
    m_doc->begin(tr("Delete variable"));
    Library& lib = m_doc->editLibrary();
    for (auto& [vid, mode, x] : literal)
        lib.variable(vid)->values.insert(mode, x);
    std::erase_if(lib.variables, [&](const Variable& v) { return v.id == id; });
    detachRefs([&](const QString& r) { return r == id; });
    m_doc->commit();
}

void Editor::bindVariable(const QString& field, const QString& variableId)
{
    auto sel = selected();
    if (sel.empty())
        return;
    const Variable* var = m_doc->library().variable(variableId);
    if (!variableId.isEmpty() && !var)
        return;
    const int slash = field.indexOf(QLatin1Char('/'));
    const QString list = slash > 0 ? field.left(slash) : QString();
    const int index = slash > 0 ? field.mid(slash + 1).toInt() : -1;
    VarType want = VarType::Color;
    if (list.isEmpty() && !vars::fieldType(field, &want))
        return;
    if (var && var->type != want)
        return;
    m_doc->breakMerge();
    m_doc->begin(variableId.isEmpty() ? tr("Detach variable") : tr("Apply variable"));
    for (NodeId id : sel) {
        Props p = m_doc->at(id).p;
        if (list == QLatin1String("fills") || list == QLatin1String("strokes")) {
            auto& paints = list == QLatin1String("fills") ? p.fills : p.strokes;
            if (index < 0 || index >= int(paints.size()))
                continue;
            paints[index].colorVar = variableId;
            if (!variableId.isEmpty())
                paints[index].type = PaintType::Solid;
            (list == QLatin1String("fills") ? p.fillStyle : p.strokeStyle).clear();
        } else if (list == QLatin1String("effects")) {
            if (index < 0 || index >= int(p.effects.size()))
                continue;
            p.effects[index].colorVar = variableId;
            p.effectStyle.clear();
        } else if (variableId.isEmpty()) {
            p.bindings.remove(field);
            if (field == QLatin1String("cornerRadius"))
                for (const char* k : {"tl", "tr", "br", "bl"})
                    p.bindings.remove(QLatin1String(k));
        } else {
            if (field == QLatin1String("cornerRadius"))
                for (const char* k : {"tl", "tr", "br", "bl"})
                    p.bindings.remove(QLatin1String(k));
            if (field == QLatin1String("w"))
                p.layoutChild.sizingH = LayoutSizing::Fixed;
            if (field == QLatin1String("h"))
                p.layoutChild.sizingV = LayoutSizing::Fixed;
            const bool textOnly = field.startsWith(QLatin1String("font")) || field == QLatin1String("characters")
                || field == QLatin1String("lineHeight") || field == QLatin1String("letterSpacing")
                || field == QLatin1String("paragraphSpacing");
            if (textOnly && p.type != NodeType::Text)
                continue;
            if (textOnly && field != QLatin1String("characters"))
                p.textStyleId.clear();
            p.bindings.insert(field, variableId);
        }
        if (!(p == m_doc->at(id).p))
            m_doc->edit(id) = p;
    }
    m_doc->commit();
}

void Editor::setVariableMode(const QString& collection, const QString& mode)
{
    auto sel = selected();
    if (sel.empty())
        return;
    m_doc->breakMerge();
    m_doc->begin(tr("Change variable mode"));
    for (NodeId id : sel) {
        if (m_doc->at(id).p.variableModes.value(collection) == mode)
            continue;
        Props& p = m_doc->edit(id);
        if (mode.isEmpty())
            p.variableModes.remove(collection);
        else
            p.variableModes.insert(collection, mode);
    }
    m_doc->commit();
}

QVariant Editor::resolveVariable(const QString& id) const
{
    const auto& sel = m_doc->selection();
    const NodeId ctx = sel.empty() ? m_doc->currentPage() : sel.front();
    VarType t;
    auto r = vars::resolve(m_doc->library(), id, [&](const QString& c) { return vars::modeFor(*m_doc, ctx, c); }, &t);
    return r ? valueToVariant(*r, t) : QVariant();
}

} // namespace drav
