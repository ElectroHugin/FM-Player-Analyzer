#pragma once

#include <QString>
#include <QStringList>

namespace fm {

// Display currency. The analyzer keeps money as it was imported (an FM27 save
// is read as euros, an export in whatever currency the game ran in) and only
// converts for display. The rates are the game's own: FM keeps one fixed rate
// per currency (units per pound) in its database, the same in every save.
namespace Currency {

// Offered display currencies, in menu order: GBP, USD, EUR, CHF, CNY, RUB.
QStringList codes();

// Currency that suits a UI language ("de" -> EUR, "zh_CN" -> CNY, ...).
QString defaultFor(const QString &uiLanguage);

// Name of a currency for menus ("Euro (€)"), in the UI language.
QString label(const QString &code);

// The display currency, app-wide. Unknown codes are ignored.
void setDisplay(const QString &code);
QString display();

// Units of the currency per pound, as the game has them.
double perPound(const QString &code);

double convert(double amount, const QString &fromCode, const QString &toCode);

// Between the currency of the data (FM27 saves are read as euros) and the
// display currency.
double toDisplay(double amount, const QString &dataCode = QStringLiteral("EUR"));
double fromDisplay(double amount, const QString &dataCode = QStringLiteral("EUR"));

// Symbol of the display currency, trimmed ("€", "CHF", "руб").
QString symbol();

// Step of a money slider in the display currency: 0.5M, scaled to a round
// factor for currencies with much smaller units (yuan: 5M, ruble: 50M).
double sliderStep();

// Currency of a money text from its symbol ("€1.2M" -> EUR, "£5K p/w" -> GBP);
// fallback when the text has none.
QString ofText(const QString &moneyText, const QString &fallback = QStringLiteral("EUR"));

// An amount in the display currency, written the way the game writes money
// ("€950", "€9.5K", "€1.4M", "CHF 2.2M", "1.4M руб").
QString format(double amount);

// Same, with a fixed number of decimals in millions ("€10.5M"), for totals
// and slider labels.
QString formatMillions(double amount, int decimals);

// A money text as imported ("€500K - €800K", "€5,000 p/w", "Not for Sale")
// in the display currency. Texts already in it, and texts without an
// amount, come back unchanged.
QString displayText(const QString &moneyText);

} // namespace Currency
} // namespace fm
