#pragma once

#include "Types.h"

#include <QMap>
#include <QString>

#include <vector>

namespace drav {

// Document-wide styles and variables. Held by the Document node's props behind a shared pointer
// that is replaced on every edit, so undo snapshots stay cheap.

enum class StyleType : std::uint8_t { Fill, Text, Effect, Grid };

struct Style {
    QString id;
    QString name; // "Group/Name" groups styles in the UI
    StyleType type = StyleType::Fill;
    QString description;
    std::vector<Paint> paints;     // Fill (used for fills and strokes)
    TextStyle text;                // Text: typography only
    std::vector<Effect> effects;   // Effect
    std::vector<LayoutGrid> grids; // Grid
    bool operator==(const Style&) const = default;
};

enum class VarType : std::uint8_t { Color, Number, String, Boolean };

struct VarValue {
    SkColor4f color = SkColors::kBlack;
    float number = 0;
    QString string;
    bool boolean = false;
    QString alias; // non-empty: the value is taken from this variable
    bool operator==(const VarValue&) const = default;
};

struct VarMode {
    QString id;
    QString name;
    bool operator==(const VarMode&) const = default;
};

struct VarCollection {
    QString id;
    QString name;
    std::vector<VarMode> modes; // never empty; the first is the default
    bool operator==(const VarCollection&) const = default;
};

struct Variable {
    QString id;
    QString name; // "Group/Name"
    QString collection;
    VarType type = VarType::Color;
    QString description;
    QMap<QString, VarValue> values; // mode id -> value
    bool operator==(const Variable&) const = default;
};

struct Library {
    std::vector<Style> styles;
    std::vector<VarCollection> collections;
    std::vector<Variable> variables;
    bool operator==(const Library&) const = default;

    bool empty() const { return styles.empty() && collections.empty() && variables.empty(); }
    const Style* style(const QString& id) const;
    Style* style(const QString& id);
    const Variable* variable(const QString& id) const;
    Variable* variable(const QString& id);
    const VarCollection* collection(const QString& id) const;
    VarCollection* collection(const QString& id);

    static QString newId();
};

const char* styleTypeName(StyleType t);
StyleType styleTypeFromName(const QString& s);
const char* varTypeName(VarType t);
VarType varTypeFromName(const QString& s);

// Copies the typographic part of a text style (family, weight, size, spacing, case, decoration).
void applyTypography(TextStyle& dst, const TextStyle& src);
bool sameTypography(const TextStyle& a, const TextStyle& b);

} // namespace drav
