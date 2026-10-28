#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

namespace drav::zip {

// Entries of a zip archive (stored or deflated; no zip64), by path. Empty on a malformed file.
QHash<QString, QByteArray> read(const QByteArray& archive);

// Raw deflate (no zlib header) and zstd frames. Empty on error.
QByteArray inflateRaw(const QByteArray& data, qsizetype sizeHint = 0);
QByteArray zstd(const QByteArray& data);

} // namespace drav::zip
