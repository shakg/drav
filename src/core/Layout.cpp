#include "Layout.h"

#include "Document.h"
#include "Text.h"

#include <algorithm>
#include <cmath>

namespace drav {

SkSize layoutSize(const Props& p)
{
    SkRect b = p.transform.mapRect(SkRect::MakeWH(p.width, p.height));
    return {b.width(), b.height()};
}

static float clampSize(float v, float mn, float mx)
{
    if (mx > 0)
        v = std::min(v, mx);
    if (mn > 0)
        v = std::max(v, mn);
    return std::max(v, 0.01f);
}

static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-3f; }

static TextAutoResize effectiveTextMode(const Document& doc, const Node& n)
{
    TextAutoResize m = n.p.textStyle.autoResize;
    if (m == TextAutoResize::WidthAndHeight && n.parent) {
        const Props& parent = doc.at(n.parent).p;
        if (parent.layout.mode != LayoutMode::None && !n.p.layoutChild.absolute
            && n.p.layoutChild.sizingH == LayoutSizing::Fill)
            m = TextAutoResize::Height;
    }
    return m;
}

void LayoutEngine::update(NodeId id, const std::unordered_set<NodeId>& affected)
{
    auto kids = m_doc.at(id).children;
    for (NodeId c : kids)
        if (affected.count(c))
            update(c, affected);
    intrinsic(id);
}

void LayoutEngine::intrinsic(NodeId id)
{
    const Node& n = m_doc.at(id);
    switch (n.type()) {
    case NodeType::Text: {
        Props p = n.p;
        p.textStyle.autoResize = effectiveTextMode(m_doc, n);
        if (text::autoSize(p) && (!nearly(p.width, n.p.width) || !nearly(p.height, n.p.height))) {
            Props& e = m_doc.edit(id);
            e.width = p.width;
            e.height = p.height;
        }
        break;
    }
    case NodeType::Group:
    case NodeType::BooleanOp:
        m_doc.fitGroup(id);
        break;
    default:
        if (isFrameLike(n.type()) && n.p.layout.mode != LayoutMode::None)
            layoutFrame(id);
        break;
    }
}

void LayoutEngine::afterResize(NodeId id)
{
    const Node& n = m_doc.at(id);
    if (n.type() == NodeType::Text) {
        TextAutoResize m = effectiveTextMode(m_doc, n);
        if (m == TextAutoResize::Height || m == TextAutoResize::WidthAndHeight) {
            float h = text::measure(n.p, n.p.width).height;
            if (!nearly(h, n.p.height))
                m_doc.edit(id).height = h;
        }
    } else if (isFrameLike(n.type()) && n.p.layout.mode != LayoutMode::None) {
        layoutFrame(id);
    }
}

void LayoutEngine::place(NodeId child, float x, float y)
{
    const Props& p = m_doc.at(child).p;
    SkRect b = p.transform.mapRect(SkRect::MakeWH(p.width, p.height));
    if (nearly(b.left(), x) && nearly(b.top(), y))
        return;
    m_doc.edit(child).transform.postTranslate(x - b.left(), y - b.top());
}

void LayoutEngine::layoutFrame(NodeId id)
{
    const Props& P0 = m_doc.at(id).p;
    const AutoLayout L = P0.layout;
    if (L.mode == LayoutMode::None)
        return;
    if (L.mode == LayoutMode::Grid)
        return layoutGrid(id);
    const bool horiz = L.mode == LayoutMode::Horizontal;
    const bool hugW = P0.layoutChild.sizingH == LayoutSizing::Hug;
    const bool hugH = P0.layoutChild.sizingV == LayoutSizing::Hug;
    const bool hugP = horiz ? hugW : hugH;
    const bool hugC = horiz ? hugH : hugW;
    float strokeInset = 0;
    if (L.strokesIncluded && !P0.strokes.empty()) {
        strokeInset = P0.stroke.align == StrokeAlign::Inside ? P0.stroke.weight
            : P0.stroke.align == StrokeAlign::Center          ? P0.stroke.weight / 2
                                                              : 0;
    }
    const float padP0 = (horiz ? L.padL : L.padT) + strokeInset, padP1 = (horiz ? L.padR : L.padB) + strokeInset;
    const float padC0 = (horiz ? L.padT : L.padL) + strokeInset, padC1 = (horiz ? L.padB : L.padR) + strokeInset;

    std::vector<NodeId> items;
    for (NodeId c : m_doc.at(id).children) {
        const Props& cp = m_doc.at(c).p;
        if (cp.visible && !cp.layoutChild.absolute)
            items.push_back(c);
    }
    const int n = (int)items.size();

    auto prim = [&](NodeId c) {
        SkSize s = layoutSize(m_doc.at(c).p);
        return horiz ? s.width() : s.height();
    };
    auto cnt = [&](NodeId c) {
        SkSize s = layoutSize(m_doc.at(c).p);
        return horiz ? s.height() : s.width();
    };
    auto sizingP = [&](NodeId c) {
        const LayoutChild& lc = m_doc.at(c).p.layoutChild;
        return horiz ? lc.sizingH : lc.sizingV;
    };
    auto sizingC = [&](NodeId c) {
        const LayoutChild& lc = m_doc.at(c).p.layoutChild;
        return horiz ? lc.sizingV : lc.sizingH;
    };
    auto minMaxP = [&](NodeId c, float v) {
        const LayoutChild& lc = m_doc.at(c).p.layoutChild;
        return horiz ? clampSize(v, lc.minW, lc.maxW) : clampSize(v, lc.minH, lc.maxH);
    };
    auto minMaxC = [&](NodeId c, float v) {
        const LayoutChild& lc = m_doc.at(c).p.layoutChild;
        return horiz ? clampSize(v, lc.minH, lc.maxH) : clampSize(v, lc.minW, lc.maxW);
    };
    auto setP = [&](NodeId c, float v) {
        const Props& cp = m_doc.at(c).p;
        float w = horiz ? v : cp.width, h = horiz ? cp.height : v;
        if (nearly(w, cp.width) && nearly(h, cp.height))
            return;
        m_doc.resize(c, w, h);
        afterResize(c);
    };
    auto setC = [&](NodeId c, float v) {
        const Props& cp = m_doc.at(c).p;
        float w = horiz ? cp.width : v, h = horiz ? v : cp.height;
        if (nearly(w, cp.width) && nearly(h, cp.height))
            return;
        m_doc.resize(c, w, h);
        afterResize(c);
    };
    auto frameP = [&] { const Props& p = m_doc.at(id).p; return horiz ? p.width : p.height; };
    auto frameC = [&] { const Props& p = m_doc.at(id).p; return horiz ? p.height : p.width; };
    auto resizeFrame = [&](float newP, float newC) {
        const Props& p = m_doc.at(id).p;
        const LayoutChild& lc = p.layoutChild;
        float w = horiz ? newP : newC, h = horiz ? newC : newP;
        w = clampSize(w, lc.minW, lc.maxW);
        h = clampSize(h, lc.minH, lc.maxH);
        if (!nearly(w, p.width) || !nearly(h, p.height))
            m_doc.resize(id, w, h);
    };

    const bool wrap = L.wrap && horiz && !hugP;

    if (!wrap) {
        // 1) Distribute remaining primary space among fill children.
        std::vector<NodeId> fills;
        float fixedSum = 0;
        for (NodeId c : items) {
            if (sizingP(c) == LayoutSizing::Fill && !hugP)
                fills.push_back(c);
            else
                fixedSum += prim(c);
        }
        const bool between = L.primaryAlign == LayoutAlign::SpaceBetween;
        const float gapTotal = (between ? 0.f : L.spacing) * std::max(0, n - 1);
        if (!fills.empty()) {
            float remain = std::max(0.f, frameP() - padP0 - padP1 - fixedSum - gapTotal);
            std::vector<NodeId> open = fills;
            // Clamp children hitting min/max and redistribute the rest.
            for (int iter = 0; iter < 4 && !open.empty(); ++iter) {
                float each = remain / open.size();
                std::vector<NodeId> next;
                float used = 0;
                for (NodeId c : open) {
                    float v = minMaxP(c, each);
                    if (!nearly(v, each)) {
                        setP(c, v);
                        used += v;
                    } else {
                        next.push_back(c);
                    }
                }
                if (next.size() == open.size()) {
                    for (NodeId c : open)
                        setP(c, each);
                    break;
                }
                remain = std::max(0.f, remain - used);
                open = std::move(next);
                if (iter == 3)
                    for (NodeId c : open)
                        setP(c, remain / open.size());
            }
        }

        // 2) Counter axis: hug picks the tallest non-fill child; fill children stretch.
        float counter = frameC() - padC0 - padC1;
        if (hugC) {
            float mx = 0;
            bool any = false;
            for (NodeId c : items)
                if (sizingC(c) != LayoutSizing::Fill) {
                    mx = std::max(mx, cnt(c));
                    any = true;
                }
            if (!any)
                for (NodeId c : items)
                    mx = std::max(mx, cnt(c));
            counter = mx;
        }
        for (NodeId c : items)
            if (sizingC(c) == LayoutSizing::Fill)
                setC(c, minMaxC(c, counter));
        if (hugC) {
            // Fill children may have changed size via text reflow; recompute.
            float mx = 0;
            for (NodeId c : items)
                mx = std::max(mx, cnt(c));
            counter = std::max(counter, mx);
        }

        // 3) Hug sizes.
        float sum = 0;
        for (NodeId c : items)
            sum += prim(c);
        const float used = sum + L.spacing * std::max(0, n - 1);
        resizeFrame(hugP ? used + padP0 + padP1 : frameP(), hugC ? counter + padC0 + padC1 : frameC());

        // 4) Positions.
        const float innerP = frameP() - padP0 - padP1;
        const float innerC = frameC() - padC0 - padC1;
        float gap = L.spacing;
        float cursor = padP0;
        const float freeSpace = innerP - used;
        switch (L.primaryAlign) {
        case LayoutAlign::Center: cursor += freeSpace / 2; break;
        case LayoutAlign::Max: cursor += freeSpace; break;
        case LayoutAlign::SpaceBetween:
            if (n > 1 && !hugP)
                gap = (innerP - sum) / (n - 1);
            break;
        default: break;
        }
        float maxBaseline = 0;
        if (L.counterAlign == LayoutAlign::Baseline && horiz)
            for (NodeId c : items)
                if (m_doc.at(c).type() == NodeType::Text)
                    maxBaseline = std::max(maxBaseline, text::measure(m_doc.at(c).p, m_doc.at(c).p.width).firstBaseline);
        for (NodeId c : items) {
            float cs = cnt(c);
            float cc = padC0;
            switch (L.counterAlign) {
            case LayoutAlign::Center: cc += (innerC - cs) / 2; break;
            case LayoutAlign::Max: cc += innerC - cs; break;
            case LayoutAlign::Baseline:
                if (horiz && m_doc.at(c).type() == NodeType::Text)
                    cc += maxBaseline - text::measure(m_doc.at(c).p, m_doc.at(c).p.width).firstBaseline;
                break;
            default: break;
            }
            place(c, horiz ? cursor : cc, horiz ? cc : cursor);
            cursor += prim(c) + gap;
        }
        return;
    }

    // Wrapping horizontal layout.
    const float innerW = frameP() - padP0 - padP1;
    std::vector<std::vector<NodeId>> lines(1);
    float lineW = 0;
    for (NodeId c : items) {
        float w = prim(c);
        if (!lines.back().empty() && lineW + L.spacing + w > innerW + 1e-3f) {
            lines.emplace_back();
            lineW = 0;
        }
        lineW += (lines.back().empty() ? 0 : L.spacing) + w;
        lines.back().push_back(c);
    }
    std::vector<float> lineH;
    float total = 0;
    for (auto& line : lines) {
        float h = 0;
        for (NodeId c : line)
            if (sizingC(c) != LayoutSizing::Fill)
                h = std::max(h, cnt(c));
        for (NodeId c : line)
            if (sizingC(c) == LayoutSizing::Fill)
                setC(c, minMaxC(c, h > 0 ? h : cnt(c)));
        for (NodeId c : line)
            h = std::max(h, cnt(c));
        lineH.push_back(h);
        total += h;
    }
    total += L.counterSpacing * std::max(0, (int)lines.size() - 1);
    if (hugC)
        resizeFrame(frameP(), total + padC0 + padC1);
    const float innerC = frameC() - padC0 - padC1;
    float y = padC0;
    if (!hugC) {
        if (L.counterAlign == LayoutAlign::Center)
            y += (innerC - total) / 2;
        else if (L.counterAlign == LayoutAlign::Max)
            y += innerC - total;
    }
    for (size_t li = 0; li < lines.size(); ++li) {
        auto& line = lines[li];
        float sum = 0;
        for (NodeId c : line)
            sum += prim(c);
        const int ln = (int)line.size();
        const float used = sum + L.spacing * std::max(0, ln - 1);
        float gap = L.spacing;
        float x = padP0;
        switch (L.primaryAlign) {
        case LayoutAlign::Center: x += (innerW - used) / 2; break;
        case LayoutAlign::Max: x += innerW - used; break;
        case LayoutAlign::SpaceBetween:
            if (ln > 1)
                gap = (innerW - sum) / (ln - 1);
            break;
        default: break;
        }
        for (NodeId c : line) {
            float ch = cnt(c);
            float cy = y;
            if (L.counterAlign == LayoutAlign::Center)
                cy += (lineH[li] - ch) / 2;
            else if (L.counterAlign == LayoutAlign::Max)
                cy += lineH[li] - ch;
            place(c, x, cy);
            x += prim(c) + gap;
        }
        y += lineH[li] + L.counterSpacing;
    }
}

void LayoutEngine::layoutGrid(NodeId id)
{
    const Props& P0 = m_doc.at(id).p;
    const AutoLayout L = P0.layout;
    const bool hugW = P0.layoutChild.sizingH == LayoutSizing::Hug;
    const bool hugH = P0.layoutChild.sizingV == LayoutSizing::Hug;
    const int cols = std::max(1, L.gridColumns);
    const float gapX = L.spacing, gapY = L.counterSpacing;

    std::vector<NodeId> items;
    for (NodeId c : m_doc.at(id).children) {
        const Props& cp = m_doc.at(c).p;
        if (cp.visible && !cp.layoutChild.absolute)
            items.push_back(c);
    }

    // Auto-place row by row into the first free cells that fit each child's span.
    struct Cell {
        int col, row, cs, rs;
    };
    std::vector<Cell> cells;
    std::vector<std::vector<bool>> used;
    auto free = [&](int r, int c, int rs, int cs) {
        for (int y = r; y < r + rs; ++y)
            for (int x = c; x < c + cs; ++x)
                if (y < int(used.size()) && used[y][x])
                    return false;
        return true;
    };
    int r = 0, c = 0;
    for (NodeId it : items) {
        const LayoutChild& lc = m_doc.at(it).p.layoutChild;
        const int cs = std::clamp(lc.colSpan, 1, cols), rs = std::max(1, lc.rowSpan);
        while (true) {
            if (c + cs > cols) {
                c = 0;
                ++r;
                continue;
            }
            if (free(r, c, rs, cs))
                break;
            if (++c >= cols) {
                c = 0;
                ++r;
            }
        }
        while (int(used.size()) < r + rs)
            used.emplace_back(cols, false);
        for (int y = r; y < r + rs; ++y)
            for (int x = c; x < c + cs; ++x)
                used[y][x] = true;
        cells.push_back({c, r, cs, rs});
        c += cs;
    }
    const int rows = std::max({L.gridRows, int(used.size()), 1});

    // Tracks: equal fractions of a fixed frame, or the largest single-span child when hugging.
    const float innerW = P0.width - L.padL - L.padR, innerH = P0.height - L.padT - L.padB;
    std::vector<float> colW(cols, 0), rowH(rows, 0);
    auto size = [&](NodeId it) { return layoutSize(m_doc.at(it).p); };
    for (size_t i = 0; i < items.size(); ++i) {
        const LayoutChild& lc = m_doc.at(items[i]).p.layoutChild;
        if (cells[i].cs == 1 && lc.sizingH != LayoutSizing::Fill)
            colW[cells[i].col] = std::max(colW[cells[i].col], size(items[i]).width());
        if (cells[i].rs == 1 && lc.sizingV != LayoutSizing::Fill)
            rowH[cells[i].row] = std::max(rowH[cells[i].row], size(items[i]).height());
    }
    if (!hugW)
        std::fill(colW.begin(), colW.end(), std::max(0.f, (innerW - gapX * (cols - 1)) / cols));
    if (!hugH && L.gridRows > 0)
        std::fill(rowH.begin(), rowH.end(), std::max(0.f, (innerH - gapY * (rows - 1)) / rows));

    auto span = [](const std::vector<float>& t, int from, int n, float gap) {
        float s = gap * (n - 1);
        for (int k = from; k < from + n && k < int(t.size()); ++k)
            s += t[k];
        return s;
    };
    auto offset = [](const std::vector<float>& t, int to, float gap) {
        float s = 0;
        for (int k = 0; k < to; ++k)
            s += t[k] + gap;
        return s;
    };
    auto align = [](LayoutAlign a, float room) {
        return a == LayoutAlign::Center ? room / 2 : a == LayoutAlign::Max ? room : 0.f;
    };
    // Fill children stretch over their cells; text heights follow from the width.
    for (size_t i = 0; i < items.size(); ++i) {
        const NodeId it = items[i];
        const Props& cp = m_doc.at(it).p;
        const float cw = span(colW, cells[i].col, cells[i].cs, gapX), ch = span(rowH, cells[i].row, cells[i].rs, gapY);
        float w = cp.layoutChild.sizingH == LayoutSizing::Fill ? cw : cp.width;
        float h = cp.layoutChild.sizingV == LayoutSizing::Fill ? ch : cp.height;
        w = clampSize(w, cp.layoutChild.minW, cp.layoutChild.maxW);
        h = clampSize(h, cp.layoutChild.minH, cp.layoutChild.maxH);
        if (!nearly(w, cp.width) || !nearly(h, cp.height)) {
            m_doc.resize(it, w, h);
            afterResize(it);
        }
        if (cells[i].rs == 1 && cp.layoutChild.sizingV != LayoutSizing::Fill && (hugH || L.gridRows == 0))
            rowH[cells[i].row] = std::max(rowH[cells[i].row], size(it).height());
    }
    for (size_t i = 0; i < items.size(); ++i) {
        const NodeId it = items[i];
        const SkSize s = size(it);
        const float cw = span(colW, cells[i].col, cells[i].cs, gapX), ch = span(rowH, cells[i].row, cells[i].rs, gapY);
        place(it, L.padL + offset(colW, cells[i].col, gapX) + align(L.primaryAlign, cw - s.width()),
            L.padT + offset(rowH, cells[i].row, gapY) + align(L.counterAlign, ch - s.height()));
    }
    float w = P0.width, h = P0.height;
    if (hugW)
        w = L.padL + L.padR + span(colW, 0, cols, gapX);
    if (hugH || L.gridRows == 0)
        h = hugH ? L.padT + L.padB + span(rowH, 0, rows, gapY) : h;
    const LayoutChild& lc = m_doc.at(id).p.layoutChild;
    w = clampSize(w, lc.minW, lc.maxW);
    h = clampSize(h, lc.minH, lc.maxH);
    if (!nearly(w, m_doc.at(id).p.width) || !nearly(h, m_doc.at(id).p.height))
        m_doc.resize(id, w, h);
}

} // namespace drav
