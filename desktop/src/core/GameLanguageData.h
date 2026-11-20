#pragma once

#include <QHash>
#include <QString>

namespace fm {

// Game texts of one FM27 game language, each mapped to the English text the
// app works with. Generated from the game itself (see
// scripts/make_game_languages_fm27.py and GameLanguageData.cpp).
struct GameLanguageTable {
    QHash<QString, QString> headers;   // export column header -> English header
    QHash<QString, QString> values;    // "<category>|<text>" -> English value
    QHash<QString, QString> positions; // position code ("TW") -> English ("GK")
    QHash<QString, QString> sides;     // side letter ("Z") -> English ("C")
    QHash<QString, QString> countries; // country name or code -> English name or code
};

// Language code ("de", "fr", "it", "zh_CN", "ru") -> table. English needs none.
const QHash<QString, GameLanguageTable> &fm27GameLanguageTables();

} // namespace fm
