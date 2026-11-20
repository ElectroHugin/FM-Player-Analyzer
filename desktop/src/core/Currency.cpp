#include "Currency.h"

#include <QCoreApplication>
#include <QLocale>
#include <QRegularExpression>

#include <array>
#include <cmath>

namespace fm {
namespace Currency {

namespace {

struct Info {
    const char *code;
    const char *symbol; // UTF-8
    bool symbolAfter;
    double perPound;
};

// Rates as stored in the FM27 database (current rate per pound); the euro
// rate is the one the save import has always used.
constexpr std::array<Info, 6> kCurrencies{{
    {"GBP", "£", false, 1.0},
    {"USD", "$", false, 1.3476190567016602},
    {"EUR", "€", false, 1.1641011238098145},
    {"CHF", "CHF ", false, 1.098191738128662},
    {"CNY", "\xef\xbf\xa5", false, 9.058795928955078}, // U+FFE5 as the game writes it
    {"RUB", " \xd1\x80\xd1\x83\xd0\xb1", true, 117.63680267333984}, // " руб"
}};

QString g_display = QStringLiteral("EUR");

const Info *find(const QString &code)
{
    for (const Info &info : kCurrencies)
        if (code == QLatin1String(info.code))
            return &info;
    return nullptr;
}

QString trimmed(double v, int decimals)
{
    QString t = QString::number(v, 'f', decimals);
    if (t.contains(QLatin1Char('.'))) {
        while (t.endsWith(QLatin1Char('0')))
            t.chop(1);
        if (t.endsWith(QLatin1Char('.')))
            t.chop(1);
    }
    return t;
}

QString withSymbol(const QString &number)
{
    const Info *info = find(g_display);
    const QString symbol = QString::fromUtf8(info->symbol);
    return info->symbolAfter ? number + symbol : symbol + number;
}

} // namespace

QStringList codes()
{
    QStringList out;
    for (const Info &info : kCurrencies)
        out << QString::fromLatin1(info.code);
    return out;
}

QString defaultFor(const QString &uiLanguage)
{
    if (uiLanguage.startsWith(QLatin1String("zh")))
        return QStringLiteral("CNY");
    if (uiLanguage == QLatin1String("ru"))
        return QStringLiteral("RUB");
    if (uiLanguage == QLatin1String("en"))
        return QStringLiteral("GBP");
    return QStringLiteral("EUR");
}

QString label(const QString &code)
{
    const char *name = code == QLatin1String("GBP")   ? QT_TRANSLATE_NOOP("fm::Currency", "British pound")
                       : code == QLatin1String("USD") ? QT_TRANSLATE_NOOP("fm::Currency", "US dollar")
                       : code == QLatin1String("EUR") ? QT_TRANSLATE_NOOP("fm::Currency", "Euro")
                       : code == QLatin1String("CHF") ? QT_TRANSLATE_NOOP("fm::Currency", "Swiss franc")
                       : code == QLatin1String("CNY") ? QT_TRANSLATE_NOOP("fm::Currency", "Chinese yuan")
                       : code == QLatin1String("RUB") ? QT_TRANSLATE_NOOP("fm::Currency", "Russian ruble")
                                                      : nullptr;
    const Info *info = find(code);
    if (!name || !info)
        return code;
    return QStringLiteral("%1 (%2)").arg(QCoreApplication::translate("fm::Currency", name),
                                         QString::fromUtf8(info->symbol).trimmed());
}

void setDisplay(const QString &code)
{
    if (find(code))
        g_display = code;
}

QString display()
{
    return g_display;
}

double perPound(const QString &code)
{
    const Info *info = find(code);
    return info ? info->perPound : 1.0;
}

double convert(double amount, const QString &fromCode, const QString &toCode)
{
    if (fromCode == toCode)
        return amount;
    return amount / perPound(fromCode) * perPound(toCode);
}

double toDisplay(double amount, const QString &dataCode)
{
    return convert(amount, dataCode, g_display);
}

double fromDisplay(double amount, const QString &dataCode)
{
    return convert(amount, g_display, dataCode);
}

QString symbol()
{
    return QString::fromUtf8(find(g_display)->symbol).trimmed();
}

double sliderStep()
{
    const double ratio = perPound(g_display) / perPound(QStringLiteral("EUR"));
    double factor = 1.0;
    for (const double f : {1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0})
        if (std::abs(std::log(ratio / f)) < std::abs(std::log(ratio / factor)))
            factor = f;
    return 500'000.0 * factor;
}

QString ofText(const QString &moneyText, const QString &fallback)
{
    if (moneyText.contains(QChar(0x20AC)))
        return QStringLiteral("EUR");
    if (moneyText.contains(QChar(0x00A3)))
        return QStringLiteral("GBP");
    if (moneyText.contains(QLatin1String("CHF")))
        return QStringLiteral("CHF");
    if (moneyText.contains(QChar(0xFFE5)) || moneyText.contains(QChar(0x00A5)))
        return QStringLiteral("CNY");
    if (moneyText.contains(QString::fromUtf8("\xd1\x80\xd1\x83\xd0\xb1")))
        return QStringLiteral("RUB");
    if (moneyText.contains(QLatin1Char('$')))
        return QStringLiteral("USD");
    return fallback;
}

QString format(double amount)
{
    const double a = std::abs(amount);
    // Unit by the rounded figure, so 999,999.9 reads "1M", not "1000K".
    const auto rounded = [](double v) {
        const double scale = v >= 10.0 ? 1.0 : 10.0;
        return std::round(v * scale) / scale;
    };
    QString number;
    if (rounded(a / 1e3) >= 1000.0)
        number = trimmed(rounded(a / 1e6), 1) + QStringLiteral("M");
    else if (std::round(a) >= 1000.0)
        number = trimmed(rounded(a / 1e3), 1) + QStringLiteral("K");
    else
        number = QString::number(qRound(a));
    return (amount < 0 ? QStringLiteral("-") : QString()) + withSymbol(number);
}

QString formatMillions(double amount, int decimals)
{
    return withSymbol(QLocale().toString(amount / 1e6, 'f', decimals) + QStringLiteral("M"));
}

QString displayText(const QString &moneyText)
{
    const QString source = ofText(moneyText, QString());
    if (source.isEmpty() || source == g_display)
        return moneyText;

    // Each amount with its symbol and K/M unit; whatever surrounds it
    // ("p/w", " - ", "Not for Sale") stays as it is.
    static const QRegularExpression amount(QString::fromUtf8(
        "(?:\xe2\x82\xac|\xc2\xa3|\\$|CHF\\s?|\xef\xbf\xa5|\xc2\xa5)?"
        "(\\d[\\d,]*(?:\\.\\d+)?)(?:\\s?([KM])\\b)?(?:\\s?\xd1\x80\xd1\x83\xd0\xb1)?"));
    QString out;
    qsizetype last = 0;
    auto it = amount.globalMatch(moneyText);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        QString digits = m.captured(1);
        digits.remove(QLatin1Char(','));
        bool ok = false;
        double value = digits.toDouble(&ok);
        if (!ok)
            continue;
        const QString unit = m.captured(2);
        if (unit == QLatin1String("M"))
            value *= 1e6;
        else if (unit == QLatin1String("K"))
            value *= 1e3;
        out += moneyText.mid(last, m.capturedStart() - last);
        out += format(convert(value, source, g_display));
        last = m.capturedEnd();
    }
    out += moneyText.mid(last);
    return out;
}

} // namespace Currency
} // namespace fm
