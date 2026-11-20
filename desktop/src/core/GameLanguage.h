#pragma once

#include <QString>
#include <QStringList>

namespace fm {

struct ImportTable;

// The language the game runs in, which is independent of the app's own UI
// language: the FM View Exporter writes column headers and many values
// ("Rechts", "Sehr stark", "V (Z)", "€2.7Mio.") in the game language. The
// import turns such exports back into the English texts the app stores, so
// an export from a German, French, Italian, Chinese or Russian game imports
// like an English one. The save game import does not need this: a save holds
// no language-dependent texts.
namespace GameLanguage {

// Setting value: tell the language from the export's column headers.
inline constexpr char kAuto[] = "auto";

// Game languages the import can read, English first.
const QStringList &codes();

// Native name for a language picker ("Deutsch", "Français", ...).
QString nativeName(const QString &code);

// Language whose export headers the file uses. Empty when the headers do not
// tell (too few known columns, or a tie).
QString detect(const QStringList &headers, const QString &fmVersionId);

// The language to read a file in: the detected one when the headers tell,
// otherwise the configured one ("auto" or empty means English).
QString resolve(const QString &configured, const QString &detected);

// Turns headers and values of a table written in `language` into English, in
// place. English, an unknown language or an FM version without tables leave
// the table unchanged.
void toEnglish(ImportTable *table, const QString &language, const QString &fmVersionId);

// Single conversions, exposed for tests.
QString positionsToEnglish(const QString &text, const QString &language);
QString moneyToEnglish(const QString &text, const QString &language);
QString heightToEnglish(const QString &text);

} // namespace GameLanguage

} // namespace fm
