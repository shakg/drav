#pragma once

#include "core/Node.h"

#include <QJSValue>
#include <QObject>
#include <QStringList>
#include <QVariant>

namespace drav {

class Editor;

// The `drav` object scripts and plugins see. Plain QJSEngine: no network, no file access.
// Node properties use the inspector's keys (see Editor::props()).
class ScriptApi : public QObject {
    Q_OBJECT
public:
    explicit ScriptApi(Editor& ed);

    Q_INVOKABLE QVariantList selection() const;
    Q_INVOKABLE void select(const QVariantList& ids);
    Q_INVOKABLE qint64 page() const;
    Q_INVOKABLE QVariantList pages() const;
    Q_INVOKABLE QVariantList children(qint64 id) const;
    Q_INVOKABLE qint64 parent(qint64 id) const;
    Q_INVOKABLE QVariantMap get(qint64 id) const;
    Q_INVOKABLE void set(qint64 id, const QString& key, const QVariant& value);
    // type: "FRAME", "RECTANGLE", "ELLIPSE", "TEXT", ... Returns the new layer's id.
    Q_INVOKABLE qint64 create(const QString& type, qint64 parent, const QVariantMap& props = {});
    Q_INVOKABLE void remove(qint64 id);
    Q_INVOKABLE QVariantList find(const QString& name) const; // layers on the page whose name contains `name`
    Q_INVOKABLE void notify(const QString& message);
    Q_INVOKABLE void log(const QVariant& value);

    QStringList output;

private:
    Editor& m_ed;
};

struct Plugin {
    QString path;
    QString name;
    QString description;
};

namespace scripting {
// Runs `code` with `drav` in scope as one undo step. Returns printed output, or the error.
QString run(Editor& ed, const QString& code, bool* ok = nullptr, int timeoutMs = 10000);
// Plugins: *.js files in the plugins folder. A leading "// name: ..." / "// description: ..." names them.
QString pluginsFolder();
std::vector<Plugin> plugins();
} // namespace scripting

} // namespace drav
