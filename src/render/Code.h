#pragma once

#include "core/Document.h"

#include <QString>

namespace drav::code {

// CSS for a layer: size and position, fills, strokes, radii, effects, typography and auto layout
// (as flexbox). Values bound to variables are written as var(--name, fallback).
QString css(const Document& doc, NodeId id);

// CSS custom property name for a variable ("Brand/Primary" -> "--brand-primary").
QString cssVarName(const QString& variableName);

} // namespace drav::code
