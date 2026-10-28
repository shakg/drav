#pragma once

#include "core/Document.h"

#include <include/core/SkImage.h>
#include <include/core/SkPicture.h>
#include <modules/skparagraph/include/Paragraph.h>

#include <QHash>

#include <memory>
#include <unordered_map>

class SkCanvas;
class SkPaint;

namespace drav {

// Draws document nodes with Skia. Container subtrees are recorded into SkPictures keyed by the
// document's subtree version, so unchanged content replays without re-recording.
class Renderer {
public:
    explicit Renderer(const Document& doc);

    struct Options {
        bool outlines = false; // wireframe view
        bool showGrids = false; // layout grids are an editor aid, not exported
    };
    void setOptions(const Options& o);
    const Options& options() const { return m_opts; }

    // Page content in page coordinates.
    sk_sp<SkPicture> recordPage(NodeId page);
    // Draws a node including its own transform (relative to its parent).
    void drawNode(SkCanvas* c, NodeId id, bool useCache = true);
    // Draws a node in its own local space (no own transform), as used by exports.
    void drawNodeLocal(SkCanvas* c, NodeId id);

    // Laid-out paragraph for a text node (cached by node version).
    skia::textlayout::Paragraph* paragraph(NodeId id);

    sk_sp<SkImage> image(const QString& hash);
    void clear();

    static void registerCodecs();

private:
    void drawContent(SkCanvas* c, const Node& n);
    void drawChildren(SkCanvas* c, const Node& n);
    void drawFills(SkCanvas* c, const Node& n, const SkPath& path, float extraAlpha);
    void drawStrokes(SkCanvas* c, const Node& n, const SkPath& path, float extraAlpha);
    void drawText(SkCanvas* c, const Node& n, float extraAlpha);
    void drawDropShadows(SkCanvas* c, const Node& n, const SkPath& path);
    void drawInnerShadows(SkCanvas* c, const Node& n, const SkPath& path);
    bool makePaint(const Paint& paint, const Props& p, float extraAlpha, SkPaint* out);
    void drawArrowCaps(SkCanvas* c, const Node& n, const SkPath& path, const SkPaint& stroke);
    void drawGrids(SkCanvas* c, const Node& n);

    const Document& m_doc;
    Options m_opts;

    struct PicEntry {
        std::uint64_t version = 0;
        sk_sp<SkPicture> pic;
    };
    std::unordered_map<NodeId, PicEntry> m_pics;
    struct ParaEntry {
        std::uint64_t version = 0;
        std::unique_ptr<skia::textlayout::Paragraph> para;
    };
    std::unordered_map<NodeId, ParaEntry> m_paras;
    QHash<QString, sk_sp<SkImage>> m_images;
};

SkBlendMode toSkBlend(BlendMode m);

} // namespace drav
