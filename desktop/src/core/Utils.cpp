#include "Utils.h"
#include "DwrsDisplay.h"

#include "GistRainbowLut.h"
#include "PlayerStatus.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace fm {

namespace {

// FM prints values in the save's currency: €, £ or $.
void removeCurrencySymbols(QString &s)
{
    s.remove(QChar(0x20AC)); // €
    s.remove(QChar(0x00A3)); // £
    s.remove(QLatin1Char('$'));
}

} // namespace

double valueToFloat(const QString &valueStr)
{
    if (valueStr.isEmpty())
        return 0.0;

    if (valueStr.contains(QLatin1String("not for sale"), Qt::CaseInsensitive))
        return kUnbuyableValue;

    QString s = valueStr;
    // Ranges like "€500K - €800K": legacy takes the lower bound.
    const int rangeSep = s.indexOf(QLatin1String(" - "));
    if (rangeSep >= 0)
        s = s.left(rangeSep);

    removeCurrencySymbols(s);
    s = s.trimmed();

    double multiplier = 1.0;
    if (s.contains(QLatin1Char('M'))) {
        multiplier = 1'000'000.0;
        s.remove(QLatin1Char('M'));
    } else if (s.contains(QLatin1Char('K'))) {
        multiplier = 1'000.0;
        s.remove(QLatin1Char('K'));
    }

    if (s.isEmpty())
        return 0.0;

    bool ok = false;
    const double value = s.toDouble(&ok);
    return ok ? value * multiplier : 0.0;
}

bool isExplicitZeroValue(const QString &rawValue)
{
    QString s = rawValue.trimmed();
    if (s.isEmpty())
        return false; // missing, not an explicit zero
    if (s.contains(QLatin1String("not for sale"), Qt::CaseInsensitive))
        return false;

    // Ranges like "€0 - €0": use the lower bound, matching valueToFloat.
    const int rangeSep = s.indexOf(QLatin1String(" - "));
    if (rangeSep >= 0)
        s = s.left(rangeSep);

    removeCurrencySymbols(s);
    s.remove(QLatin1Char('M'));
    s.remove(QLatin1Char('K'));
    s = s.trimmed();
    if (s.isEmpty())
        return false;

    bool ok = false;
    const double value = s.toDouble(&ok);
    return ok && value == 0.0;
}

double wageToWeekly(const QString &wageRaw)
{
    QString s = wageRaw.trimmed();
    if (s.isEmpty() || s == QLatin1String("-"))
        return -1.0;

    // Period suffix: p/w, p/m, p/a (also p/y), with or without spaces.
    double weeksPerUnit = 1.0;
    static const QRegularExpression period(QStringLiteral("\\s*p\\s*/\\s*([wmay])\\w*\\s*$"),
                                           QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = period.match(s);
    if (match.hasMatch()) {
        const QChar unit = match.captured(1).at(0).toLower();
        if (unit == QLatin1Char('m'))
            weeksPerUnit = 52.0 / 12.0;
        else if (unit == QLatin1Char('a') || unit == QLatin1Char('y'))
            weeksPerUnit = 52.0;
        s.truncate(match.capturedStart());
    }
    const int rangeSep = s.indexOf(QLatin1String(" - "));
    if (rangeSep >= 0)
        s = s.left(rangeSep);
    removeCurrencySymbols(s);
    s = s.trimmed();
    // Thousands separators ("5,000"); a lone decimal comma ("1,5K") becomes a dot.
    static const QRegularExpression thousands(QStringLiteral(",(?=\\d{3}(\\D|$))"));
    s.remove(thousands);
    s.replace(QLatin1Char(','), QLatin1Char('.'));

    double multiplier = 1.0;
    if (s.endsWith(QLatin1Char('M'), Qt::CaseInsensitive)) {
        multiplier = 1'000'000.0;
        s.chop(1);
    } else if (s.endsWith(QLatin1Char('K'), Qt::CaseInsensitive)) {
        multiplier = 1'000.0;
        s.chop(1);
    }
    bool ok = false;
    const double value = s.trimmed().toDouble(&ok);
    if (!ok || value < 0.0)
        return -1.0;
    return value * multiplier / weeksPerUnit;
}

bool isFreeAgent(const QString &club, const QString &transferValueRaw)
{
    const QString c = club.trimmed();
    if (c.isEmpty() || c == QLatin1String("-") || c == QString::fromUtf8("–"))
        return true;
    // Tag the departure dialog writes for released players.
    if (c.compare(PlayerStatus::freeAgentClubTag(), Qt::CaseInsensitive) == 0)
        return true;
    return isExplicitZeroValue(transferValueRaw);
}

QString getLastName(const QString &fullName)
{
    if (fullName.isEmpty())
        return QString();
    const qsizetype lastSpace = fullName.lastIndexOf(QLatin1Char(' '));
    return lastSpace < 0 ? fullName : fullName.mid(lastSpace + 1);
}

QString foldForSearch(const QString &text)
{
    // Accent-/umlaut-insensitive search key: decompose (NFD) so diacritics
    // become separate combining marks, drop those marks, lowercase, and map the
    // few Latin letters that do not decompose. "Müller"/"Muller" -> "muller",
    // "Håland" -> "haland", "Gießen" -> "giessen".
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar ch : decomposed) {
        if (ch.category() == QChar::Mark_NonSpacing)
            continue; // combining accent
        out.append(ch);
    }
    out = out.toLower();
    out.replace(QChar(0x00DF), QStringLiteral("ss")); // ß
    out.replace(QChar(0x00F8), QLatin1Char('o'));     // ø
    out.replace(QChar(0x0142), QLatin1Char('l'));     // ł
    out.replace(QChar(0x0111), QLatin1Char('d'));     // đ
    out.replace(QChar(0x00E6), QStringLiteral("ae")); // æ
    out.replace(QChar(0x0153), QStringLiteral("oe")); // œ
    return out;
}

bool containsFolded(const QString &text, const QString &foldedQuery)
{
    if (foldedQuery.isEmpty())
        return true;
    // Folding pure-ASCII text is just lower-casing, so a case-insensitive
    // search is equivalent and avoids the NFD normalization per row.
    bool ascii = true;
    for (const QChar ch : text) {
        if (ch.unicode() >= 0x80) {
            ascii = false;
            break;
        }
    }
    if (ascii)
        return text.contains(foldedQuery, Qt::CaseInsensitive);
    return foldForSearch(text).contains(foldedQuery);
}

QChar csvSeparator(const QLocale &locale)
{
    return locale.decimalPoint() == QLatin1String(",") ? QLatin1Char(';') : QLatin1Char(',');
}

QString csvField(const QString &value, QChar separator)
{
    if (!value.contains(separator) && !value.contains(QLatin1Char('"'))
        && !value.contains(QLatin1Char('\n')) && !value.contains(QLatin1Char('\r')))
        return value;
    QString quoted = value;
    quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QSet<QString> parsePositionString(const QString &posStr)
{
    QSet<QString> result;
    if (posStr.isEmpty())
        return result;

    // Same pattern as legacy: bases like "D/WB", optional "(RLC)" side group.
    static const QRegularExpression re(
        QStringLiteral(R"(^([A-Z/]+) *(?:\(([RLC]+)\))?$)"));

    const QStringList parts = posStr.split(QLatin1Char(','));
    for (const QString &rawPart : parts) {
        const QString part = rawPart.trimmed();
        const QRegularExpressionMatch match = re.match(part);
        if (!match.hasMatch())
            continue;
        const QString bases = match.captured(1);
        const QString sides = match.captured(2);
        const QStringList baseList = bases.split(QLatin1Char('/'));
        for (const QString &base : baseList) {
            if (!sides.isEmpty()) {
                for (const QChar side : sides)
                    result.insert(base + QStringLiteral(" (") + side + QLatin1Char(')'));
            } else {
                result.insert(base == QLatin1String("ST") ? QStringLiteral("ST (C)") : base);
            }
        }
    }
    return result;
}

int positionSortKey(const QString &posStr)
{
    static const QHash<QString, int> lines = {
        {QStringLiteral("GK"), 0}, {QStringLiteral("D"), 1},  {QStringLiteral("WB"), 2},
        {QStringLiteral("DM"), 3}, {QStringLiteral("M"), 4},  {QStringLiteral("AM"), 5},
        {QStringLiteral("ST"), 6},
    };
    constexpr int kUnknown = 99;

    int minLine = kUnknown;
    int maxLine = -1;
    double sideSum = 0.0;
    int count = 0;
    for (const QString &position : parsePositionString(posStr)) {
        // "D (R)" -> base "D", side 'R'; sideless "DM"/"GK" count as central.
        const int open = position.indexOf(QLatin1Char('('));
        const QString base = (open < 0 ? position : position.left(open)).trimmed();
        const auto line = lines.constFind(base);
        if (line == lines.constEnd())
            continue;
        const QChar side = open < 0 ? QLatin1Char('C') : position.at(open + 1);
        minLine = std::min(minLine, line.value());
        maxLine = std::max(maxLine, line.value());
        sideSum += side == QLatin1Char('R') ? 0.0 : side == QLatin1Char('L') ? 2.0 : 1.0;
        ++count;
    }
    if (count == 0)
        return kUnknown * 10000;
    return minLine * 10000 + maxLine * 1000 + static_cast<int>(std::lround(sideSum / count * 100));
}

QDateTime parseDwrsTimestamp(const QString &timestamp)
{
    const auto precise = QDateTime::fromString(timestamp, QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    return precise.isValid() ? precise : QDateTime::fromString(timestamp, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

double relativeLuminance(const QColor &color)
{
    const auto channel = [](int v) {
        const double s = v / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return channel(color.red()) * 0.2126
         + channel(color.green()) * 0.7152
         + channel(color.blue()) * 0.0722;
}

double contrastRatio(const QColor &a, const QColor &b)
{
    const double lumA = relativeLuminance(a);
    const double lumB = relativeLuminance(b);
    const double lighter = std::max(lumA, lumB);
    const double darker = std::min(lumA, lumB);
    return (lighter + 0.05) / (darker + 0.05);
}

CellStyle dwrsCellStyle(double normalizedValue)
{
    return dwrsCellStyle(normalizedValue, QString());
}

CellStyle rainbowCellStyle(double normalizedValue)
{
    // Same normalization window as legacy: Normalize(vmin=30, vmax=95), clipped.
    constexpr double vmin = 30.0;
    constexpr double vmax = 95.0;
    double t = (normalizedValue - vmin) / (vmax - vmin);
    t = std::clamp(t, 0.0, 1.0);

    // matplotlib maps a float through its 256-entry LUT as int(t * N), N=256.
    const int lutIndex = std::clamp(static_cast<int>(t * 256.0), 0, 255);
    const auto &rgb = kGistRainbowLut[lutIndex];
    const QColor background(rgb[0], rgb[1], rgb[2]);

    CellStyle style;
    style.background = background;
    style.text = relativeLuminance(background) > 0.5 ? QColorConstants::Black
                                                     : QColorConstants::White;
    return style;
}

CellStyle attributeCellStyle(int value)
{
    CellStyle style;
    // Tiers as in legacy: 1-7 red, 8-11 orange, 12-14 yellow, 15-17 light
    // green, 18-20 full green.
    if (value >= 18) {
        style.background = QColor(QStringLiteral("#0da025"));
        style.text = QColorConstants::White;
    } else if (value >= 15) {
        style.background = QColor(QStringLiteral("#a2d31a"));
        style.text = QColorConstants::Black;
    } else if (value >= 12) {
        style.background = QColor(QStringLiteral("#d8d21e"));
        style.text = QColorConstants::Black;
    } else if (value >= 8) {
        style.background = QColor(QStringLiteral("#ca7b3a"));
        style.text = QColorConstants::Black;
    } else if (value >= 1) {
        style.background = QColor(QStringLiteral("#cf1e1e"));
        style.text = QColorConstants::Black;
    }
    return style;
}

CellStyle personalityCellStyle(const QString &category)
{
    CellStyle style;
    if (category == QLatin1String("good")) {
        style.background = QColor(QStringLiteral("#0da025"));
        style.text = QColorConstants::White;
    } else if (category == QLatin1String("neutral")) {
        style.background = QColor(QStringLiteral("#d8d21e"));
        style.text = QColorConstants::Black;
    } else if (category == QLatin1String("bad")) {
        style.background = QColor(QStringLiteral("#cf1e1e"));
        style.text = QColorConstants::White;
    }
    return style;
}

int footStrengthLevel(const QString &text)
{
    const QString t = text.trimmed().toLower();
    if (t.isEmpty())
        return 0;
    bool isNumber = false;
    const int raw = t.toInt(&isNumber);
    if (isNumber) {
        // Same steps as the game (and Fm27SaveImport::footText).
        if (raw < 1 || raw > 20)
            return 0;
        if (raw >= 18)
            return 6;
        if (raw >= 15)
            return 5;
        if (raw >= 12)
            return 4;
        if (raw >= 9)
            return 3;
        if (raw >= 5)
            return 2;
        return 1;
    }
    static const QHash<QString, int> levels = {
        {QStringLiteral("very weak"), 1},      {QStringLiteral("weak"), 2},
        {QStringLiteral("reasonable"), 3},     {QStringLiteral("fairly strong"), 4},
        {QStringLiteral("strong"), 5},         {QStringLiteral("very strong"), 6},
        // German game language
        {QStringLiteral("sehr schwach"), 1},   {QStringLiteral("schwach"), 2},
        {QStringLiteral("ordentlich"), 3},     {QStringLiteral("ausreichend"), 3},
        {QStringLiteral("ziemlich stark"), 4}, {QStringLiteral("recht stark"), 4},
        {QStringLiteral("stark"), 5},          {QStringLiteral("sehr stark"), 6},
    };
    return levels.value(t, 0);
}

int footStrengthAttributeValue(int level)
{
    static constexpr int values[] = {0, 3, 9, 12, 13, 16, 19};
    return level >= 1 && level <= kFootStrengthLevels ? values[level] : 0;
}

} // namespace fm
