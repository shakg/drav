// Selection helpers: select same/matching layers, find and replace, bulk rename.

#include "Editor.h"

#include "core/Text.h"

#include <QRegularExpression>

namespace drav {

void Editor::selectSame(const QString& what)
{
    auto sel = selected();
    if (sel.empty())
        return;
    const Props& ref = m_doc->at(sel.front()).p;
    auto paintsKey = [](const std::vector<Paint>& v) {
        std::vector<Paint> out;
        for (Paint p : v)
            if (p.visible)
                out.push_back(p);
        return out;
    };
    auto matches = [&](const Node& n) {
        const Props& p = n.p;
        if (what == QLatin1String("fill"))
            return !p.fills.empty() && (ref.fillStyle.isEmpty() ? paintsKey(p.fills) == paintsKey(ref.fills) : p.fillStyle == ref.fillStyle);
        if (what == QLatin1String("stroke"))
            return !p.strokes.empty() && paintsKey(p.strokes) == paintsKey(ref.strokes) && p.stroke.weight == ref.stroke.weight;
        if (what == QLatin1String("effect"))
            return !p.effects.empty() && p.effects == ref.effects;
        if (what == QLatin1String("font"))
            return p.type == NodeType::Text && sameTypography(p.textStyle, ref.textStyle);
        if (what == QLatin1String("instance"))
            return p.type == NodeType::Instance && p.componentId == ref.componentId;
        if (what == QLatin1String("type"))
            return p.type == ref.type;
        // "matching": same kind of layer that looks the same.
        return p.type == ref.type && paintsKey(p.fills) == paintsKey(ref.fills) && paintsKey(p.strokes) == paintsKey(ref.strokes)
            && p.effects == ref.effects && (p.type != NodeType::Text || sameTypography(p.textStyle, ref.textStyle))
            && (p.type != NodeType::Instance || p.componentId == ref.componentId);
    };
    std::vector<NodeId> ids;
    m_doc->walk(m_doc->currentPage(), [&](const Node& n) {
        if (n.type() != NodeType::Page && n.p.visible && !n.p.locked && matches(n))
            ids.push_back(n.id);
        return true;
    });
    select(ids);
}

QVariantList Editor::findLayers(const QString& query, bool caseSensitive, bool names) const
{
    QVariantList out;
    if (query.isEmpty())
        return out;
    const auto cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    m_doc->walk(m_doc->currentPage(), [&](const Node& n) {
        if (n.type() == NodeType::Page)
            return true;
        if ((n.type() == NodeType::Text && n.p.text.contains(query, cs)) || (names && n.p.name.contains(query, cs)))
            out << qint64(n.id);
        return true;
    });
    return out;
}

int Editor::replaceText(const QString& query, const QString& replacement, bool caseSensitive, bool names)
{
    if (query.isEmpty())
        return 0;
    const auto cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    std::vector<NodeId> ids;
    for (const QVariant& v : findLayers(query, caseSensitive, names))
        ids.push_back(NodeId(v.toLongLong()));
    int count = 0;
    m_doc->breakMerge();
    m_doc->begin(tr("Replace"));
    for (NodeId id : ids) {
        const Props& cur = m_doc->at(id).p;
        if (cur.type == NodeType::Text && cur.text.contains(query, cs)) {
            // Splice each match from the end so character styles stay with the surrounding text.
            Props p = cur;
            std::vector<int> at;
            for (int i = int(p.text.indexOf(query, 0, cs)); i >= 0; i = int(p.text.indexOf(query, i + query.size(), cs)))
                at.push_back(i);
            for (auto it = at.rbegin(); it != at.rend(); ++it)
                text::splice(p, *it, int(query.size()), replacement);
            count += int(at.size());
            m_doc->edit(id) = p;
        }
        if (names && cur.name.contains(query, cs)) {
            QString n = cur.name;
            count += int(n.count(query, cs));
            n.replace(query, replacement, cs);
            if (!n.isEmpty())
                m_doc->edit(id).name = n;
        }
    }
    m_doc->commit();
    setStatus(tr("Replaced %1").arg(count));
    return count;
}

void Editor::renameLayers(const QString& pattern, const QString& find, const QString& replace, int start)
{
    auto sel = selected();
    if (sel.empty())
        return;
    // Number in layer-panel order (top first), like Figma.
    std::sort(sel.begin(), sel.end(), [&](NodeId a, NodeId b) {
        if (m_doc->at(a).parent == m_doc->at(b).parent)
            return m_doc->indexOf(a) > m_doc->indexOf(b);
        return a < b;
    });
    m_doc->breakMerge();
    m_doc->begin(tr("Rename layers"));
    const int n = int(sel.size());
    for (int i = 0; i < n; ++i) {
        const QString old = m_doc->at(sel[i]).p.name;
        QString name = pattern.isEmpty() ? old : pattern;
        if (!pattern.isEmpty()) {
            name.replace(QLatin1String("$&"), old);
            name.replace(QLatin1String("$n"), QString::number(start + i));
            name.replace(QLatin1String("$N"), QString::number(start + n - 1 - i));
        }
        if (!find.isEmpty())
            name.replace(find, replace);
        if (!name.isEmpty() && name != old)
            m_doc->edit(sel[i]).name = name;
    }
    m_doc->commit();
}

} // namespace drav
