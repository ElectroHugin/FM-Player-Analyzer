#pragma once

#include "Utils.h"

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace fm {

// One colour band of a DWRS colour scheme. It starts at its threshold (the
// first band starts at 0); an invalid colour means "no fill".
struct DwrsColorStop {
    double field = 0.0;      // threshold for outfield roles
    double goalkeeper = 0.0; // threshold for goalkeeper roles
    QColor color;
    bool operator==(const DwrsColorStop &other) const = default;
};

// How DWRS values are coloured. App setting, independent of the rating
// profile. Built-in schemes are fixed; own schemes are stored app-wide.
struct DwrsColorScheme {
    QString id;
    QString name;
    bool builtin = false;
    bool rainbow = false; // "Original": the continuous 2.2 rainbow scale
    double blend = 0.0;   // points on each side of a threshold where colours mix (0 = hard)
    QList<DwrsColorStop> stops;

    static constexpr int kMaxStops = 10;
    static constexpr double kMaxBlend = 10.0;

    CellStyle style(double value, bool goalkeeper) const;
    // An own, editable copy (the rainbow becomes sampled stops).
    DwrsColorScheme editableCopy(const QString &newId, const QString &newName) const;
    // Empty when valid, otherwise a short English reason.
    QString validate() const;
    QJsonObject toJson() const;
    static std::optional<DwrsColorScheme> fromJson(const QJsonObject &json, QString *error);
    bool sameColors(const DwrsColorScheme &other) const;
};

QString originalDwrsColorSchemeId();          // "original", the default
QList<DwrsColorScheme> builtinDwrsColorSchemes(); // translated names
std::optional<DwrsColorScheme> builtinDwrsColorScheme(const QString &id);

// Own schemes file (JSON). A missing file is an empty list.
QList<DwrsColorScheme> loadDwrsColorSchemes(const QString &path, QString *error);
bool saveDwrsColorSchemes(const QString &path, const QList<DwrsColorScheme> &schemes, QString *error);

} // namespace fm
