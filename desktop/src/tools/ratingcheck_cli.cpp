// Development check of a rating profile against a real FM27 table.
//
//   fmratingcheck DEFINITIONS_FM27.json HARVESTGREEN.json TABLE.csv [--role-share X] [--gk-role-share X] [--club NAME] [--player NAME]...
//
// TABLE.csv is the output of `fm27savedump SAVE OUT --table --unmasked`. For
// every listed player (or the whole club) the best role and, for single
// players, all roles of their positions are printed with the built-in
// Harvestgreen profile. Read-only: nothing is written anywhere.
#include "core/AppConfig.h"
#include "core/BuiltinRatingProfiles.h"
#include "core/Definitions.h"
#include "core/RatingSnapshot.h"
#include "core/RoleAssignment.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <cstdio>

namespace {
QStringList splitCsv(const QString &line)
{
    QStringList out;
    QString field;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (quoted) {
            if (c == u'"' && i + 1 < line.size() && line[i + 1] == u'"') { field += u'"'; ++i; }
            else if (c == u'"') quoted = false;
            else field += c;
        } else if (c == u'"') quoted = true;
        else if (c == u',') { out << field; field.clear(); }
        else field += c;
    }
    out << field;
    return out;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 4) {
        std::fprintf(stderr, "usage: fmratingcheck DEFINITIONS_FM27.json HARVESTGREEN.json TABLE.csv "
                             "[--role-share X] [--gk-role-share X] [--club NAME] [--player NAME]...\n");
        return 2;
    }
    fm::Definitions definitions;
    if (!definitions.load(args[1], QStringLiteral("fm27"))) {
        std::fprintf(stderr, "definitions: %s\n", qPrintable(definitions.errorString()));
        return 1;
    }
    QFile source(args[2]);
    if (!source.open(QIODevice::ReadOnly)) return 1;
    QTemporaryDir temporary;
    fm::AppConfig config(temporary.filePath(QStringLiteral("defaults.ini")));
    QString error;
    auto profile = fm::harvestgreenRatingProfile({definitions}, config,
                                                 QJsonDocument::fromJson(source.readAll()).object(), &error);
    if (!profile) { std::fprintf(stderr, "profile: %s\n", qPrintable(error)); return 1; }
    QStringList players, clubs;
    for (int i = 4; i < args.size(); ++i) {
        if (args[i] == QStringLiteral("--role-share") && i + 1 < args.size()) profile->roleShare = args[++i].toDouble();
        else if (args[i] == QStringLiteral("--gk-role-share") && i + 1 < args.size()) profile->goalkeeperRoleShare = args[++i].toDouble();
        else if (args[i] == QStringLiteral("--club") && i + 1 < args.size()) clubs << args[++i];
        else if (args[i] == QStringLiteral("--player") && i + 1 < args.size()) players << args[++i];
    }
    const auto snapshot = fm::RatingSnapshot::compile(*profile, definitions, &error);
    if (!snapshot) { std::fprintf(stderr, "compile: %s\n", qPrintable(error)); return 1; }

    QFile table(args[3]);
    if (!table.open(QIODevice::ReadOnly | QIODevice::Text)) return 1;
    QTextStream in(&table);
    const QStringList header = splitCsv(in.readLine());
    const QHash<QString, QString> renamed{{QStringLiteral("Off The Ball"), QStringLiteral("Off the Ball")},
                                          {QStringLiteral("Team Work"), QStringLiteral("Teamwork")},
                                          {QStringLiteral("Command Of Area"), QStringLiteral("Command of Area")},
                                          {QStringLiteral("One On Ones"), QStringLiteral("One vs One")}};
    QHash<int, int> columnToAttr;
    for (int c = 0; c < header.size(); ++c)
        if (const int a = fm::attrIndexByName(renamed.value(header[c], header[c])); a >= 0) columnToAttr.insert(c, a);
    const int nameCol = header.indexOf(QStringLiteral("Player"));
    const int clubCol = header.indexOf(QStringLiteral("Club"));
    const int posCol = header.indexOf(QStringLiteral("Position"));
    fm::RoleAssignment::DefaultRoles defaults(definitions);
    while (!in.atEnd()) {
        const QStringList row = splitCsv(in.readLine());
        if (row.size() != header.size()) continue;
        if (!players.contains(row[nameCol]) && !clubs.contains(row[clubCol])) continue;
        if (!clubs.isEmpty() && !clubs.contains(row[clubCol]) && !players.contains(row[nameCol])) continue;
        fm::Player player;
        for (auto it = columnToAttr.begin(); it != columnToAttr.end(); ++it) {
            const int value = row[it.key()].toInt();
            player.attrLo[it.value()] = player.attrHi[it.value()] = static_cast<uint8_t>(value);
        }
        QList<QPair<double, QString>> scores;
        for (const QString &role : defaults.forPosition(row[posCol])) {
            const auto score = snapshot->evaluate(player, role);
            if (score.normalized) scores.append({*score.normalized, role});
        }
        std::sort(scores.begin(), scores.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        if (scores.isEmpty()) continue;
        QString line = QStringLiteral("%1 (%2): ").arg(row[nameCol], row[clubCol]);
        const int shown = players.contains(row[nameCol]) ? int(scores.size()) : 1;
        for (int i = 0; i < shown; ++i)
            line += QStringLiteral("%1 %2  ").arg(scores[i].second).arg(scores[i].first, 0, 'f', 1);
        std::printf("%s\n", qPrintable(line));
    }
    return 0;
}
