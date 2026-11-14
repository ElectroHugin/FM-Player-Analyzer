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
            QSqlQuery q(db.handle()); QVERIFY(q.exec("DROP TABLE dwrs_inputs")); QVERIFY(q.exec("DROP TABLE dwrs_basis"));
            QVERIFY(q.exec("ALTER TABLE dwrs_latest DROP COLUMN basis_key"));
            QVERIFY(q.exec("PRAGMA user_version=17"));
        }
        Database migrated(Database::uniqueConnectionName("migrated")); QVERIFY2(migrated.open(path), qPrintable(migrated.errorString()));
        const auto players = migrated.loadPlayers(); const auto rows = migrated.dwrsHistory({players[0].id});
        QCOMPARE(rows.size(), size_t(1)); QCOMPARE(rows[0].normalized, 55.0);
        QVERIFY(rows[0].profileRevision.isEmpty()); QVERIFY(rows[0].attributes.isEmpty());
        QCOMPARE(QDir(temp.path()).entryList({QStringLiteral("legacy.db.pre-2.3.0-*.db")}).size(), 1);
        QSqlQuery q(migrated.handle()); QVERIFY(q.exec("PRAGMA user_version")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 19);
    }
    // 2.3.0 test builds stored the inputs once per rating (schema 18): they move
    // to one row per player and batch, shared by all roles.
    void migrationFromPerRatingProvenance() {
        QTemporaryDir temp; const auto path = temp.filePath("v18.db");
        QByteArray attrs(2 * kAttrCount, 0); attrs[idx(Attr::Pace)] = 9; attrs[idx(Attr::Pace) + kAttrCount] = 11;
        int id = 0;
        {
            Database db(Database::uniqueConnectionName("v18")); QVERIFY(db.open(path));
            std::vector<Player> players(1); players[0].uid = "one"; players[0].name = "One"; QVERIFY(db.upsertPlayers(players));
            id = players[0].id;
            QVERIFY(db.appendDwrsRatings({{id, "AF-A", 12, 55, "2026-03-01 10:00:00"}, {id, "P", 10, 50, "2026-03-01 10:00:00"}}));
            QSqlQuery q(db.handle());
            QVERIFY(q.exec("DROP TABLE dwrs_inputs")); QVERIFY(q.exec("DROP TABLE dwrs_basis"));
            QVERIFY(q.exec("CREATE TABLE dwrs_provenance (player_id INTEGER NOT NULL, role TEXT NOT NULL, ts TEXT NOT NULL,"
                           "basis_key TEXT NOT NULL, profile_id TEXT NOT NULL, profile_revision TEXT NOT NULL,"
                           "calculation_version TEXT NOT NULL, game_date TEXT, attributes BLOB NOT NULL,"
                           "attribute_format TEXT NOT NULL DEFAULT 'visible-lo-hi-v1', PRIMARY KEY(player_id, role, ts)) WITHOUT ROWID"));
            for (const char *role : {"AF-A", "P"}) {
                q.prepare("INSERT INTO dwrs_provenance VALUES (?, ?, '2026-03-01 10:00:00', 'basis', 'profile', 'rev', 'v1', '2027-03-01', ?, 'visible-lo-hi-v1')");
                q.addBindValue(id); q.addBindValue(QString::fromLatin1(role)); q.addBindValue(attrs); QVERIFY(q.exec());
            }
            QVERIFY(q.exec("PRAGMA user_version=18"));
        }
        Database migrated(Database::uniqueConnectionName("v19")); QVERIFY2(migrated.open(path), qPrintable(migrated.errorString()));
        const auto rows = migrated.dwrsHistory({id}); QCOMPARE(rows.size(), size_t(2));
        for (const auto &row : rows) {
            QCOMPARE(row.basisKey, QStringLiteral("basis")); QCOMPARE(row.profileRevision, QStringLiteral("rev"));
            QCOMPARE(row.gameDate, QDate(2027, 3, 1)); QCOMPARE(row.attributes, attrs);
        }
        QSqlQuery q(migrated.handle());
        QVERIFY(q.exec("SELECT count(*) FROM dwrs_inputs")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
        QVERIFY(q.exec("SELECT count(*) FROM sqlite_master WHERE name = 'dwrs_provenance'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
    }
    // All roles of one player in a batch share a single inputs row.
    void inputsStoredOncePerPlayerAndBatch() {
        QTemporaryDir temp; Database db(Database::uniqueConnectionName("once")); QVERIFY(db.open(temp.filePath("save.db")));
        std::vector<Player> players(2); players[0].uid = "a"; players[0].name = "A"; players[1].uid = "b"; players[1].name = "B";
        QVERIFY(db.upsertPlayers(players));
        std::vector<DwrsEntry> entries;
        for (const Player &player : players)
            for (const char *role : {"AF-A", "P", "CD-D"}) {
                DwrsEntry entry{player.id, role, 10, 50, "2026-04-01 10:00:00"};
                entry.basisKey = "basis"; entry.profileId = "profile"; entry.profileRevision = "rev";
                entry.calculationVersion = "v1"; entry.gameDate = QDate(2027, 4, 1);
                entry.attributes = QByteArray(2 * kAttrCount, 0); entry.attributes[0] = entry.attributes[kAttrCount] = char(player.id % 20 + 1);
                entries.push_back(entry);
            }
        QVERIFY2(db.appendDwrsRatings(entries), qPrintable(db.errorString()));
        QSqlQuery q(db.handle());
        QVERIFY(q.exec("SELECT count(*) FROM dwrs_inputs")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 2);
        QVERIFY(q.exec("SELECT count(*) FROM dwrs_basis")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
        const auto rows = db.dwrsHistory({players[1].id}); QCOMPARE(rows.size(), size_t(3));
        for (const auto &row : rows) QCOMPARE(row.attributes, entries[3].attributes);
    }
};
QTEST_GUILESS_MAIN(TestRatingProvenance)
#include "test_ratingprovenance.moc"
