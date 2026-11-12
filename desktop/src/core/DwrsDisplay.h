#pragma once

#include "DwrsColorScheme.h"
#include "Utils.h"

#include <QColor>
#include <QLocale>
#include <QSet>
#include <QString>


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

// --- Colours -----------------------------------------------------------------
// The active colour scheme (Settings -> DWRS colours); default "Original".
void setDwrsColorScheme(const DwrsColorScheme &scheme);
const DwrsColorScheme &dwrsColorScheme();
void setDwrsGoalkeeperRoles(const QSet<QString> &roles);
bool isDwrsGoalkeeperRole(const QString &role);

// Any fill with the text colour of higher contrast.
CellStyle dwrsReadableStyle(const QColor &background);
// The colour for a value in a role (goalkeeper roles use their own thresholds).
CellStyle dwrsCellStyle(double normalizedValue, const QString &role);
CellStyle dwrsCellStyleFor(double normalizedValue, bool goalkeeper);

// Squad comparison (dashboard): distance to the squad median in fixed point
// steps. Up to 1.5 neutral (invalid style), 1.5-4 light, beyond strong; warm
// below, cool above.
CellStyle dwrsComparisonStyle(double deltaToMedian);

} // namespace fm
