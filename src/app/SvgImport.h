#pragma once

#include "core/Document.h"

namespace drav::svg {

// Imports SVG markup as a frame of editable layers under `parent` (inside an open transaction).
// Returns the frame id, or kNoNode when the data is not SVG.
NodeId import(Document& doc, const QByteArray& data, NodeId parent, const QString& name);

} // namespace drav::svg
