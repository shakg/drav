#include "Color.h"

#include "Library.h"
#include "Node.h"

#include <modules/skcms/skcms.h>

#include <cmath>

namespace drav::color {

sk_sp<SkColorSpace> space(ColorProfile p)
{
    if (p == ColorProfile::DisplayP3)
        return SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB, SkNamedGamut::kDisplayP3);
    return nullptr;
}

namespace {

float toLinear(float v)
{
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

float fromLinear(float v)
{
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f;
}

} // namespace

SkColor4f convert(SkColor4f c, ColorProfile from, ColorProfile to)
{
    if (from == to)
        return c;
    sk_sp<SkColorSpace> src = SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB,
        from == ColorProfile::DisplayP3 ? SkNamedGamut::kDisplayP3 : SkNamedGamut::kSRGB);
    sk_sp<SkColorSpace> dst = SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB,
        to == ColorProfile::DisplayP3 ? SkNamedGamut::kDisplayP3 : SkNamedGamut::kSRGB);
    skcms_Matrix3x3 m;
    src->gamutTransformTo(dst.get(), &m);
    const float in[3] = {toLinear(c.fR), toLinear(c.fG), toLinear(c.fB)};
    float out[3];
    for (int i = 0; i < 3; ++i) {
        const float v = m.vals[i][0] * in[0] + m.vals[i][1] * in[1] + m.vals[i][2] * in[2];
        out[i] = std::clamp(fromLinear(std::max(v, 0.f)), 0.f, 1.f);
    }
    return {out[0], out[1], out[2], c.fA};
}

const char* profileName(ColorProfile p)
{
    return p == ColorProfile::DisplayP3 ? "display-p3" : "srgb";
}

ColorProfile profileFromName(const QString& s)
{
    return s.compare(QLatin1String("display-p3"), Qt::CaseInsensitive) == 0 ? ColorProfile::DisplayP3 : ColorProfile::SRGB;
}

namespace {

void paints(std::vector<Paint>& v, const std::function<void(SkColor4f&)>& f)
{
    for (Paint& pt : v) {
        f(pt.color);
        for (ColorStop& s : pt.stops)
            f(s.color);
    }
}

void effects(std::vector<Effect>& v, const std::function<void(SkColor4f&)>& f)
{
    for (Effect& e : v)
        f(e.color);
}

void grids(std::vector<LayoutGrid>& v, const std::function<void(SkColor4f&)>& f)
{
    for (LayoutGrid& g : v)
        f(g.color);
}

} // namespace

void forEachColor(Props& p, const std::function<void(SkColor4f&)>& f)
{
    paints(p.fills, f);
    paints(p.strokes, f);
    effects(p.effects, f);
    grids(p.grids, f);
    for (TextRun& r : p.textRuns)
        f(r.fill);
    f(p.pageBackground);
}

void forEachColor(Library& lib, const std::function<void(SkColor4f&)>& f)
{
    for (Style& s : lib.styles) {
        paints(s.paints, f);
        effects(s.effects, f);
        grids(s.grids, f);
    }
    for (Variable& v : lib.variables)
        if (v.type == VarType::Color)
            for (VarValue& val : v.values)
                f(val.color);
}

} // namespace drav::color
