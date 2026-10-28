#pragma once

#include "Document.h"

#include <QStringList>
#include <QVariantMap>

namespace drav::fig {

// Figma .fig import. A .fig file is either a zip (canvas.fig, images/<hash>, meta.json) or the
// bare canvas: "fig-kiwi", a version, then length-prefixed chunks — a compressed kiwi schema and
// a compressed message encoded with it (deflate or zstd). The schema travels with the file, so
// decoding is generic; mapping the decoded nodes onto drav's model uses Figma's field names.

// ---- kiwi (exposed for tests) ----
struct Schema {
    struct Field {
        QString name;
        int type = 0; // < 0: primitive (~type indexes bool, byte, int, uint, float, string, int64, uint64)
        bool array = false;
        quint32 value = 0; // field id (messages) or enum value
    };
    struct Def {
        QString name;
        int kind = 0; // 0 enum, 1 struct, 2 message
        std::vector<Field> fields;
    };
    std::vector<Def> defs;
};
bool parseSchema(const QByteArray& data, Schema* out);
// Decodes a message of type `root`. Byte arrays become QByteArray, enums their names.
QVariantMap decode(const Schema& schema, const QByteArray& data, const QString& root, QString* error = nullptr);
// Splits a .fig file into the decompressed schema and message, plus zip image entries by hash.
bool unpack(const QByteArray& file, QByteArray* schema, QByteArray* message, QHash<QString, QByteArray>* images,
    QString* error);

// ---- import ----
struct Result {
    std::vector<NodeId> pages;
    int layers = 0;
    QStringList warnings;
};
// Adds the file's pages to the document (inside an open transaction).
bool import(Document& doc, const QByteArray& file, Result* result, QString* error);

} // namespace drav::fig
