#pragma once

#include "Library.h"
#include "Node.h"

#include <QStringList>

#include <functional>
#include <optional>

namespace drav {

class Document;

// Variable resolution and style application. Nodes keep style ids and variable bindings; the
// document's derived-state pass writes the resolved values into the node's ordinary properties,
// so rendering, layout and export never look at styles or variables.
namespace vars {

// Scalar fields that can be bound through Props::bindings (paint and effect colors bind through
// their own colorVar). Keys match the inspector property names.
bool fieldType(const QString& key, VarType* type);
QStringList bindableFields();

using ModeFn = std::function<QString(const QString& collection)>;
// Follows aliases; a missing or unknown mode falls back to the collection's default.
std::optional<VarValue> resolve(const Library& lib, const QString& varId, const ModeFn& mode, VarType* type = nullptr);
// Mode for a collection at a node: the nearest ancestor-or-self with an explicit choice, else default.
QString modeFor(const Document& doc, NodeId id, const QString& collection);

bool hasRefs(const Props& p);
// Applies styles and bindings to `p` (the props of node `id`). A bound width/height is returned
// through `size` instead of being written, because resizing has to go through Document::resize.
void apply(const Document& doc, NodeId id, Props& p, std::optional<SkSize>* size);

// Makes fields of an instance node that are driven by the same style or variable as its main
// equal to the main's values, so context-dependent resolution isn't mistaken for an override.
void alignDriven(Props& node, const Props& main);

} // namespace vars
} // namespace drav
