#pragma once

#include "core/Document.h"

#include <QByteArray>

namespace drav::exporter {

enum class Format { PNG, JPG, WEBP, SVG, PDF };
Format formatFromName(const QString& s);
QString extension(Format f);

// Renders a node (in its own bounds, including effects) or a whole page when id is a page.
QByteArray exportNode(const Document& doc, NodeId id, Format format, float scale = 1.f);
// Several nodes as pages of one PDF.
QByteArray exportPdf(const Document& doc, const std::vector<NodeId>& ids);

} // namespace drav::exporter
