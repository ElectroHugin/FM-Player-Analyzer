// Dumps the players an FM27 save holds as CSV, for checking the save reader
// against reference data. Developer tool: the output contains hidden values
// and is never shown in the app.
//
//   fm27savedump SAVE.fm OUT.csv

#include "core/Fm27Save.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#include <cstdio>

using namespace fm::fm27save;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() != 3) {
        std::fprintf(stderr, "usage: fm27savedump SAVE.fm OUT.csv\n");
        return 2;
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
           "world_rep,knowledge";
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
            << p.clubUniqueId << ',' << p.worldReputation << ','
            << data.knowledgeByPindex.value(p.pindex, -1);
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
