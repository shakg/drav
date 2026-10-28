#pragma once

#include "Node.h"

#include <include/core/SkPath.h>

namespace drav {

class Document;

// Fill geometry of a node in its local space.
SkPath fillPath(const Document& doc, NodeId id);
// Stroke geometry: the fill path, except for vector networks (all segments, including open ones).
SkPath strokePath(const Document& doc, NodeId id);

// Rounded rectangle with Figma-style corner smoothing (smoothing 0 = circular corners).
SkPath smoothRect(float w, float h, const CornerRadii& r, float smoothing);

// Outline of the stroke as a filled path in local space (for hit testing and outline stroke).
SkPath strokeOutline(const Props& p, const SkPath& fill);

// Converts an SkPath (local space) to editable contours.
VectorData vectorFromPath(const SkPath& path);

} // namespace drav
