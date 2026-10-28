#pragma once

#include "Types.h"

#include <include/core/SkColorSpace.h>

#include <functional>

namespace drav {

struct Props;
struct Library;

namespace color {

// The Skia color space for a profile; nullptr means sRGB.
sk_sp<SkColorSpace> space(ColorProfile p);
// Same appearance in another profile, clamped to its gamut. Alpha is kept.
SkColor4f convert(SkColor4f c, ColorProfile from, ColorProfile to);
const char* profileName(ColorProfile p); // "srgb", "display-p3"
ColorProfile profileFromName(const QString& s);
// Every document color in a node or library: paints, gradient stops, effects, grids, text runs,
// page background, color variables.
void forEachColor(Props& p, const std::function<void(SkColor4f&)>& f);
void forEachColor(Library& lib, const std::function<void(SkColor4f&)>& f);

} // namespace color
} // namespace drav
