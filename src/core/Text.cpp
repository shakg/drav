#include "Text.h"

#include <include/core/SkFontMgr.h>
#include <include/core/SkStream.h>
#include <include/core/SkTypeface.h>
#include <include/ports/SkFontMgr_fontconfig.h>
#include <include/ports/SkFontScanner_FreeType.h>
#include <modules/skparagraph/include/ParagraphBuilder.h>
#include <modules/skparagraph/include/TypefaceFontProvider.h>
#include <modules/skunicode/include/SkUnicode_icu.h>

#include <QFile>
#include <QMutex>

#include <cmath>
#include <map>
#include <set>

using namespace skia::textlayout;

namespace drav::text {

namespace {
struct State {
    sk_sp<SkFontMgr> mgr;
    sk_sp<TypefaceFontProvider> bundled;
    sk_sp<FontCollection> collection;
    sk_sp<SkUnicode> unicode;
    QStringList families;
};
State& state()
{
    static State s;
    return s;
}
QMutex g_mutex;
} // namespace

void init(const QStringList& extraFontFiles)
{
    QMutexLocker lock(&g_mutex);
    State& s = state();
    if (s.collection)
        return;
    s.mgr = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
    s.bundled = sk_make_sp<TypefaceFontProvider>();
    QStringList files = {QStringLiteral(":/fonts/InterVariable.ttf"), QStringLiteral(":/fonts/InterVariable-Italic.ttf")};
    files += extraFontFiles;
    std::set<QString> fams;
    for (const QString& f : files) {
        QFile file(f);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QByteArray bytes = file.readAll();
        sk_sp<SkData> data = SkData::MakeWithCopy(bytes.constData(), bytes.size());
        sk_sp<SkTypeface> tf = s.mgr->makeFromData(data);
        if (!tf)
            continue;
        SkString name;
        tf->getFamilyName(&name);
        // Variable Inter reports "Inter Variable"; register it under "Inter" too.
        s.bundled->registerTypeface(tf);
        if (QString::fromUtf8(name.c_str()).startsWith(QLatin1String("Inter")))
            s.bundled->registerTypeface(tf, SkString("Inter"));
        fams.insert(QStringLiteral("Inter"));
    }
    s.collection = sk_make_sp<FontCollection>();
    s.collection->setAssetFontManager(s.bundled);
    s.collection->setDefaultFontManager(s.mgr, std::vector<SkString>{SkString("Inter"), SkString("sans-serif")});
    s.collection->enableFontFallback();
    s.unicode = SkUnicodes::ICU::Make();

    for (int i = 0; i < s.mgr->countFamilies(); ++i) {
        SkString n;
        s.mgr->getFamilyName(i, &n);
        fams.insert(QString::fromUtf8(n.c_str()));
    }
    s.families = QStringList(fams.begin(), fams.end());
}

sk_sp<FontCollection> fonts()
{
    init();
    return state().collection;
}

sk_sp<SkFontMgr> fontMgr()
{
    init();
    return state().mgr;
}

QStringList families()
{
    init();
    return state().families;
}

SkFont uiFont(float size, bool bold)
{
    static sk_sp<SkTypeface> regular, semibold;
    if (!regular) {
        auto pick = [](int weight) -> sk_sp<SkTypeface> {
            auto tfs = fonts()->findTypefaces({SkString("Inter")}, SkFontStyle(weight, 5, SkFontStyle::kUpright_Slant));
            sk_sp<SkTypeface> tf = tfs.empty() ? fontMgr()->legacyMakeTypeface(nullptr, SkFontStyle()) : tfs.front();
            if (!tf)
                return tf;
            SkFontArguments::VariationPosition::Coordinate c{SkSetFourByteTag('w', 'g', 'h', 't'), float(weight)};
            SkFontArguments args;
            args.setVariationDesignPosition({&c, 1});
            sk_sp<SkTypeface> v = tf->makeClone(args);
            return v ? v : tf;
        };
        regular = pick(400);
        semibold = pick(600);
    }
    SkFont f(bold ? semibold : regular, size);
    f.setSubpixel(true);
    f.setEdging(SkFont::Edging::kAntiAlias);
    return f;
}

QString applyCase(const QString& s, TextCase c)
{
    switch (c) {
    case TextCase::Upper: return s.toUpper();
    case TextCase::Lower: return s.toLower();
    case TextCase::Title: {
        QString r = s;
        bool start = true;
        for (QChar& ch : r) {
            if (ch.isLetter()) {
                if (start)
                    ch = ch.toUpper();
                start = false;
            } else {
                start = ch.isSpace();
            }
        }
        return r;
    }
    case TextCase::Original: break;
    }
    return s;
}

// ---------------------------------------------------------------- rich text editing

void normalize(Props& p)
{
    const int n = int(p.text.size());
    auto& runs = p.textRuns;
    for (TextRun& r : runs) {
        r.start = std::clamp(r.start, 0, n);
        r.end = std::clamp(r.end, 0, n);
    }
    std::erase_if(runs, [](const TextRun& r) { return r.end <= r.start || r.mask == 0; });
    std::sort(runs.begin(), runs.end(), [](const TextRun& a, const TextRun& b) { return a.start < b.start; });
    std::vector<TextRun> merged;
    for (const TextRun& r : runs) {
        if (!merged.empty() && merged.back().end >= r.start && merged.back().sameStyle(r))
            merged.back().end = std::max(merged.back().end, r.end);
        else
            merged.push_back(r);
    }
    runs = std::move(merged);
    const int paras = int(p.text.count(QLatin1Char('\n'))) + 1;
    if (int(p.paragraphs.size()) > paras)
        p.paragraphs.resize(paras);
    while (!p.paragraphs.empty() && p.paragraphs.back() == ParagraphAttr())
        p.paragraphs.pop_back();
}

void splice(Props& p, int pos, int removed, const QString& insert)
{
    const int n = int(p.text.size());
    pos = std::clamp(pos, 0, n);
    removed = std::clamp(removed, 0, n - pos);
    const int len = int(insert.size());

    // Paragraphs: removed newlines merge paragraphs into the one at `pos`; inserted ones split it.
    const int para = int(p.text.left(pos).count(QLatin1Char('\n')));
    const int gone = int(p.text.mid(pos, removed).count(QLatin1Char('\n')));
    const int added = int(insert.count(QLatin1Char('\n')));
    if (!p.paragraphs.empty() && (gone || added)) {
        auto& ps = p.paragraphs;
        if (int(ps.size()) <= para + gone)
            ps.resize(para + gone + 1);
        ps.erase(ps.begin() + para + 1, ps.begin() + para + 1 + gone);
        ps.insert(ps.begin() + para + 1, added, ps[para]);
    }

    for (TextRun& r : p.textRuns) {
        // Deletion of [pos, pos + removed)
        auto cut = [&](int i) { return i <= pos ? i : (i >= pos + removed ? i - removed : pos); };
        r.start = cut(r.start);
        r.end = cut(r.end);
        // Insertion at pos: grows the run holding the previous character (or the first one at 0).
        if (r.start < pos && r.end >= pos)
            r.end += len;
        else if (pos == 0 && r.start == 0 && r.end > 0)
            r.end += len;
        else if (r.start >= pos)
            r.start += len, r.end += len;
    }
    p.text.replace(pos, removed, insert);
    normalize(p);
}

static void splitAt(std::vector<TextRun>& runs, int at)
{
    for (size_t i = 0; i < runs.size(); ++i)
        if (runs[i].start < at && runs[i].end > at) {
            TextRun tail = runs[i];
            tail.start = at;
            runs[i].end = at;
            runs.insert(runs.begin() + i + 1, tail);
            return;
        }
}

void styleRange(Props& p, int start, int end, std::uint32_t mask, const TextRun& v)
{
    const int n = int(p.text.size());
    start = std::clamp(start, 0, n);
    end = std::clamp(end, 0, n);
    if (start >= end || !mask)
        return;
    auto& runs = p.textRuns;
    splitAt(runs, start);
    splitAt(runs, end);
    // Fill gaps inside the range with empty runs so every character has one.
    std::vector<TextRun> out;
    int at = start;
    for (const TextRun& r : runs) {
        if (r.end <= start || r.start >= end) {
            out.push_back(r);
            continue;
        }
        if (r.start > at) {
            TextRun gap;
            gap.start = at;
            gap.end = r.start;
            out.push_back(gap);
        }
        out.push_back(r);
        at = r.end;
    }
    if (at < end) {
        TextRun gap;
        gap.start = at;
        gap.end = end;
        out.push_back(gap);
    }
    for (TextRun& r : out) {
        if (r.start < start || r.end > end)
            continue;
        r.mask |= mask;
        if (mask & RunFamily) r.family = v.family;
        if (mask & RunWeight) r.weight = v.weight;
        if (mask & RunItalic) r.italic = v.italic;
        if (mask & RunSize) r.size = v.size;
        if (mask & RunLetterSpacing) r.letterSpacing = v.letterSpacing;
        if (mask & RunDecoration) r.decoration = v.decoration;
        if (mask & RunCase) r.textCase = v.textCase;
        if (mask & RunFill) r.fill = v.fill;
        if (mask & RunLink) r.link = v.link;
    }
    runs = std::move(out);
    normalize(p);
}

void clearRange(Props& p, int start, int end, std::uint32_t mask)
{
    auto& runs = p.textRuns;
    splitAt(runs, start);
    splitAt(runs, end);
    for (TextRun& r : runs)
        if (r.start >= start && r.end <= end) {
            r.mask &= ~mask;
            // Reset cleared fields so equal runs merge.
            TextRun def;
            if (mask & RunFamily) r.family = def.family;
            if (mask & RunWeight) r.weight = def.weight;
            if (mask & RunItalic) r.italic = def.italic;
            if (mask & RunSize) r.size = def.size;
            if (mask & RunLetterSpacing) r.letterSpacing = def.letterSpacing;
            if (mask & RunDecoration) r.decoration = def.decoration;
            if (mask & RunCase) r.textCase = def.textCase;
            if (mask & RunFill) r.fill = def.fill;
            if (mask & RunLink) r.link = def.link;
        }
    normalize(p);
}

TextRun styleAt(const Props& p, int index)
{
    TextRun s;
    const TextStyle& t = p.textStyle;
    s.family = t.family;
    s.weight = t.weight;
    s.italic = t.italic;
    s.size = t.size;
    s.letterSpacing = t.letterSpacing;
    s.decoration = t.decoration;
    s.textCase = t.textCase;
    for (const Paint& f : p.fills)
        if (f.visible && f.type == PaintType::Solid)
            s.fill = f.color;
    for (const TextRun& r : p.textRuns) {
        if (index < r.start || index >= r.end)
            continue;
        if (r.mask & RunFamily) s.family = r.family;
        if (r.mask & RunWeight) s.weight = r.weight;
        if (r.mask & RunItalic) s.italic = r.italic;
        if (r.mask & RunSize) s.size = r.size;
        if (r.mask & RunLetterSpacing) s.letterSpacing = r.letterSpacing;
        if (r.mask & RunDecoration) s.decoration = r.decoration;
        if (r.mask & RunCase) s.textCase = r.textCase;
        if (r.mask & RunFill) s.fill = r.fill;
        if (r.mask & RunLink) s.link = r.link;
        s.mask = r.mask;
    }
    return s;
}

std::pair<int, int> paragraphRange(const QString& text, int start, int end)
{
    const int first = int(text.left(std::max(0, start)).count(QLatin1Char('\n')));
    const int last = int(text.left(std::max(start, end)).count(QLatin1Char('\n')));
    return {first, last};
}

void setParagraphs(Props& p, int first, int last, const ParagraphAttr& attr)
{
    if (int(p.paragraphs.size()) <= last)
        p.paragraphs.resize(last + 1);
    for (int i = first; i <= last; ++i)
        p.paragraphs[i] = attr;
    normalize(p);
}

// ---------------------------------------------------------------- layout

namespace {

// One stretch of the laid-out string: characters [src, src + text.size()) with one style, or a
// synthetic piece (list marker, paragraph spacing line) with src = -1.
struct Piece {
    QString text;
    int src = -1;
    const TextRun* run = nullptr;
    bool spacer = false;
};

std::vector<Piece> pieces(const Props& p)
{
    std::vector<Piece> out;
    const QString& t = p.text;
    const int n = int(t.size());
    const auto& runs = p.textRuns;
    auto runAt = [&](int i) -> const TextRun* {
        for (const TextRun& r : runs)
            if (i >= r.start && i < r.end)
                return &r;
        return nullptr;
    };
    std::map<int, int> counters; // ordered list numbering per indent level
    int i = 0, para = 0;
    while (true) {
        int end = int(t.indexOf(QLatin1Char('\n'), i));
        if (end < 0)
            end = n;
        const ParagraphAttr attr = para < int(p.paragraphs.size()) ? p.paragraphs[para] : ParagraphAttr();
        if (attr.list == ListType::Ordered) {
            for (auto it = counters.upper_bound(attr.indent); it != counters.end();)
                it = counters.erase(it);
            ++counters[attr.indent];
        } else {
            counters.clear();
        }
        if (attr.list != ListType::None) {
            static const char16_t* const bullets[] = {u"•", u"◦", u"▪"};
            QString marker = QString(attr.indent * 4, QLatin1Char(' '));
            marker += attr.list == ListType::Bullet ? QString::fromUtf16(bullets[attr.indent % 3])
                                                    : QString::number(counters[attr.indent]) + QLatin1Char('.');
            marker += QStringLiteral("  ");
            out.push_back({marker, -1, runAt(i), false});
        }
        // Split the paragraph at run boundaries.
        int a = i;
        while (a < end) {
            const TextRun* r = runAt(a);
            int b = end;
            for (const TextRun& x : runs) {
                if (x.start > a && x.start < b)
                    b = x.start;
                if (x.end > a && x.end < b)
                    b = x.end;
            }
            out.push_back({t.mid(a, b - a), a, r, false});
            a = b;
        }
        if (end >= n)
            break;
        out.push_back({QStringLiteral("\n"), end, runAt(end), false});
        if (p.textStyle.paragraphSpacing > 0)
            out.push_back({QStringLiteral("\n"), -1, nullptr, true});
        i = end + 1;
        ++para;
    }
    return out;
}

skia::textlayout::TextStyle styleFor(const Props& p, const TextRun* r, SkColor4f color)
{
    const TextStyle& ts = p.textStyle;
    QString family = ts.family;
    int weight = ts.weight;
    bool italic = ts.italic;
    float size = ts.size, letter = ts.letterSpacing;
    TextDecoration deco = ts.decoration;
    if (r) {
        if (r->mask & RunFamily) family = r->family;
        if (r->mask & RunWeight) weight = r->weight;
        if (r->mask & RunItalic) italic = r->italic;
        if (r->mask & RunSize) size = r->size;
        if (r->mask & RunLetterSpacing) letter = r->letterSpacing;
        if (r->mask & RunDecoration) deco = r->decoration;
        else if (r->mask & RunLink) deco = TextDecoration::Underline;
        if (r->mask & RunFill) color = r->fill;
    }
    skia::textlayout::TextStyle st;
    st.setFontFamilies({SkString(family.toUtf8().constData())});
    st.setFontSize(size);
    st.setFontStyle(SkFontStyle(weight, SkFontStyle::kNormal_Width,
        italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant));
    std::vector<SkFontArguments::VariationPosition::Coordinate> coords;
    coords.push_back({SkSetFourByteTag('w', 'g', 'h', 't'), float(weight)});
    for (auto it = ts.axes.begin(); it != ts.axes.end(); ++it) {
        const QByteArray tag = it.key().toLatin1().leftJustified(4, ' ', true);
        coords.push_back({SkSetFourByteTag(tag[0], tag[1], tag[2], tag[3]), it.value()});
    }
    SkFontArguments args;
    args.setVariationDesignPosition({coords.data(), int(coords.size())});
    st.setFontArguments(args);
    for (auto it = ts.features.begin(); it != ts.features.end(); ++it)
        st.addFontFeature(SkString(it.key().toLatin1().constData()), it.value());
    st.setColor(color.toSkColor());
    st.setLetterSpacing(letter / 100.f * size);
    if (ts.lineHeight > 0) {
        const float px = ts.lineHeightPercent ? ts.lineHeight / 100.f * ts.size : ts.lineHeight;
        st.setHeight(px / size);
        st.setHeightOverride(true);
        st.setHalfLeading(true);
    }
    if (deco == TextDecoration::Underline)
        st.setDecoration(skia::textlayout::TextDecoration::kUnderline);
    else if (deco == TextDecoration::Strikethrough)
        st.setDecoration(skia::textlayout::TextDecoration::kLineThrough);
    st.setDecorationColor(color.toSkColor());
    return st;
}

} // namespace

std::vector<int> displayMap(const Props& p)
{
    const int n = int(p.text.size());
    std::vector<int> map(n + 1, 0);
    int disp = 0;
    for (const Piece& pc : pieces(p)) {
        if (pc.src >= 0)
            for (int k = 0; k < pc.text.size() && pc.src + k <= n; ++k)
                map[pc.src + k] = disp + k;
        disp += int(pc.text.size());
    }
    map[n] = disp;
    return map;
}

int fromDisplay(const std::vector<int>& map, int display)
{
    auto it = std::lower_bound(map.begin(), map.end(), display);
    return it == map.end() ? int(map.size()) - 1 : int(it - map.begin());
}

std::vector<Axis> axes(const QString& family, int weight, bool italic)
{
    std::vector<Axis> out;
    auto tfs = fonts()->findTypefaces({SkString(family.toUtf8().constData())},
        SkFontStyle(weight, SkFontStyle::kNormal_Width, italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant));
    if (tfs.empty())
        return out;
    const int count = tfs.front()->getVariationDesignParameters(nullptr, 0);
    if (count <= 0)
        return out;
    std::vector<SkFontParameters::Variation::Axis> params(count);
    tfs.front()->getVariationDesignParameters(params.data(), count);
    for (const auto& a : params) {
        const char tag[5] = {char(a.tag >> 24), char(a.tag >> 16), char(a.tag >> 8), char(a.tag), 0};
        out.push_back({QString::fromLatin1(tag), a.min, a.max, a.def});
    }
    return out;
}

bool hasFamily(const QString& family)
{
    return families().contains(family);
}

std::unique_ptr<Paragraph> build(const Props& p, float layoutWidth, SkColor4f color)
{
    init();
    const TextStyle& ts = p.textStyle;
    skia::textlayout::TextStyle st = styleFor(p, nullptr, color);

    ParagraphStyle ps;
    ps.setTextStyle(st);
    switch (ts.alignH) {
    case TextAlignH::Left: ps.setTextAlign(TextAlign::kLeft); break;
    case TextAlignH::Center: ps.setTextAlign(TextAlign::kCenter); break;
    case TextAlignH::Right: ps.setTextAlign(TextAlign::kRight); break;
    case TextAlignH::Justified: ps.setTextAlign(TextAlign::kJustify); break;
    }
    if (ts.maxLines > 0 || ts.autoResize == TextAutoResize::Truncate) {
        if (ts.maxLines > 0)
            ps.setMaxLines(ts.maxLines);
        ps.setEllipsis(u"…");
    }
    StrutStyle strut;
    if (ts.lineHeight > 0) {
        strut.setStrutEnabled(true);
        strut.setFontFamilies({SkString(ts.family.toUtf8().constData())});
        strut.setFontSize(ts.size);
        const float px = ts.lineHeightPercent ? ts.lineHeight / 100.f * ts.size : ts.lineHeight;
        strut.setHeight(px / ts.size);
        strut.setHeightOverride(true);
        strut.setHalfLeading(true);
        strut.setForceStrutHeight(true);
        ps.setStrutStyle(strut);
    }

    auto builder = ParagraphBuilder::make(ps, fonts(), state().unicode);
    for (const Piece& pc : pieces(p)) {
        if (pc.spacer) {
            // Paragraph spacing: an empty line scaled to the spacing.
            skia::textlayout::TextStyle sp = st;
            sp.setFontSize(ts.paragraphSpacing);
            sp.setHeight(1);
            sp.setHeightOverride(true);
            builder->pushStyle(sp);
        } else {
            builder->pushStyle(styleFor(p, pc.run, color));
        }
        const TextCase tc = pc.run && (pc.run->mask & RunCase) ? pc.run->textCase : ts.textCase;
        const QByteArray u = applyCase(pc.text, tc).toUtf8();
        builder->addText(u.constData(), u.size());
        builder->pop();
    }
    auto para = builder->Build();
    para->layout(layoutWidth < 0 ? 1e6f : std::max(layoutWidth, 1.f));
    return para;
}

Metrics measure(const Props& p, float layoutWidth)
{
    auto para = build(p, layoutWidth);
    Metrics m;
    m.width = std::ceil(para->getMaxIntrinsicWidth());
    m.height = std::ceil(para->getHeight());
    m.firstBaseline = para->getAlphabeticBaseline();
    if (p.text.isEmpty()) {
        m.width = 1;
    }
    return m;
}

bool autoSize(Props& p)
{
    const TextAutoResize mode = p.textStyle.autoResize;
    float w = p.width, h = p.height;
    if (mode == TextAutoResize::WidthAndHeight) {
        Metrics m = measure(p, -1);
        w = std::max(1.f, m.width);
        h = m.height;
    } else if (mode == TextAutoResize::Height) {
        h = measure(p, p.width).height;
    } else {
        return false;
    }
    if (w == p.width && h == p.height)
        return false;
    p.width = w;
    p.height = h;
    return true;
}

float verticalOffset(const Props& p, float paragraphHeight)
{
    switch (p.textStyle.alignV) {
    case TextAlignV::Top: return 0;
    case TextAlignV::Center: return (p.height - paragraphHeight) / 2;
    case TextAlignV::Bottom: return p.height - paragraphHeight;
    }
    return 0;
}

} // namespace drav::text
