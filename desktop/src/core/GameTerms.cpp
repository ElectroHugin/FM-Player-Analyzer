#include "GameTerms.h"

#include "Definitions.h"
#include "GameTermsData.h"

#include <QRegularExpression>
#include <QStringList>

namespace fm::GameTerms {

namespace {

const QHash<QString, QString> *table()
{
    const QString language = Definitions::displayLanguage();
    if (language == QLatin1String("en"))
        return nullptr;
    const auto &all = fm27GameTerms();
    const auto it = all.constFind(language);
    return it == all.constEnd() ? nullptr : &it.value();
}

QString lookup(const QHash<QString, QString> &terms, const QString &category,
               const QString &english)
{
    return terms.value(category + QLatin1Char('|') + english);
}

} // namespace

QString text(const QString &category, const QString &english)
{
    const auto *terms = table();
    if (!terms || english.isEmpty())
        return english;
    const QString local = lookup(*terms, category, english.trimmed());
    return local.isEmpty() ? english : local;
}

QString attribute(const QString &name)
{
    const auto *terms = table();
    if (!terms)
        return name;
    // The app names two attributes differently from the game.
    static const QHash<QString, QString> gameNames = {
        {QStringLiteral("One vs One"), QStringLiteral("One On Ones")},
        {QStringLiteral("Teamwork"), QStringLiteral("Team Work")},
    };
    QString local = lookup(*terms, QStringLiteral("attribute"), gameNames.value(name, name));
    if (local.isEmpty()) {
        // Same name with other capitalisation ("Off the Ball" / "Off The Ball").
        const QString key = QStringLiteral("attribute|");
        for (auto it = terms->cbegin(); it != terms->cend(); ++it) {
            if (it.key().startsWith(key)
                && it.key().mid(key.size()).compare(name, Qt::CaseInsensitive) == 0) {
                local = it.value();
                break;
            }
        }
    }
    return local.isEmpty() ? name : local;
}

QString positions(const QString &positionRaw)
{
    const auto *terms = table();
    if (!terms || positionRaw.isEmpty())
        return positionRaw;
    static const QRegularExpression part(QStringLiteral("^([A-Z/]+)(?:\\s*\\(([LRC]+)\\))?$"));
    QStringList out;
    for (const QString &piece : positionRaw.split(QLatin1Char(','))) {
        const QString trimmed = piece.trimmed();
        const auto match = part.match(trimmed);
        if (!match.hasMatch()) {
            out << trimmed;
            continue;
        }
        QStringList codes;
        for (const QString &code : match.captured(1).split(QLatin1Char('/'))) {
            const QString local = lookup(*terms, QStringLiteral("position_code"), code);
            codes << (local.isEmpty() ? code : local);
        }
        QString result = codes.join(QLatin1Char('/'));
        if (match.hasCaptured(2) && !match.captured(2).isEmpty()) {
            QString sides;
            for (const QChar side : match.captured(2)) {
                const QString local = lookup(*terms, QStringLiteral("side_code"), QString(side));
                sides += local.isEmpty() ? QString(side) : local;
            }
            result += QStringLiteral(" (%1)").arg(sides);
        }
        out << result;
    }
    return out.join(QStringLiteral(", "));
}

} // namespace fm::GameTerms
