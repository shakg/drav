// Component properties: definitions on components and sets, links from layers, instance values.

#include "Editor.h"

#include "core/Library.h"

namespace drav {

namespace {

const char* const kPropTypes[] = {"BOOLEAN", "TEXT", "INSTANCE_SWAP"};

ComponentPropType propType(const QString& s)
{
    for (int i = 0; i < 3; ++i)
        if (s == QLatin1String(kPropTypes[i]))
            return ComponentPropType(i);
    return ComponentPropType::Boolean;
}

ComponentPropType typeForField(const QString& field)
{
    if (field == QLatin1String("characters"))
        return ComponentPropType::Text;
    if (field == QLatin1String("mainComponent"))
        return ComponentPropType::InstanceSwap;
    return ComponentPropType::Boolean;
}

// Where new properties go for a layer: the component set of a variant, else the component.
NodeId defOwnerFor(const Document& d, NodeId id)
{
    const Node& n = d.at(id);
    if (n.type() == NodeType::ComponentSet)
        return id;
    NodeId comp = d.componentOf(id);
    if (!comp)
        return kNoNode;
    NodeId parent = d.at(comp).parent;
    return d.at(parent).type() == NodeType::ComponentSet ? parent : comp;
}

// The component or set defining a property id.
NodeId definerOf(const Document& d, const QString& propId)
{
    NodeId found = kNoNode;
    d.walk(d.root(), [&](const Node& n) {
        if (found)
            return false;
        for (const ComponentProp& p : n.p.componentProps)
            if (p.id == propId)
                found = n.id;
        return !found;
    });
    return found;
}

} // namespace

QString Editor::addComponentProp(const QString& type, const QString& name)
{
    auto sel = selected();
    if (sel.size() != 1)
        return {};
    const NodeId owner = defOwnerFor(*m_doc, sel.front());
    if (!owner || m_doc->isInsideInstance(owner))
        return {};
    ComponentProp d;
    d.id = Library::newId();
    d.type = propType(type);
    static const char* const bases[] = {"Property", "Text", "Instance"};
    d.name = name.trimmed().isEmpty() ? QString::fromLatin1(bases[int(d.type)]) : name.trimmed();
    if (d.type == ComponentPropType::Boolean)
        d.defaultValue = QStringLiteral("true");
    else if (d.type == ComponentPropType::Text)
        d.defaultValue = d.name;
    m_doc->breakMerge();
    m_doc->begin(tr("Add property"));
    m_doc->edit(owner).componentProps.push_back(d);
    m_doc->commit();
    return d.id;
}

QString Editor::createLinkedProp(const QString& field, const QString& name)
{
    auto sel = selected();
    if (sel.size() != 1)
        return {};
    const NodeId layer = sel.front();
    const NodeId owner = defOwnerFor(*m_doc, layer);
    const Props& lp = m_doc->at(layer).p;
    if (!owner || owner == layer || m_doc->isInsideInstance(layer))
        return {};
    if (field == QLatin1String("characters") && lp.type != NodeType::Text)
        return {};
    if (field == QLatin1String("mainComponent") && lp.type != NodeType::Instance)
        return {};
    ComponentProp d;
    d.id = Library::newId();
    d.type = typeForField(field);
    d.name = name.trimmed().isEmpty() ? lp.name : name.trimmed();
    d.defaultValue = Document::fieldValue(lp, field);
    if (d.type == ComponentPropType::InstanceSwap)
        d.preferred = {lp.componentId};
    m_doc->breakMerge();
    m_doc->begin(tr("Create property"));
    m_doc->edit(owner).componentProps.push_back(d);
    m_doc->edit(layer).propRefs.insert(field, d.id);
    m_doc->commit();
    return d.id;
}

void Editor::editComponentProp(const QString& id, const QVariantMap& changes)
{
    const NodeId owner = definerOf(*m_doc, id);
    if (!owner)
        return;
    m_doc->breakMerge();
    m_doc->begin(tr("Edit property"));
    for (ComponentProp& d : m_doc->edit(owner).componentProps) {
        if (d.id != id)
            continue;
        if (changes.contains(QStringLiteral("name")) && !changes.value(QStringLiteral("name")).toString().trimmed().isEmpty())
            d.name = changes.value(QStringLiteral("name")).toString().trimmed();
        if (changes.contains(QStringLiteral("default"))) {
            const QVariant v = changes.value(QStringLiteral("default"));
            d.defaultValue = v.typeId() == QMetaType::Bool ? (v.toBool() ? QStringLiteral("true") : QStringLiteral("false"))
                                                           : v.toString();
        }
        if (changes.contains(QStringLiteral("preferred"))) {
            d.preferred.clear();
            for (const QVariant& x : changes.value(QStringLiteral("preferred")).toList())
                d.preferred.push_back(NodeId(x.toLongLong()));
        }
    }
    m_doc->commit();
}

void Editor::deleteComponentProp(const QString& id)
{
    const NodeId owner = definerOf(*m_doc, id);
    if (!owner)
        return;
    m_doc->breakMerge();
    m_doc->begin(tr("Delete property"));
    std::erase_if(m_doc->edit(owner).componentProps, [&](const ComponentProp& d) { return d.id == id; });
    std::vector<NodeId> refs, values;
    m_doc->walk(m_doc->root(), [&](const Node& n) {
        if (std::find(n.p.propRefs.cbegin(), n.p.propRefs.cend(), id) != n.p.propRefs.cend())
            refs.push_back(n.id);
        if (n.p.propValues.contains(id))
            values.push_back(n.id);
        return true;
    });
    for (NodeId n : refs) {
        Props& p = m_doc->edit(n);
        for (auto it = p.propRefs.begin(); it != p.propRefs.end();)
            it = it.value() == id ? p.propRefs.erase(it) : std::next(it);
    }
    for (NodeId n : values)
        m_doc->edit(n).propValues.remove(id);
    m_doc->commit();
}

void Editor::linkComponentProp(const QString& field, const QString& propId)
{
    auto sel = selected();
    if (sel.empty())
        return;
    m_doc->breakMerge();
    m_doc->begin(propId.isEmpty() ? tr("Unlink property") : tr("Link property"));
    for (NodeId id : sel) {
        if (m_doc->isInsideInstance(id))
            continue;
        if (propId.isEmpty()) {
            if (m_doc->at(id).p.propRefs.contains(field))
                m_doc->edit(id).propRefs.remove(field);
            continue;
        }
        bool ok = false;
        for (const ComponentProp* d : m_doc->propDefs(m_doc->componentOf(id)))
            ok |= d->id == propId && d->type == typeForField(field);
        if (ok)
            m_doc->edit(id).propRefs.insert(field, propId);
    }
    m_doc->commit();
}

void Editor::setInstanceProp(const QString& propId, const QVariant& value)
{
    auto sel = selected();
    if (sel.empty())
        return;
    const QString v = value.typeId() == QMetaType::Bool ? (value.toBool() ? QStringLiteral("true") : QStringLiteral("false"))
                                                        : value.toString();
    m_doc->breakMerge();
    m_doc->begin(tr("Set property"));
    for (NodeId id : sel) {
        if (m_doc->at(id).type() != NodeType::Instance || m_doc->at(id).p.propValues.value(propId) == v)
            continue;
        m_doc->edit(id).propValues.insert(propId, v);
    }
    m_doc->commit();
}

} // namespace drav
