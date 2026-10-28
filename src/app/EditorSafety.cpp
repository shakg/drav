// Autosave and crash recovery, version history and recent files. Everything stays on disk in the
// app data folder; nothing leaves the machine.

#include "Editor.h"
#include "LayerModel.h"

#include "core/Serializer.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

namespace drav {

namespace {

QString dataDir(const QString& sub)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1Char('/') + sub;
    QDir().mkpath(dir);
    return dir;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return f.commit();
}

} // namespace

void Editor::newSession()
{
    m_sessionId = QUuid::createUuid().toString(QUuid::Id128).left(12);
}

// ---------------------------------------------------------------- recent files

QStringList Editor::recentFiles() const
{
    QStringList out;
    for (const QString& f : QSettings().value(QStringLiteral("recentFiles")).toStringList())
        if (QFileInfo::exists(f))
            out << f;
    return out;
}

void Editor::addRecent(const QString& path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    QStringList l = QSettings().value(QStringLiteral("recentFiles")).toStringList();
    l.removeAll(abs);
    l.prepend(abs);
    while (l.size() > 10)
        l.removeLast();
    QSettings().setValue(QStringLiteral("recentFiles"), l);
    emit recentFilesChanged();
}

void Editor::clearRecentFiles()
{
    QSettings().remove(QStringLiteral("recentFiles"));
    emit recentFilesChanged();
}

// ---------------------------------------------------------------- recovery

QString Editor::recoveryPath() const
{
    return dataDir(QStringLiteral("recovery")) + QLatin1Char('/') + m_sessionId + QStringLiteral(".drav");
}

void Editor::autosaveNow()
{
    const QString path = recoveryPath();
    if (!writeFile(path, io::save(*m_doc)))
        return;
    const QJsonObject meta{{QStringLiteral("original"), m_fileUrl.isEmpty() ? QString() : m_fileUrl.toLocalFile()},
        {QStringLiteral("name"), fileName()}, {QStringLiteral("time"), QDateTime::currentDateTime().toString(Qt::ISODate)}};
    writeFile(path + QStringLiteral(".json"), QJsonDocument(meta).toJson());
}

QVariantList Editor::recoveryFiles() const
{
    QVariantList out;
    const QDir dir(dataDir(QStringLiteral("recovery")));
    const QString own = QFileInfo(recoveryPath()).fileName();
    for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("*.drav")}, QDir::Files, QDir::Time)) {
        if (fi.fileName() == own)
            continue; // this session's own file
        QFile mf(fi.absoluteFilePath() + QStringLiteral(".json"));
        QJsonObject meta;
        if (mf.open(QIODevice::ReadOnly))
            meta = QJsonDocument::fromJson(mf.readAll()).object();
        out << QVariantMap{{QStringLiteral("path"), fi.absoluteFilePath()},
            {QStringLiteral("original"), meta.value(QStringLiteral("original")).toString()},
            {QStringLiteral("name"), meta.value(QStringLiteral("name")).toString(fi.baseName())},
            {QStringLiteral("time"), fi.lastModified()}};
    }
    return out;
}

bool Editor::recover(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = f.readAll();
    QFile mf(path + QStringLiteral(".json"));
    QString original;
    if (mf.open(QIODevice::ReadOnly))
        original = QJsonDocument::fromJson(mf.readAll()).object().value(QStringLiteral("original")).toString();
    setTool(QStringLiteral("select"));
    m_renderer->clear();
    QString err;
    if (!io::load(*m_doc, data, &err)) {
        setStatus(err);
        return false;
    }
    m_fileUrl = original.isEmpty() ? QUrl() : QUrl::fromLocalFile(original);
    m_doc->markDirty(); // recovered work is unsaved until the user saves it
    discardRecovery(path);
    newSession();
    autosaveNow();
    m_contentDirty = true;
    emit fileChanged();
    emit componentsChanged();
    m_layers->sync();
    zoomToFit();
    setStatus(tr("Recovered unsaved changes"));
    return true;
}

void Editor::discardRecovery(const QString& path)
{
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".json"));
}

void Editor::discardChanges()
{
    discardRecovery(recoveryPath());
}

// ---------------------------------------------------------------- versions

QString Editor::versionsDir() const
{
    // Per saved file (by path); unsaved documents keep versions per session.
    const QString key = m_fileUrl.isEmpty()
        ? m_sessionId
        : QString::fromLatin1(QCryptographicHash::hash(QFileInfo(m_fileUrl.toLocalFile()).absoluteFilePath().toUtf8(),
                                  QCryptographicHash::Sha1)
                                  .toHex()
                                  .left(16));
    return dataDir(QStringLiteral("versions/") + key);
}

bool Editor::saveVersion(const QString& name)
{
    const QString label = name.trimmed().isEmpty() ? tr("Version") : name.trimmed();
    const QString file = versionsDir() + QLatin1Char('/') + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))
        + QLatin1Char('-') + QString::fromLatin1(QUrl::toPercentEncoding(label)) + QStringLiteral(".drav");
    if (!writeFile(file, io::save(*m_doc)))
        return false;
    // Automatic versions are capped; named ones are kept.
    const QVariantList all = versions();
    int autos = 0;
    for (const QVariant& v : all)
        if (v.toMap().value(QStringLiteral("name")).toString() == tr("Autosave") && ++autos > 50)
            QFile::remove(v.toMap().value(QStringLiteral("path")).toString());
    emit versionsChanged();
    setStatus(tr("Saved version \"%1\"").arg(label));
    return true;
}

QVariantList Editor::versions() const
{
    QVariantList out;
    const QDir dir(versionsDir());
    QStringList files = dir.entryList({QStringLiteral("*.drav")}, QDir::Files, QDir::Name);
    std::reverse(files.begin(), files.end());
    for (const QString& f : files) {
        // yyyyMMdd-HHmmss-zzz-<name>.drav
        const QString stem = f.chopped(5);
        const QDateTime time = QDateTime::fromString(stem.left(19), QStringLiteral("yyyyMMdd-HHmmss-zzz"));
        out << QVariantMap{{QStringLiteral("path"), dir.absoluteFilePath(f)},
            {QStringLiteral("name"), QUrl::fromPercentEncoding(stem.mid(20).toLatin1())}, {QStringLiteral("time"), time}};
    }
    return out;
}

bool Editor::restoreVersion(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = f.readAll();
    saveVersion(tr("Before restore")); // so restoring is reversible
    const QUrl url = m_fileUrl;
    setTool(QStringLiteral("select"));
    m_renderer->clear();
    QString err;
    if (!io::load(*m_doc, data, &err)) {
        setStatus(err);
        return false;
    }
    m_fileUrl = url;
    m_doc->markDirty();
    m_contentDirty = true;
    emit fileChanged();
    emit componentsChanged();
    m_layers->sync();
    requestUpdate();
    setStatus(tr("Restored version"));
    return true;
}

} // namespace drav
