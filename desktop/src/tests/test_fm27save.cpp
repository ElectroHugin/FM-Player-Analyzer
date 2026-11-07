// FM27 save games: the visibility rules of Fm27SaveImport and the small
// parsers of Fm27Save. A real save is optional: set FM27_TEST_SAVE to a copy
// of one to also run the end-to-end import (the file is only read).

#include "core/Database.h"
#include "core/Fm27Save.h"
#include "core/Fm27SaveFamily.h"
#include "core/Fm27SaveImport.h"
#include "core/HtmlImporter.h"
#include "core/Utils.h"

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <cstring>

using namespace fm;
using namespace fm::fm27save;

namespace {

SavePlayer makePlayer(quint32 uniqueId, quint32 pindex, const QString &name, quint32 clubUid,
                      const QString &club, int value)
{
    SavePlayer p;
    p.uid = uniqueId - 1;
    p.uniqueId = uniqueId;
    p.pindex = pindex;
    p.personFound = true;
    p.name = name;
    p.age = 24;
    p.clubUid = clubUid;
    p.fieldingClubUid = clubUid;
    p.clubShortName = club;
    p.positions.fill(1);
    p.positions[PosMC] = 20;
    p.attributes.fill(uint8_t(value));
    p.leftFoot = 12;
    p.rightFoot = 20;
    p.heightCm = 180;
    p.nationId = 145;
    return p;
}

QByteArray fogSection(const QList<QPair<quint32, int>> &entries)
{
    QByteArray b("\x03\x01" "tad.", 6);
    b.append(QByteArray(11, '\0'));
    const quint32 count = quint32(entries.size());
    b.append(reinterpret_cast<const char *>(&count), 4);
    for (const auto &[pindex, knowledge] : entries) {
        b.append(char(knowledge));
        const quint32 reference = pindex + 1;
        b.append(reinterpret_cast<const char *>(&reference), 4);
    }
    return b;
}

int column(const ImportTable &table, const QString &name)
{
    return int(table.headers.indexOf(name));
}

} // namespace

class TestFm27Save : public QObject {
    Q_OBJECT

private slots:
    void saveFamily()
    {
        using namespace fm::Fm27SaveFamily;
        QCOMPARE(baseName(QStringLiteral("/g/Career (v02).fm")), QStringLiteral("Career"));
        QCOMPARE(baseName(QStringLiteral("/g/A - B -  Versuch D.fm")),
                 QStringLiteral("A - B -  Versuch D"));
        QVERIFY(baseName(QStringLiteral("/g/config.xml")).isEmpty());
        QVERIFY(belongsTo(QStringLiteral("Career (v13).fm"), QStringLiteral("Career")));
        QVERIFY(!belongsTo(QStringLiteral("Career 2.fm"), QStringLiteral("Career")));
        QVERIFY(!belongsTo(QStringLiteral("last save overwrite backup.fm"),
                           QStringLiteral("Career")));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto write = [&dir](const QString &name, const QDateTime &time) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
            // Flush first: a write still buffered at close would stamp the
            // file with the current time again and undo the order below.
            QVERIFY(file.flush());
            QVERIFY(file.setFileTime(time, QFileDevice::FileModificationTime));
        };
        const QDateTime base = QDateTime::currentDateTime().addDays(-1);
        write(QStringLiteral("Career.fm"), base);
        write(QStringLiteral("Career (v02).fm"), base.addSecs(60));
        write(QStringLiteral("Career (v03).fm"), base.addSecs(-60));
        write(QStringLiteral("Other.fm"), base.addSecs(600));
        const Member newestMember = newest(dir.filePath(QStringLiteral("Career.fm")));
        QCOMPARE(QFileInfo(newestMember.path).fileName(), QStringLiteral("Career (v02).fm"));
        QVERIFY(newest(dir.filePath(QStringLiteral("Missing.fm"))).path.isEmpty());
    }

    void fogOfWar()
    {
        QHash<quint32, int> knowledge;
        QVERIFY(parseFogOfWar(fogSection({{17867, 97}, {68506, 76}}), &knowledge));
        QCOMPARE(knowledge.size(), 2);
        QCOMPARE(knowledge.value(17867), 97);
        QCOMPARE(knowledge.value(68506), 76);
        // The count must fill the section exactly.
        QByteArray truncated = fogSection({{1, 50}});
        truncated.chop(1);
        QVERIFY(!parseFogOfWar(truncated, &knowledge));
        QVERIFY(!parseFogOfWar(QByteArray("nonsense"), &knowledge));
        // A damaged entry drops the whole block: no half-filled table.
        knowledge.clear();
        QByteArray damaged = fogSection({{17867, 97}, {68506, 120}});
        QVERIFY(!parseFogOfWar(damaged, &knowledge));
        QVERIFY(knowledge.isEmpty());
        // ... and a failed read does not leave an earlier table behind.
        QVERIFY(parseFogOfWar(fogSection({{17867, 97}}), &knowledge));
        QVERIFY(!parseFogOfWar(damaged, &knowledge));
        QVERIFY(knowledge.isEmpty());
    }

    void positionText_data()
    {
        QTest::addColumn<QList<int>>("good"); // indexes rated 15+
        QTest::addColumn<QString>("text");
        QTest::newRow("striker") << QList<int>{PosST} << QStringLiteral("ST (C)");
        QTest::newRow("full back") << QList<int>{PosDC, PosDR, PosWBR} << QStringLiteral("D (RC), WB (R)");
        QTest::newRow("joined") << QList<int>{PosDL, PosWBL} << QStringLiteral("D/WB (L)");
        QTest::newRow("dm and m") << QList<int>{PosDM, PosMC} << QStringLiteral("DM, M (C)");
        QTest::newRow("three joined") << QList<int>{PosDR, PosWBR, PosMR} << QStringLiteral("D/WB/M (R)");
        QTest::newRow("st stays apart") << QList<int>{PosAMC, PosST} << QStringLiteral("AM (C), ST (C)");
        QTest::newRow("wide") << QList<int>{PosML, PosAML, PosAMC, PosAMR, PosST}
                              << QStringLiteral("M (L), AM (RLC), ST (C)");
        QTest::newRow("keeper") << QList<int>{PosGK} << QStringLiteral("GK");
    }

    void positionText()
    {
        QFETCH(QList<int>, good);
        QFETCH(QString, text);
        std::array<uint8_t, 15> ratings;
        ratings.fill(10);
        for (int index : good)
            ratings[size_t(index)] = 17;
        QCOMPARE(Fm27SaveImport::positionText(ratings), text);
    }

    void feet()
    {
        QCOMPARE(Fm27SaveImport::footText(20), QStringLiteral("Very Strong"));
        QCOMPARE(Fm27SaveImport::footText(15), QStringLiteral("Strong"));
        QCOMPARE(Fm27SaveImport::footText(12), QStringLiteral("Fairly Strong"));
        QCOMPARE(Fm27SaveImport::footText(10), QStringLiteral("Reasonable"));
        QCOMPARE(Fm27SaveImport::footText(6), QStringLiteral("Weak"));
        QCOMPARE(Fm27SaveImport::footText(2), QStringLiteral("Very Weak"));
        QCOMPARE(Fm27SaveImport::nationCode(145), QStringLiteral("GER"));
        QCOMPARE(Fm27SaveImport::nationCode(99999), QString());
    }

    // The analyzer's ranges hold the true value, are stable, and do not give
    // it away by always sitting in the middle.
    void ranges()
    {
        int centred = 0, total = 0;
        QSet<int> offsets;
        for (quint32 id = 1000; id < 1400; ++id) {
            for (int attr = 0; attr < 5; ++attr) {
                for (int width = 1; width <= 5; ++width) {
                    const int value = 1 + int((id + quint32(attr)) % 20);
                    const auto [lo, hi] = Fm27SaveImport::drawRange(id, attr, value, width);
                    QVERIFY(lo >= 1 && hi <= 20 && lo <= value && value <= hi);
                    QCOMPARE(hi - lo, width);
                    QCOMPARE(Fm27SaveImport::drawRange(id, attr, value, width), std::make_pair(lo, hi));
                    if (value > 5 && value < 15) {
                        ++total;
                        offsets.insert(value - lo);
                        if (2 * value == lo + hi)
                            ++centred;
                    }
                }
            }
        }
        QVERIFY(offsets.size() == 6); // the true value appears at every place 0..5
        QVERIFY(centred < total / 3);
        QCOMPARE(Fm27SaveImport::drawRange(1, 1, 12, 0), std::make_pair(12, 12));
        QCOMPARE(Fm27SaveImport::rangedAttributeCount(100), 0);
        QCOMPARE(Fm27SaveImport::rangedAttributeCount(78), 0);
        QVERIFY(Fm27SaveImport::rangedAttributeCount(58) >= 18);
        QVERIFY(Fm27SaveImport::rangedAttributeCount(76) <= 12);
        QVERIFY(Fm27SaveImport::rangedAttributeCount(60) >= Fm27SaveImport::rangedAttributeCount(70));
    }

    void visibility()
    {
        SaveData save;
        save.clubName = QStringLiteral("Home FC");
        save.players = {
            makePlayer(10, 1, QStringLiteral("Own Player"), 500, QStringLiteral("Home FC"), 50 / 5),
            makePlayer(11, 2, QStringLiteral("Famous Player"), 600, QStringLiteral("Away FC"), 15),
            makePlayer(12, 3, QStringLiteral("Scouted Player"), 600, QStringLiteral("Away FC"), 14),
            makePlayer(13, 4, QStringLiteral("Partly Scouted"), 600, QStringLiteral("Away FC"), 12),
            makePlayer(14, 5, QStringLiteral("Barely Known"), 600, QStringLiteral("Away FC"), 11),
            makePlayer(15, 6, QStringLiteral("Unknown Player"), 600, QStringLiteral("Away FC"), 9),
            makePlayer(16, 7, QStringLiteral("Free Agent"), 0, QString(), 9),
            makePlayer(17, 8, QStringLiteral("Reserve Player"), 501, QStringLiteral("Home FC II"), 9),
        };
        save.players.append(makePlayer(18, 9, QStringLiteral("Famous Woman"), 700,
                                       QStringLiteral("Away FC Women"), 15));
        save.players[8].worldReputation = 9000;
        save.players[8].women = true; // a men's career leaves women's clubs out
        save.players[0].weeklyWage = 10000;
        save.players[0].transferValue = 2000000;
        save.players[0].squadStatus = 3;
        save.players[0].averageRating = 7.1375;
        save.players[2].weeklyWage = 50000; // a scouted player's wage shows as a range
        save.players[2].squadStatus = 1;
        save.players[1].worldReputation = 8000;
        save.players[7].fieldingClubUid = 500; // reserve side fielded by the user's club
        save.knowledgeByPindex = {{3, 95}, {4, 62}, {5, 40}};
        save.gameDate = QDate(2026, 9, 14);

        ImportTable table;
        Fm27SaveImport::buildTable(save, {true}, &table);
        // The save's date and the scouted players go to the import history.
        QCOMPARE(table.gameDate, QDate(2026, 9, 14));
        QCOMPARE(table.knownUids, (QStringList{QStringLiteral("12"), QStringLiteral("13")}));
        QStringList names;
        for (const QStringList &row : table.rows)
            names << row.at(column(table, QStringLiteral("Player")));
        QCOMPARE(names, (QStringList{QStringLiteral("Own Player"), QStringLiteral("Famous Player"),
                                     QStringLiteral("Scouted Player"), QStringLiteral("Partly Scouted"),
                                     QStringLiteral("Reserve Player")}));

        // No hidden value has a column.
        for (const char *hidden : {"Consistency", "Dirtiness", "Important Matches", "Injury Proneness",
                                   "Versatility", "Current Ability", "Potential Ability", "CA", "PA"})
            QVERIFY2(!table.headers.contains(QString::fromLatin1(hidden)), hidden);
        QCOMPARE(table.headers.size(), 13 + 47);
        QCOMPARE(table.rows.at(0).at(column(table, QStringLiteral("Wage"))), QStringLiteral("€12K p/w"));
        QCOMPARE(table.rows.at(0).at(column(table, QStringLiteral("Playing Time"))),
                 QStringLiteral("Regular Starter"));
        {
            // The valuation is a range around the true value (€2.33M).
            const QString value = table.rows.at(0).at(column(table, QStringLiteral("Transfer Value")));
            const QStringList parts = value.split(QStringLiteral(" - "));
            QCOMPARE(parts.size(), 2);
            QVERIFY(valueToFloat(parts[0]) <= 2.33e6 && valueToFloat(parts[1]) >= 2.32e6);
        }
        QCOMPARE(table.rows.at(0).at(column(table, QStringLiteral("Rating"))), QStringLiteral("7.14"));
        {
            // A scouted player's wage shows as a range around the true €58.2K.
            QString wage = table.rows.at(2).at(column(table, QStringLiteral("Wage")));
            QVERIFY(wage.endsWith(QStringLiteral(" p/w")));
            wage.chop(4);
            const QStringList parts = wage.split(QStringLiteral(" - "));
            QCOMPARE(parts.size(), 2);
            QVERIFY(valueToFloat(parts[0]) <= 58.3e3 && valueToFloat(parts[1]) >= 58.1e3);
        }
        QCOMPARE(table.rows.at(2).at(column(table, QStringLiteral("Playing Time"))),
                 QStringLiteral("Star Player"));

        const QRegularExpression range(QStringLiteral("^(\\d+)-(\\d+)$"));
        const auto countRanges = [&](const QStringList &row) {
            int ranges = 0;
            for (int c = 13; c < row.size(); ++c) {
                const auto m = range.match(row.at(c));
                if (m.hasMatch()) {
                    ++ranges;
                    const int lo = m.captured(1).toInt(), hi = m.captured(2).toInt();
                    if (lo > 12 || hi < 12)
                        return -1; // the range must hold the true value (12)
                }
            }
            return ranges;
        };
        QCOMPARE(countRanges(table.rows.at(0)), 0);
        QCOMPARE(countRanges(table.rows.at(2)), 0);
        QCOMPARE(countRanges(table.rows.at(3)), Fm27SaveImport::rangedAttributeCount(62));
        QCOMPARE(table.rows.at(0).at(column(table, QStringLiteral("Unique ID"))), QStringLiteral("10"));
        QCOMPARE(table.rows.at(0).at(column(table, QStringLiteral("Nation"))), QStringLiteral("GER"));
        QCOMPARE(table.rows.at(0).at(column(table, QStringLiteral("Height"))), QStringLiteral("180 cm"));
        QCOMPARE(table.rows.at(0).at(column(table, QStringLiteral("Position"))), QStringLiteral("M (C)"));

        // Stable: the same save gives the same ranges.
        ImportTable again;
        Fm27SaveImport::buildTable(save, {true}, &again);
        QCOMPARE(again.rows, table.rows);

        // Without masking: everyone with a club, all exact.
        ImportTable all;
        Fm27SaveImport::buildTable(save, {false}, &all);
        QCOMPARE(all.rows.size(), 7);
        QCOMPARE(all.rows.at(0).at(column(all, QStringLiteral("Transfer Value"))), QStringLiteral("€2.3M"));
        QCOMPARE(all.rows.at(2).at(column(all, QStringLiteral("Wage"))), QStringLiteral("€58K p/w"));
        for (const QStringList &row : all.rows) {
            for (int c = 13; c < row.size(); ++c)
                QVERIFY(!row.at(c).contains(QLatin1Char('-')));
        }
    }

    // Optional: a real save, end to end through the regular importer.
    void realSave()
    {
        const QString path = qEnvironmentVariable("FM27_TEST_SAVE");
        if (path.isEmpty())
            QSKIP("FM27_TEST_SAVE not set");
        QElapsedTimer timer;
        timer.start();
        SaveData save;
        QString error;
        QVERIFY2(readSave(path, &save, &error), qPrintable(error));
        qInfo() << "read" << save.players.size() << "players in" << timer.elapsed() << "ms, build" << save.build
                << "date" << save.gameDate << "club" << save.clubName;
        QVERIFY(save.players.size() > 50000);
        QVERIFY(!save.clubName.isEmpty());

        QTemporaryDir dir;
        Database db(QStringLiteral("fm27_save_test"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        Fm27SaveImport::setAttributeMasking(true);
        timer.restart();
        const ImportResult result = HtmlImporter::importFile(path, db, {}, {}, QStringLiteral("fm27"));
        QVERIFY2(result.success, qPrintable(result.error));
        qInfo() << "imported" << result.playersImported << "players in" << timer.elapsed() << "ms; unknown columns"
                << result.unknownColumns;
        QVERIFY(result.playersImported > 20);
        QVERIFY(result.unknownColumns.isEmpty());

        // Without masking every player with a club arrives (tens of thousands).
        Database all(QStringLiteral("fm27_save_test_all"));
        QVERIFY(all.open(dir.filePath(QStringLiteral("all.db"))));
        Fm27SaveImport::setAttributeMasking(false);
        timer.restart();
        const ImportResult unmasked = HtmlImporter::importFile(path, all, {}, {}, QStringLiteral("fm27"));
        Fm27SaveImport::setAttributeMasking(true);
        QVERIFY2(unmasked.success, qPrintable(unmasked.error));
        qInfo() << "imported unmasked" << unmasked.playersImported << "players in" << timer.elapsed() << "ms";
        QVERIFY(unmasked.playersImported > 10 * result.playersImported);
    }
};

QTEST_GUILESS_MAIN(TestFm27Save)
#include "test_fm27save.moc"
