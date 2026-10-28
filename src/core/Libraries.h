#pragma once

#include "Document.h"

namespace drav::libs {

// Linked libraries: other .drav files whose styles, variables and components this document uses.
// Styles and variables are imported with their original ids (so references inside imported
// layers resolve unchanged) and marked with the library id. Components are copied on first use
// into a hidden page per library; each copied node remembers "<library>:<source node>" so a
// later update can re-sync it in place, which the document's instances then follow.
// All functions run inside an open transaction.

QString fileHash(const QByteArray& data);

// Adds (or refreshes) a link and imports the library's styles and variables. Returns its id.
QString link(Document& doc, const QString& path, const QString& name, const QString& hash, const Document& src);

// Local copy of a library component (importing it and the components it uses). Returns kNoNode
// if the source node is not a component.
NodeId importComponent(Document& doc, const QString& libId, const Document& src, NodeId srcId);

// Re-imports styles, variables and every imported component from the library's new contents.
void update(Document& doc, const QString& libId, const Document& src, const QString& hash);

// Forgets the link; imported styles, variables and components become local.
void unlink(Document& doc, const QString& libId);

NodeId libraryPage(const Document& doc, const QString& libId);

} // namespace drav::libs
