#include "Libraries.h"

#include <QCryptographicHash>

#include <functional>

namespace drav::libs {

namespace {

QString key(const QString& libId, NodeId srcId) { return libId + QLatin1Char(':') + QString::number(srcId); }

// remote key -> local node, for everything imported from one library.
QHash<QString, NodeId> remoteIndex(const Document& doc, const QString& libId)
{
    QHash<QString, NodeId> out;
    const QString prefix = libId + QLatin1Char(':');
    doc.walk(doc.root(), [&](const Node& n) {
        if (n.p.remote.startsWith(prefix))
            out.insert(n.p.remote, n.id);
        return true;
    });
    return out;
}

// The top-level importable unit for a component: its component set if it's a variant.
NodeId unitOf(const Document& src, NodeId srcId)
{
    const Node& n = src.at(srcId);
    if (n.type() == NodeType::Component && n.parent && src.at(n.parent).type() == NodeType::ComponentSet)
        return n.parent;
    return srcId;
}

void importStylesAndVariables(Library& lib, const QString& libId, const Library& from)
{
    auto upsert = [&](auto& list, auto item) {
        item.remote = libId;
        for (auto& x : list)
            if (x.id == item.id) {
                x = item;
                return;
            }
        list.push_back(item);
    };
    for (const Style& s : from.styles)
        if (s.remote.isEmpty()) // only the library's own assets, not ones it uses from elsewhere
            upsert(lib.styles, s);
    for (const VarCollection& c : from.collections)
        if (c.remote.isEmpty())
            upsert(lib.collections, c);
    for (const Variable& v : from.variables)
        if (v.remote.isEmpty())
            upsert(lib.variables, v);
}

// Props of a source node as stored locally: ids that point into the library are mapped.
Props localProps(const Props& sp, const QString& libId, NodeId srcId, const QHash<QString, NodeId>& index)
{
    Props p = sp;
    p.remote = key(libId, srcId);
    p.locked = false;
    if (p.componentId)
        p.componentId = index.value(key(libId, p.componentId), kNoNode);
    if (p.sourceId)
        p.sourceId = index.value(key(libId, p.sourceId), kNoNode);
    for (ComponentProp& d : p.componentProps)
        for (auto& x : d.preferred)
            x = index.value(key(libId, x), kNoNode);
    for (ComponentProp& d : p.componentProps)
        if (d.type == ComponentPropType::InstanceSwap)
            d.defaultValue = QString::number(index.value(key(libId, d.defaultValue.toULongLong()), kNoNode));
    for (auto it = p.propValues.begin(); it != p.propValues.end(); ++it)
        if (const NodeId mapped = index.value(key(libId, it.value().toULongLong()), kNoNode))
            it.value() = QString::number(mapped);
    return p;
}

// Components used (through instances) inside a source subtree, as importable units.
void dependencies(const Document& src, NodeId root, std::vector<NodeId>& out)
{
    src.walk(root, [&](const Node& n) {
        if (n.type() == NodeType::Instance && src.exists(n.p.componentId)) {
            const NodeId unit = unitOf(src, n.p.componentId);
            if (unit != unitOf(src, root) && std::find(out.begin(), out.end(), unit) == out.end())
                out.push_back(unit);
        }
        for (const ComponentProp& d : n.p.componentProps)
            for (auto x : d.preferred)
                if (src.exists(x) && src.at(x).type() == NodeType::Component) {
                    const NodeId unit = unitOf(src, x);
                    if (unit != unitOf(src, root) && std::find(out.begin(), out.end(), unit) == out.end())
                        out.push_back(unit);
                }
        return true;
    });
}

// Creates or re-syncs the local copy of a source subtree under `parent`.
NodeId syncTree(Document& doc, const QString& libId, const Document& src, NodeId srcId, NodeId parent, int index,
    QHash<QString, NodeId>& idx)
{
    const QString k = key(libId, srcId);
    NodeId local = idx.value(k, kNoNode);
    const Props want = localProps(src.at(srcId).p, libId, srcId, idx);
    for (const auto* list : {&want.fills, &want.strokes})
        for (const Paint& f : *list)
            if (!f.imageHash.isEmpty() && !doc.images().contains(f.imageHash) && src.images().contains(f.imageHash))
                doc.addImage(src.images().value(f.imageHash));
    if (!local || !doc.exists(local)) {
        local = doc.create(want, parent, index);
        idx.insert(k, local);
    } else {
        Props next = want;
        next.transform = parent == doc.at(local).parent && doc.at(parent).type() == NodeType::Page ? doc.at(local).p.transform : want.transform;
        if (!(doc.at(local).p == next))
            doc.edit(local) = next;
        if (doc.at(local).parent != parent)
            doc.move(local, parent, index);
    }
    std::vector<NodeId> keep;
    for (NodeId c : src.at(srcId).children)
        keep.push_back(syncTree(doc, libId, src, c, local, -1, idx));
    // Drop children the library removed; order as in the library.
    const auto kids = doc.at(local).children;
    for (NodeId c : kids)
        if (std::find(keep.begin(), keep.end(), c) == keep.end())
            doc.remove(c);
    for (int i = 0; i < int(keep.size()); ++i)
        if (doc.indexOf(keep[i]) != i)
            doc.move(keep[i], local, i);
    return local;
}

// Second pass: links that pointed at nodes imported later in the same pass.
void fixLinks(Document& doc, const QString& libId, const Document& src, NodeId local, const QHash<QString, NodeId>& idx)
{
    doc.walk(local, [&](const Node& n) {
        const NodeId srcId = n.p.remote.section(QLatin1Char(':'), 1).toULongLong();
        if (!src.exists(srcId))
            return true;
        const Props want = localProps(src.at(srcId).p, libId, srcId, idx);
        if (want.componentId != n.p.componentId || want.sourceId != n.p.sourceId || want.componentProps != n.p.componentProps) {
            Props p = n.p;
            p.componentId = want.componentId;
            p.sourceId = want.sourceId;
            p.componentProps = want.componentProps;
            doc.edit(n.id) = p;
        }
        return true;
    });
}

NodeId importUnit(Document& doc, const QString& libId, const Document& src, NodeId unit, QHash<QString, NodeId>& idx,
    std::vector<NodeId>& visiting)
{
    if (std::find(visiting.begin(), visiting.end(), unit) != visiting.end())
        return idx.value(key(libId, unit), kNoNode); // dependency cycle
    visiting.push_back(unit);
    std::vector<NodeId> deps;
    dependencies(src, unit, deps);
    for (NodeId d : deps)
        if (!idx.contains(key(libId, d)))
            importUnit(doc, libId, src, d, idx, visiting);
    NodeId page = libraryPage(doc, libId);
    if (!page) {
        Props pp;
        pp.type = NodeType::Page;
        pp.name = QStringLiteral("Library");
        pp.libraryPage = libId;
        page = doc.create(pp, doc.root());
    }
    const NodeId local = syncTree(doc, libId, src, unit, page, -1, idx);
    fixLinks(doc, libId, src, local, idx);
    visiting.pop_back();
    return local;
}

} // namespace

QString fileHash(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

NodeId libraryPage(const Document& doc, const QString& libId)
{
    for (NodeId p : doc.pages())
        if (doc.at(p).p.libraryPage == libId)
            return p;
    return kNoNode;
}

QString link(Document& doc, const QString& path, const QString& name, const QString& hash, const Document& src)
{
    QString id;
    for (const LibraryLink& l : doc.library().links)
        if (l.path == path)
            id = l.id;
    Library& lib = doc.editLibrary();
    if (id.isEmpty()) {
        id = Library::newId();
        lib.links.push_back({id, path, name, hash});
    } else {
        for (LibraryLink& l : lib.links)
            if (l.id == id)
                l = {id, path, name, hash};
    }
    importStylesAndVariables(lib, id, src.library());
    return id;
}

NodeId importComponent(Document& doc, const QString& libId, const Document& src, NodeId srcId)
{
    if (!src.exists(srcId) || src.at(srcId).type() != NodeType::Component)
        return kNoNode;
    QHash<QString, NodeId> idx = remoteIndex(doc, libId);
    if (const NodeId existing = idx.value(key(libId, srcId), kNoNode); existing && doc.isAttached(existing))
        return existing;
    std::vector<NodeId> visiting;
    importUnit(doc, libId, src, unitOf(src, srcId), idx, visiting);
    return idx.value(key(libId, srcId), kNoNode);
}

void update(Document& doc, const QString& libId, const Document& src, const QString& hash)
{
    Library& lib = doc.editLibrary();
    for (LibraryLink& l : lib.links)
        if (l.id == libId)
            l.hash = hash;
    importStylesAndVariables(lib, libId, src.library());
    // Re-sync every imported unit that still exists in the library.
    QHash<QString, NodeId> idx = remoteIndex(doc, libId);
    const NodeId page = libraryPage(doc, libId);
    if (!page)
        return;
    const auto units = doc.at(page).children;
    std::vector<NodeId> visiting;
    for (NodeId u : units) {
        const NodeId srcId = doc.at(u).p.remote.section(QLatin1Char(':'), 1).toULongLong();
        if (src.exists(srcId) && src.isAttached(srcId))
            importUnit(doc, libId, src, srcId, idx, visiting);
    }
}

void unlink(Document& doc, const QString& libId)
{
    Library& lib = doc.editLibrary();
    std::erase_if(lib.links, [&](const LibraryLink& l) { return l.id == libId; });
    for (Style& s : lib.styles)
        if (s.remote == libId)
            s.remote.clear();
    for (VarCollection& c : lib.collections)
        if (c.remote == libId)
            c.remote.clear();
    for (Variable& v : lib.variables)
        if (v.remote == libId)
            v.remote.clear();
    const QHash<QString, NodeId> idx = remoteIndex(doc, libId);
    for (NodeId id : idx)
        doc.edit(id).remote.clear();
    if (const NodeId page = libraryPage(doc, libId))
        doc.edit(page).libraryPage.clear(); // the copies stay, on a visible page
}

} // namespace drav::libs
