#pragma once

#include "Node.h"

#include <modules/skparagraph/include/FontCollection.h>
#include <modules/skparagraph/include/Paragraph.h>

#include <include/core/SkFont.h>

#include <QStringList>

#include <memory>

namespace drav::text {

// Registers bundled fonts and the system font manager. Safe to call repeatedly.
void init(const QStringList& extraFontFiles = {});
sk_sp<skia::textlayout::FontCollection> fonts();
sk_sp<SkFontMgr> fontMgr();
QStringList families();
// Font for editor chrome drawn on the canvas (labels, measurements).
SkFont uiFont(float size, bool bold = false);

QString applyCase(const QString& s, TextCase c);

// Builds a paragraph from a text node and lays it out at the node's width
// (or unbounded for auto-width). `color` is used when the text has a single solid fill.
std::unique_ptr<skia::textlayout::Paragraph> build(const Props& p, float layoutWidth, SkColor4f color = SkColors::kBlack);

struct Metrics {
    float width = 0;  // natural width (max intrinsic)
    float height = 0; // laid-out height at the given width
    float firstBaseline = 0;
};
// layoutWidth < 0 means unbounded.
Metrics measure(const Props& p, float layoutWidth);

// Applies the node's auto-resize rule to width/height. Returns true if size changed.
bool autoSize(Props& p);

// Vertical offset of the paragraph inside the text box, from alignV.
float verticalOffset(const Props& p, float paragraphHeight);

} // namespace drav::text
