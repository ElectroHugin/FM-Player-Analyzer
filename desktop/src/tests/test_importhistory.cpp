#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "core/Database.h"
#include "core/HtmlImporter.h"
#include "core/ImportHistory.h"

using namespace fm;

namespace {

ImportHistory::ImportRecord importAt(const QString &when, const QDate &gameDate)
{
    ImportHistory::ImportRecord r;
    r.importedAt = when;
    r.gameDate = gameDate;
    r.source = QStringLiteral("test.fm");
    r.format = QStringLiteral("fm");
    return r;
}

ImportHistory::Snapshot snap(int playerId, int pace, double rating = 7.0)
{
    ImportHistory::Snapshot s;
    s.playerId = playerId;
    s.attrLo[0] = s.attrHi[0] = uint8_t(pace);
    s.averageRating = rating;
    s.transferValue = 1000000;
    s.transferValueText = QStringLiteral("€1M");
    s.wage = QStringLiteral("€10K p/w");
    return s;
}

std::vector<Player> twoPlayers(Database &db)
{
    std::vector<Player> players(2);
    players[0].uid = QStringLiteral("1");
    players[0].name = QStringLiteral("A");
    players[1].uid = QStringLiteral("2");
    players[1].name = QStringLiteral("B");
    if (!db.upsertPlayers(players))
        return {};
    return players;
}

} // namespace

class TestImportHistory : public QObject
{
    Q_OBJECT

private slots:
    void seasons()
    {
        QCOMPARE(ImportHistory::seasonOf(QDate(2026, 7, 1)), 2026);
        QCOMPARE(ImportHistory::seasonOf(QDate(2027, 6, 30)), 2026);
        QCOMPARE(ImportHistory::seasonLabel(2026), QStringLiteral("2026/27"));
        QCOMPARE(ImportHistory::seasonLabel(2099), QStringLiteral("2099/00"));
        QCOMPARE(ImportHistory::periodKey(QDate(2027, 1, 15), false), QStringLiteral("2027-01"));
        QCOMPARE(ImportHistory::periodKey(QDate(2027, 1, 15), true), QStringLiteral("S2026"));

        // Other rhythms: a calendar-year league and one starting in February.
        const ImportHistory::SeasonStart calendar{1, 1};
        QCOMPARE(ImportHistory::seasonOf(QDate(2027, 1, 1), calendar), 2027);
        QCOMPARE(ImportHistory::seasonLabel(2027, calendar), QStringLiteral("2027"));
        const ImportHistory::SeasonStart february{2, 15};
        QCOMPARE(ImportHistory::seasonOf(QDate(2027, 2, 14), february), 2026);
        QCOMPARE(ImportHistory::seasonOf(QDate(2027, 2, 15), february), 2027);
        QCOMPARE(ImportHistory::seasonStartText(february), QStringLiteral("02-15"));
        QVERIFY(ImportHistory::parseSeasonStart(QStringLiteral("02-15")) == february);
        QVERIFY(ImportHistory::parseSeasonStart(QStringLiteral("02-30")) == ImportHistory::SeasonStart{});
        QVERIFY(ImportHistory::parseSeasonStart(QString()) == ImportHistory::SeasonStart{});
    }

    void condensesByConfiguredSeasonStart()
    {
        // Calendar-year seasons: 2024 leaves the window in 2027.
        QTemporaryDir dir;
        Database db(QStringLiteral("ih_calendar"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        QVERIFY(db.setSetting(QString::fromLatin1(ImportHistory::kSeasonStartSetting),
                              QStringLiteral("01-01")));
        const std::vector<Player> players = twoPlayers(db);
        const int a = players[0].id;
        int pace = 10;
        int minute = 0;
        const auto importOn = [&](const QDate &date) {
            const QString when = QStringLiteral("2026-11-06 13:%1:00").arg(minute++, 2, 10, QLatin1Char('0'));
            return ImportHistory::record(db, importAt(when, date), {snap(a, pace++)});
        };
        QVERIFY(importOn(QDate(2024, 3, 1)));
        QVERIFY(importOn(QDate(2024, 11, 1)));
        QVERIFY(importOn(QDate(2026, 12, 1)));
        QCOMPARE(db.snapshots(a).size(), size_t(3));
        QVERIFY(importOn(QDate(2027, 1, 10)));
        const auto rows = db.snapshots(a);
        QCOMPARE(rows.size(), size_t(3));
        QVERIFY(rows[0].condensed);
        QCOMPARE(rows[0].period, QStringLiteral("S2024"));
        QCOMPARE(rows[0].gameDate, QDate(2024, 11, 1));
    }

    void logsImportsAndFindsGameDate()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("ih_log"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        QVERIFY(!db.lastGameDate().isValid());
        QVERIFY(ImportHistory::record(db, importAt("2026-11-06 18:00:00", QDate(2026, 8, 3)), {}));
        QVERIFY(ImportHistory::record(db, importAt("2026-11-06 19:00:00", {}), {}));
        QCOMPARE(db.imports().size(), size_t(2));
        QCOMPARE(db.lastGameDate(), QDate(2026, 8, 3));

        const auto imports = db.imports();
        const auto at = [](const char *ts) {
            return QDateTime::fromString(QString::fromLatin1(ts), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        };
        QVERIFY(!ImportHistory::gameDateAt(imports, at("2026-11-06 17:59:59")).isValid());
        QCOMPARE(ImportHistory::gameDateAt(imports, at("2026-11-06 18:00:05")), QDate(2026, 8, 3));
        // The later import had no date: nothing to show for it.
        QVERIFY(!ImportHistory::gameDateAt(imports, at("2026-11-06 19:30:00")).isValid());
    }

    void monthlyOnlyChanges()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("ih_month"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        const std::vector<Player> players = twoPlayers(db);
        QCOMPARE(players.size(), size_t(2));
        const int a = players[0].id;
        const int b = players[1].id;

        // August: first state of both.
        QVERIFY(ImportHistory::record(db, importAt("2026-11-01 10:00:00", QDate(2026, 8, 10)),
                                      {snap(a, 12), snap(b, 14)}));
        // Same month again: the newer state replaces August's for both.
        QVERIFY(ImportHistory::record(db, importAt("2026-11-01 11:00:00", QDate(2026, 8, 24)),
                                      {snap(a, 13), snap(b, 14)}));
        QCOMPARE(db.snapshots(a).size(), size_t(1));
        QCOMPARE(db.snapshots(a).front().attrLo[0], uint8_t(13));
        QCOMPARE(db.snapshots(a).front().gameDate, QDate(2026, 8, 24));
        QCOMPARE(db.snapshots(b).size(), size_t(1));
        QCOMPARE(db.snapshots(b).front().gameDate, QDate(2026, 8, 24));

        // September: A unchanged -> no row; B's rating moved -> new row.
        QVERIFY(ImportHistory::record(db, importAt("2026-11-02 10:00:00", QDate(2026, 9, 5)),
                                      {snap(a, 13), snap(b, 14, 7.2)}));
        QCOMPARE(db.snapshots(a).size(), size_t(1));
        QCOMPARE(db.snapshots(b).size(), size_t(2));

        // September again: B back to August's data -> its September row goes.
        QVERIFY(ImportHistory::record(db, importAt("2026-11-02 11:00:00", QDate(2026, 9, 20)),
                                      {snap(a, 13), snap(b, 14, 7.0)}));
        QCOMPARE(db.snapshots(b).size(), size_t(1));
        QCOMPARE(db.snapshots(b).front().transferValueText, QStringLiteral("€1M"));
    }

    void olderImportKeepsNewerMonthState()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("ih_order"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        const std::vector<Player> players = twoPlayers(db);
        const int a = players[0].id;
        const int b = players[1].id;
        QVERIFY(ImportHistory::record(db, importAt("2026-11-01 10:00:00", QDate(2026, 7, 20)),
                                      {snap(b, 14)}));
        QVERIFY(ImportHistory::record(db, importAt("2026-11-01 11:00:00", QDate(2026, 8, 30)),
                                      {snap(a, 15), snap(b, 16)}));
        // An older export of the same month, imported afterwards: neither
        // replaces (A) nor removes (B, same data as July) the newer state.
        QVERIFY(ImportHistory::record(db, importAt("2026-11-01 12:00:00", QDate(2026, 8, 10)),
                                      {snap(a, 12), snap(b, 14)}));
        QCOMPARE(db.snapshots(a).size(), size_t(1));
        QCOMPARE(db.snapshots(a).front().gameDate, QDate(2026, 8, 30));
        QCOMPARE(db.snapshots(a).front().attrLo[0], uint8_t(15));
        QCOMPARE(db.snapshots(b).size(), size_t(2));
        QCOMPARE(db.snapshots(b).back().attrLo[0], uint8_t(16));
    }

    void olderImportKeepsUnchangedNewerMonth()
    {
        // July 14; on 30 August still 14 (no row stored); an older export of
        // 10 August with 12 imported afterwards must not become August.
        QTemporaryDir dir;
        Database db(QStringLiteral("ih_seen"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        const std::vector<Player> players = twoPlayers(db);
        const int a = players[0].id;
        QVERIFY(ImportHistory::record(db, importAt("2026-11-01 10:00:00", QDate(2026, 7, 20)),
                                      {snap(a, 14)}));
        QVERIFY(ImportHistory::record(db, importAt("2026-11-01 11:00:00", QDate(2026, 8, 30)),
                                      {snap(a, 14)}));
        QCOMPARE(db.snapshots(a).size(), size_t(1));
        QVERIFY(ImportHistory::record(db, importAt("2026-11-01 12:00:00", QDate(2026, 8, 10)),
                                      {snap(a, 12)}));
        QCOMPARE(db.snapshots(a).size(), size_t(1));
        QCOMPARE(db.snapshots(a).front().attrLo[0], uint8_t(14));
        // A newer export still records normally.
        QVERIFY(ImportHistory::record(db, importAt("2026-11-01 13:00:00", QDate(2026, 8, 31)),
                                      {snap(a, 13)}));
        QCOMPARE(db.snapshots(a).size(), size_t(2));
    }

    void noDateNoSnapshots()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("ih_nodate"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        const std::vector<Player> players = twoPlayers(db);
        QVERIFY(ImportHistory::record(db, importAt("2026-11-01 10:00:00", {}),
                                      {snap(players[0].id, 12)}));
        QVERIFY(db.snapshots(players[0].id).empty());
        QCOMPARE(db.imports().size(), size_t(1));
    }

    void condensesOldSeasons()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("ih_condense"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        const std::vector<Player> players = twoPlayers(db);
        const int a = players[0].id;

        // Season 2026/27: three monthly states.
        int pace = 10;
        int minute = 0;
        const auto importOn = [&](const QDate &date) {
            const QString when = QStringLiteral("2026-11-06 12:%1:00").arg(minute++, 2, 10, QLatin1Char('0'));
            return ImportHistory::record(db, importAt(when, date), {snap(a, pace++)});
        };
        QVERIFY(importOn(QDate(2026, 8, 1)));
        QVERIFY(importOn(QDate(2026, 12, 1)));
        QVERIFY(importOn(QDate(2027, 5, 1)));
        // 2027/28 and 2028/29 keep 2026/27 monthly (two seasons back).
        QVERIFY(importOn(QDate(2027, 9, 1)));
        QVERIFY(importOn(QDate(2028, 9, 1)));
        QCOMPARE(db.snapshots(a).size(), size_t(5));
        // 2029/30: 2026/27 leaves the window and collapses to May 2027.
        QVERIFY(importOn(QDate(2029, 9, 1)));
        const auto rows = db.snapshots(a);
        QCOMPARE(rows.size(), size_t(4));
        QVERIFY(rows[0].condensed);
        QCOMPARE(rows[0].gameDate, QDate(2027, 5, 1));
        QCOMPARE(rows[0].attrLo[0], uint8_t(12));
        QVERIFY(!rows[1].condensed);
        QCOMPARE(rows[1].gameDate, QDate(2027, 9, 1));
        // A condensed season does not shrink further.
        QVERIFY(importOn(QDate(2029, 10, 1)));
        QCOMPARE(db.snapshots(a).size(), size_t(5));
        QVERIFY(db.snapshots(a).front().condensed);
    }

    void scope()
    {
        ImportHistory::Scope scope;
        scope.userClub = QStringLiteral("FC Test");
        scope.secondTeamClub = QStringLiteral("FC Test II");
        scope.knownUids = {QStringLiteral("77")};
        Player p;
        p.uid = QStringLiteral("5");
        p.club = QStringLiteral("Other");
        QVERIFY(!scope.contains(p));
        p.club = QStringLiteral("FC Test II");
        QVERIFY(scope.contains(p));
        p.club = QStringLiteral("Other");
        p.onShortlist = true;
        QVERIFY(scope.contains(p));
        p.onShortlist = false;
        p.uid = QStringLiteral("r-77"); // unified newgen id
        QVERIFY(scope.contains(p));
        p.uid = QStringLiteral("8");
        scope.allPlayers = true;
        QVERIFY(scope.contains(p));
    }

    void importFileTakesTheGivenDate()
    {
        // End to end through the importer: own club followed, others not.
        QTemporaryDir dir;
        Database db(QStringLiteral("ih_import"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        QVERIFY(db.setSetting(QStringLiteral("user_club"), QStringLiteral("FC Test")));
        const QString path = dir.filePath(QStringLiteral("export.csv"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("UID;Name;Age;Club;Position;Personality;Transfer Value;Height;Acc;Pac;Fin;Avg Rat\n"
                   "1001;Alpha;24;FC Test;D (C);Balanced;\xE2\x82\xAC" "1.2M;181 cm;14;15;12;7.10\n"
                   "1002;Beta;25;Other FC;ST (C);Balanced;\xE2\x82\xAC" "2M;175 cm;9;11;16;6.90\n");
        file.close();

        std::vector<Player> players;
        const ImportResult result = HtmlImporter::importFile(path, db, players, {}, QString(), {},
                                                             &players, QDate(2026, 10, 4));
        QVERIFY2(result.success, qPrintable(result.error));
        QCOMPARE(result.gameDate, QDate(2026, 10, 4));
        QCOMPARE(result.snapshots, 1);
        QCOMPARE(db.imports().size(), size_t(1));
        QCOMPARE(db.imports().front().source, QStringLiteral("export.csv"));
        QCOMPARE(db.imports().front().format, QStringLiteral("csv"));
        const Player *alpha = nullptr;
        for (const Player &p : players)
            if (p.uid == QLatin1String("1001"))
                alpha = &p;
        QVERIFY(alpha);
        const auto rows = db.snapshots(alpha->id);
        QCOMPARE(rows.size(), size_t(1));
        QCOMPARE(rows.front().gameDate, QDate(2026, 10, 4));
        QCOMPARE(rows.front().transferValueText, QStringLiteral("€1.2M"));
    }

    void mergeMovesSnapshots()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("ih_merge"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        const std::vector<Player> players = twoPlayers(db);
        QVERIFY(ImportHistory::record(db, importAt("2026-11-01 10:00:00", QDate(2026, 8, 1)),
                                      {snap(players[1].id, 9)}));
        QVERIFY(db.mergePlayerInto(players[1].id, players[0].id));
        QCOMPARE(db.snapshots(players[0].id).size(), size_t(1));
    }
};

QTEST_GUILESS_MAIN(TestImportHistory)
#include "test_importhistory.moc"
