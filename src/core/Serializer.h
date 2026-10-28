#pragma once

#include "Library.h"
#include "Node.h"

#include <QByteArray>
#include <QCborMap>

namespace drav {

class Document;

namespace io {

// 2: styles, variables, bindings (library on the document node).
constexpr int kFormatVersion = 2;

// .drav file: "DRAV" magic, then a CBOR map with nodes and the image store.
QByteArray save(const Document& doc);
bool load(Document& doc, const QByteArray& data, QString* error = nullptr);

QCborMap propsToCbor(const Props& p);
Props propsFromCbor(const QCborMap& m);


QCborMap libraryToCbor(const Library& lib);
Library libraryFromCbor(const QCborMap& m);

// Clipboard: subtrees of the given nodes (plus referenced images and main components' ids).
QByteArray copy(const Document& doc, const std::vector<NodeId>& ids);
// Inserts copied nodes under `parent` (inside an open transaction). Returns the new top-level ids.
std::vector<NodeId> paste(Document& doc, const QByteArray& data, NodeId parent, int index = -1);

inline const char* kClipboardMime = "application/x-drav-nodes";

} // namespace io
} // namespace drav
