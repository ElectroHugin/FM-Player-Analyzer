#include <QtTest>
#include <QSqlQuery>
#include <QDir>
#include "core/Database.h"
#include "core/Utils.h"
using namespace fm;
class TestRatingProvenance : public QObject {
    Q_OBJECT
private slots:
    void attributesAndLatestWithoutHistory() {
        QTemporaryDir temp; Database db(Database::uniqueConnectionName("provenance")); QVERIFY(db.open(temp.filePath("save.db")));
        std::vector<Player> players(1); players[0].uid = "one"; players[0].name = "One"; QVERIFY(db.upsertPlayers(players));
        DwrsEntry old{players[0].id, "AF-A", 12, 55, "2026-01-01 10:00:00"}; QVERIFY(db.appendDwrsRatings({old}));
        DwrsEntry entry{players[0].id, "AF-A", 14.5, 68.4, "2026-02-01 10:00:00.123"};
        entry.basisKey = "basis"; entry.profileId = "profile"; entry.profileRevision = "revision";
        entry.calculationVersion = "curves-uniform-range-v1"; entry.gameDate = QDate(2027, 2, 1);
        entry.attributes = QByteArray(2 * kAttrCount, 0);
        entry.attributes[idx(Attr::Pace)] = 8; entry.attributes[idx(Attr::Pace) + kAttrCount] = 12;
        entry.attributes[idx(Attr::NaturalFitness)] = entry.attributes[idx(Attr::NaturalFitness) + kAttrCount] = 19;
        QVERIFY2(db.appendDwrsRatings({entry}), qPrintable(db.errorString()));
        const auto rows = db.dwrsHistory({players[0].id}); QCOMPARE(rows.size(), size_t(2));
        QVERIFY(rows[0].basisKey.isEmpty()); QVERIFY(rows[0].attributes.isEmpty());
        QCOMPARE(rows[1].attributes, entry.attributes); QCOMPARE(rows[1].gameDate, entry.gameDate);
        QCOMPARE(rows[1].basisKey, entry.basisKey); QCOMPARE(rows[1].profileRevision, entry.profileRevision);
        QVERIFY(parseDwrsTimestamp(rows[1].timestamp).isValid());
        entry.normalized = 68.6; entry.timestamp = "2026-02-02 10:00:00.123";
        QVERIFY(db.appendDwrsRatings({entry}, RatingsOrigin::Import, false));
        QCOMPARE(db.dwrsHistory({players[0].id}).size(), size_t(2));
        QCOMPARE(db.latestDwrsRatings().value({players[0].id, "AF-A"}).second, 68.6);
        auto invalid = entry; invalid.attributes.clear(); QVERIFY(!db.appendDwrsRatings({invalid}));
        QCOMPARE(db.dwrsHistory({players[0].id}).size(), size_t(2));
    }
    void migrationPreservesUnknownAndBackup() {
        QTemporaryDir temp; const auto path = temp.filePath("legacy.db");
        {
            Database db(Database::uniqueConnectionName("legacy")); QVERIFY(db.open(path));
            std::vector<Player> players(1); players[0].uid = "one"; players[0].name = "One"; QVERIFY(db.upsertPlayers(players));
            QVERIFY(db.appendDwrsRatings({{players[0].id, "AF-A", 12, 55, "2026-01-01 10:00:00"}}));
            QSqlQuery q(db.handle()); QVERIFY(q.exec("DROP TABLE dwrs_provenance"));
            QVERIFY(q.exec("ALTER TABLE dwrs_latest DROP COLUMN basis_key"));
            QVERIFY(q.exec("PRAGMA user_version=17"));
        }
        Database migrated(Database::uniqueConnectionName("migrated")); QVERIFY2(migrated.open(path), qPrintable(migrated.errorString()));
        const auto players = migrated.loadPlayers(); const auto rows = migrated.dwrsHistory({players[0].id});
        QCOMPARE(rows.size(), size_t(1)); QCOMPARE(rows[0].normalized, 55.0);
        QVERIFY(rows[0].profileRevision.isEmpty()); QVERIFY(rows[0].attributes.isEmpty());
        QCOMPARE(QDir(temp.path()).entryList({QStringLiteral("legacy.db.pre-2.3.0-*.db")}).size(), 1);
        QSqlQuery q(migrated.handle()); QVERIFY(q.exec("PRAGMA user_version")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 18);
    }
};
QTEST_GUILESS_MAIN(TestRatingProvenance)
#include "test_ratingprovenance.moc"
