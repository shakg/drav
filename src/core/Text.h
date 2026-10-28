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

// ---- rich text ----
// Replaces `removed` characters at `pos` with `insert`, shifting style runs and paragraph
// attributes. Inserted text takes the style of the character before it.
void splice(Props& p, int pos, int removed, const QString& insert);
// Sets the fields in `mask` (TextRunField) from `values` on [start, end).
void styleRange(Props& p, int start, int end, std::uint32_t mask, const TextRun& values);
// Removes the fields in `mask` from runs overlapping [start, end) (they fall back to the layer style).
void clearRange(Props& p, int start, int end, std::uint32_t mask);
// Clamps, sorts and merges runs; trims paragraph attributes to the paragraph count.
void normalize(Props& p);
// Effective character style at an index (run fields applied over the layer's style and fill).
TextRun styleAt(const Props& p, int index);
// Paragraph index range [first, last] touched by [start, end).
std::pair<int, int> paragraphRange(const QString& text, int start, int end);
void setParagraphs(Props& p, int first, int last, const ParagraphAttr& attr);

// The string laid out for a text layer differs from its characters (list markers, paragraph
// spacing lines). Maps each character index (0..size) to its index in the laid-out string.
std::vector<int> displayMap(const Props& p);
int fromDisplay(const std::vector<int>& map, int display);

// Variable axes of the typeface used for a family: {tag, min, max, default}.
struct Axis {
    QString tag;
    float min, max, def;
};
std::vector<Axis> axes(const QString& family, int weight, bool italic);
bool hasFamily(const QString& family);

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
