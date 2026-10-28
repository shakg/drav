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

std::unique_ptr<Paragraph> build(const Props& p, float layoutWidth, SkColor4f color)
{
    init();
    const TextStyle& ts = p.textStyle;
    skia::textlayout::TextStyle st;
    st.setFontFamilies({SkString(ts.family.toUtf8().constData())});
    st.setFontSize(ts.size);
    st.setFontStyle(SkFontStyle(ts.weight, SkFontStyle::kNormal_Width,
        ts.italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant));
    SkFontArguments::VariationPosition::Coordinate wght{SkSetFourByteTag('w', 'g', 'h', 't'), float(ts.weight)};
    SkFontArguments args;
    args.setVariationDesignPosition({&wght, 1});
    st.setFontArguments(args);
    st.setColor(color.toSkColor());
    st.setLetterSpacing(ts.letterSpacing / 100.f * ts.size);
    if (ts.lineHeight > 0) {
        const float px = ts.lineHeightPercent ? ts.lineHeight / 100.f * ts.size : ts.lineHeight;
        st.setHeight(px / ts.size);
        st.setHeightOverride(true);
        st.setHalfLeading(true);
    }
    if (ts.decoration == TextDecoration::Underline)
        st.setDecoration(skia::textlayout::TextDecoration::kUnderline);
    else if (ts.decoration == TextDecoration::Strikethrough)
        st.setDecoration(skia::textlayout::TextDecoration::kLineThrough);
    st.setDecorationColor(color.toSkColor());

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
    QString content = applyCase(p.text, ts.textCase);
    if (ts.paragraphSpacing > 0) {
        // Paragraph spacing: emulate with a scaled empty line between paragraphs.
        const QStringList paras = content.split(QLatin1Char('\n'));
        for (int i = 0; i < paras.size(); ++i) {
            QByteArray u = paras[i].toUtf8();
            builder->pushStyle(st);
            builder->addText(u.constData(), u.size());
            builder->pop();
            if (i + 1 < paras.size()) {
                builder->addText("\n", 1);
                skia::textlayout::TextStyle sp = st;
                sp.setFontSize(ts.paragraphSpacing);
                sp.setHeight(1);
                sp.setHeightOverride(true);
                builder->pushStyle(sp);
                builder->addText("\n", 1);
                builder->pop();
            }
        }
    } else {
        QByteArray u = content.toUtf8();
        builder->addText(u.constData(), u.size());
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
