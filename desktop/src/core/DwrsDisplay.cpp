#include "DwrsDisplay.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace fm {

namespace {
int g_decimals = 0;
QLocale g_locale;
DwrsColorScheme &activeScheme()
{
    static DwrsColorScheme scheme = *builtinDwrsColorScheme(originalDwrsColorSchemeId());
    return scheme;
}
QSet<QString> g_goalkeeperRoles;

CellStyle withReadableText(const QColor &background)
{
    CellStyle style;
    if (!background.isValid())
        return style;
    style.background = background;
    const QColor black = QColorConstants::Black, white = QColorConstants::White;
    style.text = contrastRatio(background, black) >= contrastRatio(background, white) ? black : white;
    return style;
}
} // namespace

void setDwrsDecimals(int decimals) { g_decimals = std::clamp(decimals, 0, 2); }
int dwrsDecimals() { return g_decimals; }
void setDwrsLocale(const QLocale &locale) { g_locale = locale; }

double roundDwrs(double value)
{
    const double scale = std::pow(10.0, g_decimals);
    return std::round(value * scale) / scale;
}

QString formatDwrs(double value)
{
    QLocale locale = g_locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    return locale.toString(roundDwrs(value), 'f', g_decimals);
}

QString formatDwrsPercent(double value) { return formatDwrs(value) + QLatin1Char('%'); }

QString formatDwrsDelta(double delta)
{
    const double rounded = roundDwrs(delta);
    if (rounded == 0.0)
        return formatDwrs(0.0);
    return (rounded > 0 ? QStringLiteral("+") : QString()) + formatDwrs(rounded);
}

void setDwrsColorScheme(const DwrsColorScheme &scheme) { activeScheme() = scheme; }
const DwrsColorScheme &dwrsColorScheme() { return activeScheme(); }
void setDwrsGoalkeeperRoles(const QSet<QString> &roles) { g_goalkeeperRoles = roles; }
bool isDwrsGoalkeeperRole(const QString &role)
{
    // Shown abbreviations drop the phase suffix ("GK" for "GK-IP").
    return g_goalkeeperRoles.contains(role) || g_goalkeeperRoles.contains(role + QLatin1String("-IP"))
           || g_goalkeeperRoles.contains(role + QLatin1String("-OOP"));
}

CellStyle dwrsReadableStyle(const QColor &background) { return withReadableText(background); }

CellStyle dwrsCellStyle(double normalizedValue, const QString &role)
{
    return dwrsCellStyleFor(normalizedValue, isDwrsGoalkeeperRole(role));
}

CellStyle dwrsCellStyleFor(double normalizedValue, bool goalkeeper)
{
    // The shown value decides, so a cell never contradicts its number.
    return activeScheme().style(roundDwrs(normalizedValue), goalkeeper);
}

namespace {
// Calm diverging scale: warm (below the squad) to cool (above), grey between.
const QColor kStrongBelow(0xD9, 0x7A, 0x4A), kLightBelow(0xF0, 0xC2, 0x9E);
const QColor kNeutral(0xD6, 0xD9, 0xDF);
const QColor kLightAbove(0xA9, 0xCB, 0xE8), kStrongAbove(0x3F, 0x83, 0xC4);
} // namespace

CellStyle dwrsComparisonStyle(double delta)
{
    const double d = roundDwrs(delta);
    if (std::abs(d) <= 1.5)
        return {};
    if (d < -4.0) return withReadableText(kStrongBelow);
    if (d < 0) return withReadableText(kLightBelow);
    if (d > 4.0) return withReadableText(kStrongAbove);
    return withReadableText(kLightAbove);
}

QList<QColor> dwrsComparisonColors()
{
    return {kStrongBelow, kLightBelow, kNeutral, kLightAbove, kStrongAbove};
}

CellStyle dwrsComparisonNeutralStyle() { return withReadableText(kNeutral); }

} // namespace fm
