#include "GameLanguage.h"

#include "GameLanguageData.h"
#include "ImportTable.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace fm {

namespace GameLanguage {

namespace {

bool hasTables(const QString &fmVersionId)
{
    // Empty = the default version, which is FM27 for exports with these
    // headers; FM24 exports are English only.
    return fmVersionId.isEmpty() || fmVersionId == QLatin1String("fm27");
}

const GameLanguageTable *tableFor(const QString &language)
{
    const auto &tables = fm27GameLanguageTables();
    const auto it = tables.constFind(language);
    return it == tables.constEnd() ? nullptr : &it.value();
}

const QSet<QString> &englishHeaders()
{
    static const QSet<QString> headers = [] {
        QSet<QString> all;
        for (const GameLanguageTable &table : fm27GameLanguageTables()) {
            for (const QString &english : table.headers)
                all.insert(english);
        }
        return all;
    }();
    return headers;
}

QString valueToEnglish(const GameLanguageTable &table, const QString &category, const QString &text)
{
    const QString trimmed = text.trimmed();
    const auto it = table.values.constFind(category + QLatin1Char('|') + trimmed);
    return it == table.values.constEnd() ? trimmed : it.value();
}

// "Medienfreundlich, Verschlossen": each part on its own.
QString listToEnglish(const GameLanguageTable &table, const QString &category, const QString &text)
{
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() < 2)
        return valueToEnglish(table, category, text);
    QStringList out;
    out.reserve(parts.size());
    for (const QString &part : parts)
        out << valueToEnglish(table, category, part);
    return out.join(QStringLiteral(", "));
}

// One amount with its unit as the game writes it in the given language:
// "2.7Mio." (de), "15m" (it: thousands), "1.5万" (zh: ten thousands),
// "190К" (ru, Cyrillic). Returns NaN when the text is not an amount.
double amountValue(QString text, const QString &language)
{
    text = text.trimmed();
    static const QRegularExpression amount(
        QStringLiteral("^([0-9]+(?:[.,][0-9]+)?)\\s*(.*)$"));
    const QRegularExpressionMatch match = amount.match(text);
    if (!match.hasMatch())
        return std::nan("");
    QString number = match.captured(1);
    number.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double value = number.toDouble(&ok);
    if (!ok)
        return std::nan("");
    const QString unit = match.captured(2).trimmed();
    if (unit.isEmpty())
        return value;
    // Italian and Spanish write thousands as "m" (mila, mil).
    const bool italian = language == QLatin1String("it") || language == QLatin1String("es");
    if (unit == QLatin1String("K") || unit == QString::fromUtf8("К")
        || (italian && unit == QLatin1String("m")))
        return value * 1e3;
    if (unit == QLatin1String("M") || unit == QString::fromUtf8("М")
        || unit == QLatin1String("Mio.") || unit == QLatin1String("Mio"))
        return value * 1e6;
    if (unit == QLatin1String("Mrd.") || unit == QLatin1String("bn")
        || unit == QLatin1String("B") || unit == QString::fromUtf8("Млрд"))
        return value * 1e9;
    if (unit == QString::fromUtf8("万"))
        return value * 1e4;
    if (unit == QString::fromUtf8("亿"))
        return value * 1e8;
    return std::nan("");
}

// English short form as the game writes it: "€22.5K", "€1.9M", "€950".
QString englishAmount(double value)
{
    const auto shortNumber = [](double v) {
        QString s = QString::number(v, 'f', 2);
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
        return s;
    };
    if (std::abs(value) >= 1e6)
        return shortNumber(value / 1e6) + QLatin1Char('M');
    if (std::abs(value) >= 1e3)
        return shortNumber(value / 1e3) + QLatin1Char('K');
    return shortNumber(value);
}

// Wage period suffixes. Weekly ones are taken from exports of every game
// language; monthly and yearly ones follow the same pattern and are not yet
// checked against the game.
QString periodSuffix(const QString &text, QString *rest)
{
    struct Suffix {
        const char *text;
        const char *english;
    };
    static const Suffix suffixes[] = {
        {"p/w", "p/w"}, {"p/m", "p/m"}, {"p/a", "p/a"}, {"p/y", "p/a"},
        {"/W.", "p/w"}, {"p/W", "p/w"}, {"/M.", "p/m"}, {"/J.", "p/a"},
        {"p/s", "p/w"}, {"p/ sem.", "p/w"}, {"por semana", "p/w"}, {"por mes", "p/m"},
        {"por año", "p/a"},
        {"/周", "p/w"}, {"周薪", "p/w"}, {"每周", "p/w"},
        {"/月", "p/m"}, {"月薪", "p/m"}, {"/年", "p/a"}, {"年薪", "p/a"},
        {"/нед", "p/w"}, {"в/н", "p/w"}, {"в нед.", "p/w"},
        {"/мес", "p/m"}, {"в/м", "p/m"}, {"/год", "p/a"}, {"в/г", "p/a"},
    };
    for (const Suffix &suffix : suffixes) {
        const QString s = QString::fromUtf8(suffix.text);
        if (text.endsWith(s)) {
            *rest = text.left(text.size() - s.size()).trimmed();
            return QString::fromLatin1(suffix.english);
        }
    }
    *rest = text;
    return QString();
}

} // namespace

const QStringList &codes()
{
    static const QStringList all = {QStringLiteral("en"), QStringLiteral("de"), QStringLiteral("fr"),
                                    QStringLiteral("it"), QStringLiteral("zh_CN"), QStringLiteral("ru"),
                                    QStringLiteral("es")};
    return all;
}

QString nativeName(const QString &code)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("de"), QStringLiteral("Deutsch")},
        {QStringLiteral("fr"), QString::fromUtf8("Français")},
        {QStringLiteral("it"), QStringLiteral("Italiano")},
        {QStringLiteral("zh_CN"), QString::fromUtf8("简体中文")},
        {QStringLiteral("ru"), QString::fromUtf8("Русский")},
        {QStringLiteral("es"), QString::fromUtf8("Español")},
    };
    return names.value(code, code);
}

QString detect(const QStringList &headers, const QString &fmVersionId)
{
    if (!hasTables(fmVersionId))
        return QString();
    QHash<QString, int> scores;
    for (const QString &raw : headers) {
        const QString header = raw.trimmed();
        if (englishHeaders().contains(header))
            ++scores[QStringLiteral("en")];
        const auto &tables = fm27GameLanguageTables();
        for (auto it = tables.constBegin(); it != tables.constEnd(); ++it) {
            if (it.value().headers.contains(header))
                ++scores[it.key()];
        }
    }
    QString best;
    int bestScore = 0, secondScore = 0;
    for (auto it = scores.constBegin(); it != scores.constEnd(); ++it) {
        if (it.value() > bestScore) {
            secondScore = bestScore;
            bestScore = it.value();
            best = it.key();
        } else if (it.value() > secondScore) {
            secondScore = it.value();
        }
    }
    // Headers that read the same in several languages ("Unique ID",
    // "Position") do not decide; a few distinctive ones do.
    if (bestScore < 3 || bestScore == secondScore)
        return QString();
    return best;
}

QString resolve(const QString &configured, const QString &detected)
{
    if (!detected.isEmpty())
        return detected;
    if (configured.isEmpty() || configured == QLatin1String(kAuto))
        return QStringLiteral("en");
    return configured;
}

QString positionsToEnglish(const QString &text, const QString &language)
{
    const GameLanguageTable *table = tableFor(language);
    if (!table || text.trimmed().isEmpty())
        return text;
    // "V/FV (L), DM, M (Z)": groups of codes, each with optional side letters.
    static const QRegularExpression group(QStringLiteral("^([^()]+?)\\s*(?:\\(([^()]*)\\))?$"));
    QStringList out;
    for (const QString &rawPart : text.split(QLatin1Char(','))) {
        const QString part = rawPart.trimmed();
        if (part.isEmpty())
            continue;
        const QRegularExpressionMatch match = group.match(part);
        if (!match.hasMatch())
            return text;
        QStringList codes;
        for (const QString &code : match.captured(1).split(QLatin1Char('/')))
            codes << table->positions.value(code.trimmed(), code.trimmed());
        QString converted = codes.join(QLatin1Char('/'));
        if (match.hasCaptured(2)) {
            QString sides;
            for (const QChar side : match.captured(2).trimmed())
                sides += table->sides.value(QString(side), QString(side));
            converted += QStringLiteral(" (") + sides + QLatin1Char(')');
        }
        out << converted;
    }
    return out.join(QStringLiteral(", "));
}

QString moneyToEnglish(const QString &text, const QString &language)
{
    QString s = text.trimmed();
    if (s.isEmpty() || s == QLatin1String("-"))
        return text;
    // Italian wages come in brackets: "(€15m - €22.5m p/s)".
    if (s.startsWith(QLatin1Char('(')) && s.endsWith(QLatin1Char(')')))
        s = s.mid(1, s.size() - 2).trimmed();
    QString rest;
    const QString period = periodSuffix(s, &rest);
    s = rest;

    static const QRegularExpression rangeSeparator(QString::fromUtf8("\\s+[-–]\\s+"));
    QStringList amounts;
    for (QString part : s.split(rangeSeparator)) {
        part = part.trimmed();
        QString currency;
        if (!part.isEmpty() && (part.at(0) == QChar(0x20AC) || part.at(0) == QChar(0x00A3)
                                || part.at(0) == QLatin1Char('$'))) {
            currency = part.left(1);
            part = part.mid(1);
        }
        const double value = amountValue(part, language);
        if (std::isnan(value))
            return text; // not a figure the import knows: keep it as it is
        amounts << currency + englishAmount(value);
    }
    QString out = amounts.join(QStringLiteral(" - "));
    if (!period.isEmpty())
        out += QLatin1Char(' ') + period;
    return out;
}

QString heightToEnglish(const QString &text)
{
    // "185 cm", "185厘米", "185 см"
    static const QRegularExpression height(QStringLiteral("^\\s*([0-9]+)\\s*\\D*$"));
    const QRegularExpressionMatch match = height.match(text);
    return match.hasMatch() ? match.captured(1) + QStringLiteral(" cm") : text;
}

void toEnglish(ImportTable *table, const QString &language, const QString &fmVersionId)
{
    if (!table || !hasTables(fmVersionId))
        return;
    const GameLanguageTable *data = tableFor(language);
    if (!data)
        return;

    for (QString &header : table->headers) {
        const auto it = data->headers.constFind(header.trimmed());
        if (it != data->headers.constEnd())
            header = it.value();
    }

    enum class Kind { Positions, Money, Height, Decimal, Country, Value, List };
    struct Column {
        int index;
        Kind kind;
        QString category;
    };
    QList<Column> columns;
    for (int c = 0; c < table->headers.size(); ++c) {
        const QString &h = table->headers.at(c);
        if (h == QLatin1String("Position") || h == QLatin1String("Best Pos"))
            columns.append({c, Kind::Positions, {}});
        else if (h == QLatin1String("Wage") || h == QLatin1String("Transfer Value"))
            columns.append({c, Kind::Money, {}});
        else if (h == QLatin1String("Height"))
            columns.append({c, Kind::Height, {}});
        else if (h == QLatin1String("Rating"))
            columns.append({c, Kind::Decimal, {}});
        else if (h == QLatin1String("Nation") || h == QLatin1String("2nd Nat"))
            columns.append({c, Kind::Country, {}});
        else if (h == QLatin1String("Preferred Foot"))
            columns.append({c, Kind::Value, QStringLiteral("preferred_foot")});
        else if (h == QLatin1String("Left Foot") || h == QLatin1String("Right Foot"))
            columns.append({c, Kind::Value, QStringLiteral("foot_strength")});
        else if (h == QLatin1String("Personality"))
            columns.append({c, Kind::Value, QStringLiteral("personality")});
        else if (h == QLatin1String("Playing Time"))
            columns.append({c, Kind::Value, QStringLiteral("playing_time")});
        else if (h == QLatin1String("Media Handling"))
            columns.append({c, Kind::List, QStringLiteral("media_handling")});
        else if (h == QLatin1String("Inf"))
            columns.append({c, Kind::List, QStringLiteral("information")});
    }

    for (QStringList &row : table->rows) {
        for (const Column &column : std::as_const(columns)) {
            if (column.index >= row.size())
                continue;
            QString &cell = row[column.index];
            switch (column.kind) {
            case Kind::Positions:
                cell = positionsToEnglish(cell, language);
                break;
            case Kind::Money:
                cell = moneyToEnglish(cell, language);
                break;
            case Kind::Height:
                cell = heightToEnglish(cell);
                break;
            case Kind::Decimal:
                cell.replace(QLatin1Char(','), QLatin1Char('.'));
                break;
            case Kind::Country:
                cell = data->countries.value(cell.trimmed(), cell);
                break;
            case Kind::Value:
                cell = valueToEnglish(*data, column.category, cell);
                break;
            case Kind::List:
                cell = listToEnglish(*data, column.category, cell);
                break;
            }
        }
    }
}

} // namespace GameLanguage

} // namespace fm
