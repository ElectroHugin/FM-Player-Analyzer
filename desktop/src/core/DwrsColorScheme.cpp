#include "DwrsColorScheme.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace fm {

namespace {
const char *kContext = "DwrsColorScheme";

CellStyle readable(const QColor &background)
{
    CellStyle style;
    if (!background.isValid())
        return style;
    style.background = background;
    const QColor black = QColorConstants::Black, white = QColorConstants::White;
    style.text = contrastRatio(background, black) >= contrastRatio(background, white) ? black : white;
    return style;
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t),
                            float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

double threshold(const DwrsColorStop &stop, bool goalkeeper)
{
    return goalkeeper ? stop.goalkeeper : stop.field;
}

DwrsColorScheme make(const QString &id, const char *name, double blend,
                     std::initializer_list<DwrsColorStop> stops)
{
    DwrsColorScheme scheme;
    scheme.id = id;
    scheme.name = QCoreApplication::translate(kContext, name);
    scheme.builtin = true;
    scheme.blend = blend;
    scheme.stops = stops;
    return scheme;
}
} // namespace

QString originalDwrsColorSchemeId() { return QStringLiteral("original"); }

QList<DwrsColorScheme> builtinDwrsColorSchemes()
{
    DwrsColorScheme original;
    original.id = originalDwrsColorSchemeId();
    original.name = QCoreApplication::translate(kContext, QT_TRANSLATE_NOOP("DwrsColorScheme", "Original (Regenbogen)"));
    original.builtin = true;
    original.rainbow = true;
    // Shown in the editor and used for a copy: samples of the rainbow.
    original = original.editableCopy(original.id, original.name);
    original.builtin = true;
    original.rainbow = true;
    return {
        original,
        // Rarity tiers as in role-playing games; "Common" has no fill.
        make(QStringLiteral("rarity"), QT_TRANSLATE_NOOP("DwrsColorScheme", "Seltenheit"), 0.0,
             {{0, 0, QColor(0x9A, 0xA0, 0xA6)},
              {55, 48, QColor()},
              {65, 55, QColor(0x2E, 0x9E, 0x4F)},
              {72, 61, QColor(0x2F, 0x7F, 0xD8)},
              {78, 69, QColor(0x8E, 0x4E, 0xC6)},
              {84, 78, QColor(0xE8, 0x89, 0x1C)}}),
        // Red to green with soft borders.
        make(QStringLiteral("traffic-light"), QT_TRANSLATE_NOOP("DwrsColorScheme", "Ampel"), 2.0,
             {{0, 0, QColor(0xD6, 0x45, 0x45)},
              {60, 52, QColor(0xE8, 0x89, 0x1C)},
              {68, 60, QColor(0xE3, 0xC3, 0x41)},
              {75, 67, QColor(0x8C, 0xC1, 0x52)},
              {82, 74, QColor(0x2E, 0x9E, 0x4F)}}),
        // Viridis-like: readable with red-green colour blindness.
        make(QStringLiteral("viridis"), QT_TRANSLATE_NOOP("DwrsColorScheme", "Farbenblind-freundlich"), 3.0,
             {{0, 0, QColor(0x3B, 0x52, 0x8B)},
              {62, 54, QColor(0x21, 0x91, 0x8C)},
              {72, 64, QColor(0x5E, 0xC9, 0x62)},
              {80, 72, QColor(0xB5, 0xDE, 0x2B)},
              {86, 78, QColor(0xFD, 0xE7, 0x25)}}),
        // One hue, light to dark: calm tables.
        make(QStringLiteral("mono"), QT_TRANSLATE_NOOP("DwrsColorScheme", "Dezent (Lila)"), 3.0,
             {{0, 0, QColor()},
              {60, 52, QColor(0xE4, 0xD8, 0xF5)},
              {70, 62, QColor(0xC3, 0xA6, 0xEC)},
              {78, 70, QColor(0x9B, 0x6B, 0xDB)},
              {85, 77, QColor(0x6A, 0x3B, 0xB0)}}),
    };
}

std::optional<DwrsColorScheme> builtinDwrsColorScheme(const QString &id)
{
    for (const auto &scheme : builtinDwrsColorSchemes())
        if (scheme.id == id)
            return scheme;
    return std::nullopt;
}

CellStyle DwrsColorScheme::style(double value, bool goalkeeper) const
{
    if (rainbow)
        return rainbowCellStyle(value);
    if (stops.isEmpty())
        return {};
    int band = 0;
    for (int i = 1; i < stops.size(); ++i)
        if (value >= threshold(stops[i], goalkeeper))
            band = i;
    QColor color = stops[band].color;
    if (blend > 0.0) {
        // Near a border both neighbours mix; a band without fill never mixes.
        const auto mixAt = [&](int upper) -> std::optional<QColor> {
            const QColor &low = stops[upper - 1].color, &high = stops[upper].color;
            const double border = threshold(stops[upper], goalkeeper);
            if (!low.isValid() || !high.isValid() || std::abs(value - border) >= blend)
                return std::nullopt;
            return mix(low, high, (value - (border - blend)) / (2.0 * blend));
        };
        if (band + 1 < stops.size())
            if (const auto mixed = mixAt(band + 1)) color = *mixed;
        if (band > 0)
            if (const auto mixed = mixAt(band)) color = *mixed;
    }
    return readable(color);
}

DwrsColorScheme DwrsColorScheme::editableCopy(const QString &newId, const QString &newName) const
{
    DwrsColorScheme copy = *this;
    copy.id = newId;
    copy.name = newName;
    copy.builtin = false;
    if (rainbow) {
        // Six samples of the 30..95 rainbow, borders halfway, soft borders.
        copy.rainbow = false;
        copy.stops.clear();
        const double centres[] = {30, 43, 56, 69, 82, 95};
        for (int i = 0; i < 6; ++i) {
            const double from = i == 0 ? 0.0 : (centres[i - 1] + centres[i]) / 2.0;
            copy.stops.append({from, from, rainbowCellStyle(centres[i]).background});
        }
        copy.blend = 6.5;
    }
    return copy;
}

QString DwrsColorScheme::validate() const
{
    if (rainbow)
        return {};
    if (stops.isEmpty() || stops.size() > kMaxStops)
        return QStringLiteral("stop-count");
    if (!(blend >= 0.0 && blend <= kMaxBlend))
        return QStringLiteral("blend");
    for (int i = 1; i < stops.size(); ++i) {
        for (const bool keeper : {false, true}) {
            const double here = threshold(stops[i], keeper), before = threshold(stops[i - 1], keeper);
            if (!(here >= 0.0 && here <= 100.0) || (i > 1 && here <= before))
                return QStringLiteral("thresholds");
        }
    }
    return {};
}

QJsonObject DwrsColorScheme::toJson() const
{
    QJsonArray array;
    for (const auto &stop : stops)
        array.append(QJsonObject{{"field", stop.field},
                                 {"goalkeeper", stop.goalkeeper},
                                 {"color", stop.color.isValid() ? stop.color.name() : QStringLiteral("none")}});
    QJsonObject json{{"id", id}, {"name", name}, {"blend", blend}, {"stops", array}};
    if (rainbow)
        json.insert(QStringLiteral("rainbow"), true);
    return json;
}

std::optional<DwrsColorScheme> DwrsColorScheme::fromJson(const QJsonObject &json, QString *error)
{
    DwrsColorScheme scheme;
    scheme.id = json.value(QStringLiteral("id")).toString();
    scheme.name = json.value(QStringLiteral("name")).toString();
    scheme.blend = json.value(QStringLiteral("blend")).toDouble();
    scheme.rainbow = json.value(QStringLiteral("rainbow")).toBool();
    for (const auto &value : json.value(QStringLiteral("stops")).toArray()) {
        const QJsonObject stop = value.toObject();
        const QString colorText = stop.value(QStringLiteral("color")).toString();
        const QColor color = colorText == QLatin1String("none") ? QColor() : QColor(colorText);
        if (colorText != QLatin1String("none") && !color.isValid()) {
            if (error) *error = QStringLiteral("color");
            return std::nullopt;
        }
        scheme.stops.append({stop.value(QStringLiteral("field")).toDouble(),
                             stop.value(QStringLiteral("goalkeeper")).toDouble(), color});
    }
    if (scheme.id.isEmpty()) {
        if (error) *error = QStringLiteral("id");
        return std::nullopt;
    }
    const QString reason = scheme.validate();
    if (!reason.isEmpty()) {
        if (error) *error = reason;
        return std::nullopt;
    }
    return scheme;
}

bool DwrsColorScheme::sameColors(const DwrsColorScheme &other) const
{
    return rainbow == other.rainbow && blend == other.blend && stops == other.stops;
}

QList<DwrsColorScheme> loadDwrsColorSchemes(const QString &path, QString *error)
{
    QList<DwrsColorScheme> result;
    QFile file(path);
    if (!file.exists())
        return result;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return result;
    }
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).object()
                                 .value(QStringLiteral("schemes")).toArray();
    for (const auto &value : array) {
        QString reason;
        if (auto scheme = DwrsColorScheme::fromJson(value.toObject(), &reason))
            result.append(*scheme);
        else if (error)
            *error = reason;
    }
    return result;
}

bool saveDwrsColorSchemes(const QString &path, const QList<DwrsColorScheme> &schemes, QString *error)
{
    QJsonArray array;
    for (const auto &scheme : schemes)
        array.append(scheme.toJson());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(QJsonObject{{"format", "fm-dwrs-color-schemes"}, {"version", 1},
                                         {"schemes", array}}).toJson());
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace fm
