#include "DwrsDisplay.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace fm {

namespace {
int g_decimals = 0;
QLocale g_locale;
DwrsColorMode g_mode = DwrsColorMode::Tiers;
DwrsTierThresholds g_field = defaultFieldTierThresholds(true);
DwrsTierThresholds g_goalkeeper = defaultGoalkeeperTierThresholds();
DwrsTierColors g_colors = defaultDwrsTierColors();
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

DwrsTierThresholds defaultFieldTierThresholds(bool classic)
{
    return classic ? DwrsTierThresholds{48, 55, 61, 68, 76} : DwrsTierThresholds{55, 65, 72, 78, 84};
}

DwrsTierThresholds defaultGoalkeeperTierThresholds() { return {48, 55, 61, 68, 76}; }

DwrsTierColors defaultDwrsTierColors()
{
    return {QColor(0x9A, 0xA0, 0xA6), QColor(), QColor(0x2E, 0x9E, 0x4F),
            QColor(0x2F, 0x7F, 0xD8), QColor(0x8E, 0x4E, 0xC6), QColor(0xE8, 0x89, 0x1C)};
}

void setDwrsColorMode(DwrsColorMode mode) { g_mode = mode; }
DwrsColorMode dwrsColorMode() { return g_mode; }
void setDwrsTierThresholds(const DwrsTierThresholds &field, const DwrsTierThresholds &goalkeeper)
{
    g_field = field;
    g_goalkeeper = goalkeeper;
}
void setDwrsTierColors(const DwrsTierColors &colors) { g_colors = colors; }
void setDwrsGoalkeeperRoles(const QSet<QString> &roles) { g_goalkeeperRoles = roles; }
bool isDwrsGoalkeeperRole(const QString &role)
{
    // Shown abbreviations drop the phase suffix ("GK" for "GK-IP").
    return g_goalkeeperRoles.contains(role) || g_goalkeeperRoles.contains(role + QLatin1String("-IP"))
           || g_goalkeeperRoles.contains(role + QLatin1String("-OOP"));
}

int dwrsTier(double value, bool goalkeeper)
{
    const auto &thresholds = goalkeeper ? g_goalkeeper : g_field;
    int tier = 0;
    for (int i = 0; i < int(thresholds.size()); ++i)
        if (roundDwrs(value) >= thresholds[i])
            tier = i + 1;
    return tier;
}

CellStyle dwrsTierStyleFor(const QColor &background) { return withReadableText(background); }

CellStyle dwrsTierStyle(int tier)
{
    return withReadableText(g_colors[std::clamp(tier, 0, kDwrsTierCount - 1)]);
}

CellStyle dwrsCellStyle(double normalizedValue, const QString &role)
{
    return dwrsCellStyleFor(normalizedValue, isDwrsGoalkeeperRole(role));
}

CellStyle dwrsCellStyleFor(double normalizedValue, bool goalkeeper)
{
    if (g_mode == DwrsColorMode::Rainbow)
        return rainbowCellStyle(normalizedValue);
    return dwrsTierStyle(dwrsTier(normalizedValue, goalkeeper));
}

CellStyle dwrsComparisonStyle(double delta)
{
    // Calm diverging scale: warm (below the squad) to cool (above).
    static const QColor strongBelow(0xD9, 0x7A, 0x4A), lightBelow(0xF0, 0xC2, 0x9E);
    static const QColor lightAbove(0xA9, 0xCB, 0xE8), strongAbove(0x3F, 0x83, 0xC4);
    const double d = roundDwrs(delta);
    if (std::abs(d) <= 1.5)
        return {};
    if (d < -4.0) return withReadableText(strongBelow);
    if (d < 0) return withReadableText(lightBelow);
    if (d > 4.0) return withReadableText(strongAbove);
    return withReadableText(lightAbove);
}

QString colorsToString(const DwrsTierColors &colors)
{
    QStringList parts;
    for (const QColor &color : colors)
        parts << (color.isValid() ? color.name() : QStringLiteral("none"));
    return parts.join(QLatin1Char(','));
}

DwrsTierColors colorsFromString(const QString &text)
{
    DwrsTierColors result = defaultDwrsTierColors();
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() != kDwrsTierCount)
        return result;
    for (int i = 0; i < kDwrsTierCount; ++i) {
        if (parts[i] == QLatin1String("none")) { result[i] = QColor(); continue; }
        const QColor color(parts[i]);
        if (color.isValid())
            result[i] = color;
    }
    return result;
}

} // namespace fm
