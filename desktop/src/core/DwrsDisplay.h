#pragma once

#include "Utils.h"

#include <QColor>
#include <QLocale>
#include <QSet>
#include <QString>

#include <array>

namespace fm {

// How DWRS values are shown: precision, decimal separator and colour tiers.
// Set on the UI thread when a save or its rating profile changes; read by
// display code only (never by rating workers).

// --- Precision ---------------------------------------------------------------
// Classic keeps whole numbers; curve profiles show one decimal.
void setDwrsDecimals(int decimals);
int dwrsDecimals();
void setDwrsLocale(const QLocale &locale); // decimal separator of the UI language
double roundDwrs(double value);             // to the shown precision
QString formatDwrs(double value);           // "86.3" / "86,3" / "86"
QString formatDwrsPercent(double value);    // "86.3%"
QString formatDwrsDelta(double delta);      // "+1.3", "-0.4", "0"

// --- Colour tiers ------------------------------------------------------------
// Six rarity tiers: Poor, Common, Uncommon, Rare, Epic, Legendary. Common has
// no fill. Thresholds (5 values, from Common up) belong to the rating profile:
// one set for outfield roles, one for goalkeeper roles.
inline constexpr int kDwrsTierCount = 6;
using DwrsTierThresholds = std::array<double, kDwrsTierCount - 1>;
using DwrsTierColors = std::array<QColor, kDwrsTierCount>;

DwrsTierThresholds defaultFieldTierThresholds(bool classic);
DwrsTierThresholds defaultGoalkeeperTierThresholds();
DwrsTierColors defaultDwrsTierColors(); // Common is invalid = no fill

enum class DwrsColorMode { Tiers, Rainbow };
void setDwrsColorMode(DwrsColorMode mode);
DwrsColorMode dwrsColorMode();
void setDwrsTierThresholds(const DwrsTierThresholds &field, const DwrsTierThresholds &goalkeeper);
void setDwrsTierColors(const DwrsTierColors &colors);
void setDwrsGoalkeeperRoles(const QSet<QString> &roles);
bool isDwrsGoalkeeperRole(const QString &role);

// 0 (Poor) .. 5 (Legendary).
int dwrsTier(double value, bool goalkeeper = false);
// Tier fill with the text colour of higher contrast (>= 4.5:1 for the defaults).
CellStyle dwrsTierStyle(int tier);
CellStyle dwrsTierStyleFor(const QColor &background); // readable text on any fill
// The colour for a value in a role (goalkeeper roles use their own thresholds).
CellStyle dwrsCellStyle(double normalizedValue, const QString &role);
CellStyle dwrsCellStyleFor(double normalizedValue, bool goalkeeper);

// Squad comparison (dashboard): distance to the squad median in fixed point
// steps. Up to 1.5 neutral (invalid style), 1.5-4 light, beyond strong; warm
// below, cool above.
CellStyle dwrsComparisonStyle(double deltaToMedian);

QString colorsToString(const DwrsTierColors &colors);
DwrsTierColors colorsFromString(const QString &text); // falls back to defaults

} // namespace fm
