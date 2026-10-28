#include "Library.h"

#include <QUuid>

#include <algorithm>

namespace drav {

namespace {
template <typename T> auto findById(T& v, const QString& id) -> decltype(&v.front())
{
    auto it = std::find_if(v.begin(), v.end(), [&](const auto& x) { return x.id == id; });
    return it == v.end() ? nullptr : &*it;
}

const char* const kStyleTypes[] = {"FILL", "TEXT", "EFFECT", "GRID"};
const char* const kVarTypes[] = {"COLOR", "FLOAT", "STRING", "BOOLEAN"};
} // namespace

const Style* Library::style(const QString& id) const { return id.isEmpty() ? nullptr : findById(styles, id); }
Style* Library::style(const QString& id) { return id.isEmpty() ? nullptr : findById(styles, id); }
const Variable* Library::variable(const QString& id) const { return id.isEmpty() ? nullptr : findById(variables, id); }
Variable* Library::variable(const QString& id) { return id.isEmpty() ? nullptr : findById(variables, id); }
const VarCollection* Library::collection(const QString& id) const
{
    return id.isEmpty() ? nullptr : findById(collections, id);
}
VarCollection* Library::collection(const QString& id) { return id.isEmpty() ? nullptr : findById(collections, id); }

QString Library::newId() { return QUuid::createUuid().toString(QUuid::Id128).left(16); }

const char* styleTypeName(StyleType t) { return kStyleTypes[int(t)]; }
StyleType styleTypeFromName(const QString& s)
{
    for (int i = 0; i < int(std::size(kStyleTypes)); ++i)
        if (s == QLatin1String(kStyleTypes[i]))
            return StyleType(i);
    return StyleType::Fill;
}
const char* varTypeName(VarType t) { return kVarTypes[int(t)]; }
VarType varTypeFromName(const QString& s)
{
    for (int i = 0; i < int(std::size(kVarTypes)); ++i)
        if (s == QLatin1String(kVarTypes[i]))
            return VarType(i);
    return VarType::Color;
}

void applyTypography(TextStyle& dst, const TextStyle& src)
{
    dst.family = src.family;
    dst.features = src.features;
    dst.axes = src.axes;
    dst.weight = src.weight;
    dst.italic = src.italic;
    dst.size = src.size;
    dst.lineHeight = src.lineHeight;
    dst.lineHeightPercent = src.lineHeightPercent;
    dst.letterSpacing = src.letterSpacing;
    dst.paragraphSpacing = src.paragraphSpacing;
    dst.textCase = src.textCase;
    dst.decoration = src.decoration;
}

bool sameTypography(const TextStyle& a, const TextStyle& b)
{
    TextStyle t = a;
    applyTypography(t, b);
    return t == a;
}

} // namespace drav
