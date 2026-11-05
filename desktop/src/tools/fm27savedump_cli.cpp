// Dumps the players an FM27 save holds as CSV, for checking the save reader
// against reference data. Developer tool: the output contains hidden values
// and is never shown in the app.
//
//   fm27savedump SAVE.fm OUT.csv                 every player, raw (hidden values too)
//   fm27savedump SAVE.fm OUT.csv --table [--unmasked]
//                                                the import table the app would build

#include "core/Fm27Save.h"
#include "core/Fm27SaveImport.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#include <cstdio>

using namespace fm::fm27save;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3) {
        std::fprintf(stderr, "usage: fm27savedump SAVE.fm OUT.csv [--table [--unmasked]]\n");
        return 2;
    }
    if (args.contains(QStringLiteral("--table"))) {
        fm::Fm27SaveImport::Options options;
        options.attributeMasking = !args.contains(QStringLiteral("--unmasked"));
        fm::ImportTable table;
        fm::ImportError importError;
        if (!fm::Fm27SaveImport::buildTable(args[1], options, &table, &importError)) {
            std::fprintf(stderr, "error: %s\n", qPrintable(importError.message));
            return 1;
        }
        QFile file(args[2]);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 1;
        QTextStream out(&file);
        const auto quoted = [](QString s) { return QLatin1Char('"') + s.replace(QLatin1Char('"'), QLatin1String("\"\"")) + QLatin1Char('"'); };
        QStringList header;
        for (const QString &h : table.headers)
            header << quoted(h);
        out << header.join(QLatin1Char(',')) << "\n";
        for (const QStringList &row : table.rows) {
            QStringList cells;
            for (const QString &c : row)
                cells << quoted(c);
            out << cells.join(QLatin1Char(',')) << "\n";
        }
        std::fprintf(stderr, "%lld rows\n", qlonglong(table.rows.size()));
        return 0;
    }
    SaveData data;
    QString error;
    if (!readSave(args[1], &data, &error)) {
        std::fprintf(stderr, "error: %s\n", qPrintable(error));
        return 1;
    }
    QFile file(args[2]);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "cannot write %s\n", qPrintable(args[2]));
        return 1;
    }
    QTextStream out(&file);
    out << "uid,unique_id,pindex,offset,name,birth,age,nation,height,lf,rf,club_uid,club_short,club_unique,"
           "world_rep,knowledge,value,wage,status";
    for (int i = 0; i < SavePositionCount; ++i)
        out << ",pos" << i;
    for (int i = 0; i < SaveAttrCount; ++i)
        out << ",a" << i;
    for (int i = 0; i < SavePersonalityCount; ++i)
        out << ",p" << i;
    out << "\n";
    for (const SavePlayer &p : data.players) {
        QString name = p.name;
        name.replace(QLatin1Char('"'), QLatin1String("\"\""));
        out << p.uid << ',' << p.uniqueId << ',' << p.pindex << ',' << p.recordOffset << ",\"" << name << "\","
            << (p.birthDate.isValid() ? p.birthDate.toString(Qt::ISODate) : QString()) << ','
            << (p.age >= 0 ? QString::number(p.age) : QString()) << ',' << p.nationId << ',' << p.heightCm << ','
            << p.leftFoot << ',' << p.rightFoot << ',' << p.clubUid << ",\"" << p.clubShortName << "\","
            << p.clubUniqueId << (p.women ? "w" : "") << ',' << p.worldReputation << ','
            << data.knowledgeByPindex.value(p.pindex, -1) << ',' << p.transferValue << ','
            << p.weeklyWage << ',' << p.squadStatus;
        for (int i = 0; i < SavePositionCount; ++i)
            out << ',' << p.positions[i];
        for (int i = 0; i < SaveAttrCount; ++i)
            out << ',' << p.attributes[i];
        for (int i = 0; i < SavePersonalityCount; ++i)
            out << ',' << p.personality[i];
        out << "\n";
    }
    std::fprintf(stderr, "club %s, build %s, date %s, %lld players, %lld with knowledge\n", qPrintable(data.clubName), qPrintable(data.build),
                 qPrintable(data.gameDate.toString(Qt::ISODate)), qlonglong(data.players.size()),
                 qlonglong(data.knowledgeByPindex.size()));
    return 0;
}
