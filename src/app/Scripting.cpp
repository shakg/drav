#include "Scripting.h"

#include "Editor.h"

#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QStandardPaths>
#include <QTextStream>

#include <condition_variable>
#include <mutex>
#include <thread>

namespace drav {

ScriptApi::ScriptApi(Editor& ed)
    : m_ed(ed)
{
}

QVariantList ScriptApi::selection() const
{
    QVariantList out;
    for (NodeId id : m_ed.doc().selection())
        out << qint64(id);
    return out;
}

void ScriptApi::select(const QVariantList& ids)
{
    std::vector<NodeId> v;
    for (const QVariant& x : ids)
        if (m_ed.doc().isAttached(NodeId(x.toLongLong())))
            v.push_back(NodeId(x.toLongLong()));
    m_ed.select(v);
}

qint64 ScriptApi::page() const { return qint64(m_ed.doc().currentPage()); }

QVariantList ScriptApi::pages() const
{
    QVariantList out;
    for (const QVariant& p : m_ed.pages())
        out << p.toMap().value(QStringLiteral("id"));
    return out;
}

QVariantList ScriptApi::children(qint64 id) const
{
    QVariantList out;
    if (m_ed.doc().isAttached(NodeId(id)))
        for (NodeId c : m_ed.doc().at(NodeId(id)).children)
            out << qint64(c);
    return out;
}

qint64 ScriptApi::parent(qint64 id) const
{
    return m_ed.doc().isAttached(NodeId(id)) ? qint64(m_ed.doc().at(NodeId(id)).parent) : 0;
}

QVariantMap ScriptApi::get(qint64 id) const
{
    if (!m_ed.doc().isAttached(NodeId(id)))
        return {};
    // The inspector's view of a single layer.
    const auto saved = m_ed.doc().selection();
    Document& d = const_cast<Document&>(m_ed.doc());
    QSignalBlocker block(&d);
    d.setSelection({NodeId(id)});
    QVariantMap m = m_ed.props();
    d.setSelection(saved);
    m.remove(QStringLiteral("count"));
    m.remove(QStringLiteral("pageBackground"));
    m.remove(QStringLiteral("sameType"));
    return m;
}

void ScriptApi::set(qint64 id, const QString& key, const QVariant& value)
{
    if (!m_ed.doc().isAttached(NodeId(id)))
        return;
    const auto saved = m_ed.doc().selection();
    m_ed.doc().setSelection({NodeId(id)});
    m_ed.setProp(key, value);
    m_ed.doc().setSelection(saved);
}

qint64 ScriptApi::create(const QString& type, qint64 parent, const QVariantMap& props)
{
    const NodeType t = nodeTypeFromName(type.toUpper());
    if (t == NodeType::Document || t == NodeType::Page || t == NodeType::Instance)
        return 0;
    NodeId p = NodeId(parent);
    if (!m_ed.doc().isAttached(p) || !isContainer(m_ed.doc().at(p).type()))
        p = m_ed.doc().currentPage();
    Props pr = m_ed.defaultProps(t);
    Document& d = m_ed.doc();
    d.begin(QObject::tr("Script"));
    const NodeId id = d.create(pr, p);
    d.commit();
    for (auto it = props.begin(); it != props.end(); ++it)
        set(qint64(id), it.key(), it.value());
    return qint64(id);
}

void ScriptApi::remove(qint64 id)
{
    Document& d = m_ed.doc();
    if (!d.isAttached(NodeId(id)) || d.at(NodeId(id)).type() == NodeType::Page || NodeId(id) == d.root())
        return;
    d.begin(QObject::tr("Script"));
    d.remove(NodeId(id));
    d.commit();
}

QVariantList ScriptApi::find(const QString& name) const
{
    QVariantList out;
    m_ed.doc().walk(m_ed.doc().currentPage(), [&](const Node& n) {
        if (n.type() != NodeType::Page && n.p.name.contains(name, Qt::CaseInsensitive))
            out << qint64(n.id);
        return true;
    });
    return out;
}

QString ScriptApi::colorProfile() const { return m_ed.colorProfile(); }

void ScriptApi::setColorProfile(const QString& profile, bool convert) { m_ed.setColorProfile(profile, convert); }

void ScriptApi::notify(const QString& message)
{
    m_ed.setStatus(message);
    output << message;
}

void ScriptApi::log(const QVariant& value)
{
    output << value.toString();
}

namespace scripting {

QString run(Editor& ed, const QString& code, bool* ok, int timeoutMs)
{
    QJSEngine engine; // deliberately bare: no XMLHttpRequest, no file system
    ScriptApi api(ed);
    QJSEngine::setObjectOwnership(&api, QJSEngine::CppOwnership);
    engine.globalObject().setProperty(QStringLiteral("drav"), engine.newQObject(&api));
    engine.evaluate(QStringLiteral("function print() { drav.log(Array.prototype.map.call(arguments, "
                                   "function (a) { return typeof a === 'object' ? JSON.stringify(a) : String(a); }).join(' ')); }"));
    // A runaway script is stopped after the timeout.
    std::mutex m;
    std::condition_variable cv;
    bool finished = false;
    std::thread watchdog([&] {
        std::unique_lock lock(m);
        if (!cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return finished; }))
            engine.setInterrupted(true);
    });
    // Everything the script does is one undo step.
    Document& d = ed.doc();
    d.breakMerge();
    d.begin(QObject::tr("Run script"));
    const QJSValue r = engine.evaluate(code, QStringLiteral("script.js"));
    {
        std::lock_guard lock(m);
        finished = true;
    }
    cv.notify_one();
    watchdog.join();
    const bool failed = r.isError() || engine.isInterrupted();
    if (failed) {
        d.cancel();
    } else {
        d.commit();
    }
    d.breakMerge();
    if (ok)
        *ok = !failed;
    if (engine.isInterrupted())
        return QObject::tr("Stopped: the script ran longer than %1 seconds").arg(timeoutMs / 1000.0);
    if (failed)
        return QStringLiteral("Line %1: %2").arg(r.property(QStringLiteral("lineNumber")).toInt()).arg(r.toString());
    QStringList out = api.output;
    if (!r.isUndefined() && !r.isNull())
        out << r.toString();
    return out.join(QLatin1Char('\n'));
}

QString pluginsFolder()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/plugins");
    QDir().mkpath(dir);
    return dir;
}

std::vector<Plugin> plugins()
{
    std::vector<Plugin> out;
    QDir dir(pluginsFolder());
    for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("*.js")}, QDir::Files, QDir::Name)) {
        Plugin p{fi.absoluteFilePath(), fi.completeBaseName(), {}};
        QFile f(fi.absoluteFilePath());
        if (f.open(QIODevice::ReadOnly)) {
            QTextStream in(&f);
            for (int i = 0; i < 10 && !in.atEnd(); ++i) {
                const QString line = in.readLine().trimmed();
                if (line.startsWith(QLatin1String("// name:")))
                    p.name = line.mid(8).trimmed();
                else if (line.startsWith(QLatin1String("// description:")))
                    p.description = line.mid(15).trimmed();
            }
        }
        out.push_back(p);
    }
    return out;
}

} // namespace scripting

QString Editor::runScript(const QString& code)
{
    bool ok = true;
    const QString out = scripting::run(*this, code, &ok);
    if (!ok)
        setStatus(out);
    return out;
}

QVariantList Editor::plugins() const
{
    QVariantList out;
    for (const Plugin& p : scripting::plugins())
        out << QVariantMap{{QStringLiteral("path"), p.path}, {QStringLiteral("name"), p.name},
            {QStringLiteral("description"), p.description}};
    return out;
}

QString Editor::runPlugin(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return tr("Cannot read %1").arg(path);
    return runScript(QString::fromUtf8(f.readAll()));
}

QString Editor::pluginsFolder() const { return scripting::pluginsFolder(); }

} // namespace drav
