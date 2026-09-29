#include <QtTest>

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "core/Database.h"
#include "core/Player.h"

using namespace fm;

class TestDatabase : public QObject
{
    Q_OBJECT

private slots:
    // Regression for the WAL backup bug: a freshly committed player lives in the
    // -wal sidecar, not yet in the main .db file. createBackup must capture it
    // (VACUUM INTO), which a plain file copy of the .db would not.
    void bulkRatingsAndChunkedReads()
    {
        // Backlog #31/#32/#22: multi-row rating writes across chunk boundaries,
        // role-only writes, and id-chunked reads beyond 500 ids.
        QTemporaryDir dir;
        Database db(QStringLiteral("bulk_test"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));

        std::vector<Player> players(620);
        for (int i = 0; i < 620; ++i) {
            players[i].uid = QString::number(i + 1);
            players[i].name = players[i].uid;
            players[i].assignedRoles = {QStringLiteral("CM-S")};
        }
        QVERIFY(db.upsertPlayers(players));

        // 620 rows at ts B (spans 4 chunks incl. a partial one) ...
        const QString tsA = QStringLiteral("2026-01-01 10:00:00");
        const QString tsB = QStringLiteral("2026-02-01 10:00:00");
        std::vector<DwrsEntry> newer;
        for (const Player &p : players)
            newer.push_back({p.id, QStringLiteral("CM-S"), 1.0, 70.0, tsB});
        QVERIFY(db.appendDwrsRatings(newer));
        // ... then an OLDER append must not move dwrs_latest backwards, while
        // the history keeps both rows.
        std::vector<DwrsEntry> older;
        for (const Player &p : players)
            older.push_back({p.id, QStringLiteral("CM-S"), 1.0, 50.0, tsA});
        QVERIFY(db.appendDwrsRatings(older));

        QList<int> ids;
        for (const Player &p : players)
            ids << p.id;
        const LatestRatings all = db.latestDwrsRatings();
        const LatestRatings some = db.latestDwrsRatings(ids); // > 500 ids -> chunked
        QCOMPARE(all.size(), 620);
        QCOMPARE(some.size(), 620);
        for (const int id : ids)
            QCOMPARE(some.value({id, QStringLiteral("CM-S")}).second, 70.0);

        const auto history = db.dwrsHistory(ids);
        QCOMPARE(static_cast<int>(history.size()), 1240);
        for (size_t i = 1; i < history.size(); ++i) { // globally ordered across chunks
            QVERIFY(history[i - 1].playerId < history[i].playerId
                    || (history[i - 1].playerId == history[i].playerId
                        && history[i - 1].timestamp <= history[i].timestamp));
        }

        // Role-only write and a role-preserving upsert.
        QVERIFY(db.replacePlayerRoles({{players[0].id, {QStringLiteral("AP-S")}}}));
        players[1].assignedRoles.clear(); // must NOT reach the DB with RoleWrite::Keep
        players[1].age = 30;
        std::vector<Player> update{players[1]};
        QVERIFY(db.upsertPlayers(update, Database::RoleWrite::Keep));
        const auto reloaded = db.loadPlayers();
        QCOMPARE(reloaded[0].assignedRoles, QStringList{QStringLiteral("AP-S")});
        QCOMPARE(reloaded[1].assignedRoles, QStringList{QStringLiteral("CM-S")});
        QCOMPARE(reloaded[1].age, 30);
    }

    void loadPlayersSubsetMatchesFullLoad()
    {
        // Backlog #11: the targeted re-read must return exactly what the full
        // load returns for those players, side tables included.
        QTemporaryDir dir;
        Database db(QStringLiteral("subset_test"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        std::vector<Player> players(700);
        for (int i = 0; i < 700; ++i) {
            players[i].uid = QString::number(i + 1);
            players[i].name = QStringLiteral("P%1").arg(i);
            players[i].age = 18 + i % 20;
            players[i].assignedRoles = {QStringLiteral("CM-S"), QStringLiteral("W-S")};
            players[i].attrLo[3] = static_cast<uint8_t>(1 + i % 20);
            players[i].attrHi[3] = static_cast<uint8_t>(1 + i % 20);
        }
        QVERIFY(db.upsertPlayers(players));
        QVERIFY(db.setNationalSquadIds({players[5].id, players[650].id}));
        QVERIFY(db.setShortlistIds({players[6].id}));
        QVERIFY(db.setTrainingRole(players[7].id, QStringLiteral("W-S")));
        PlayerRegistration registration;
        registration.clubTrained = true;
        registration.homeGrown = true;
        registration.u21 = PlayerRegistration::U21::No;
        registration.uefaListed = true;
        QVERIFY(db.setRegistrations({{players[8].id, registration}}));

        const auto all = db.loadPlayers();
        QList<int> ids;
        for (int i = 0; i < 700; i += 3) // > 500 ids -> several chunks
            ids << players[i].id;
        ids << players[5].id << players[6].id << players[7].id << players[8].id << players[650].id
            << 999999 /* unknown -> skipped */;
        const auto subset = db.loadPlayers(ids);

        QHash<int, const Player *> byId;
        for (const Player &p : all)
            byId.insert(p.id, &p);
        QSet<int> expected(ids.cbegin(), ids.cend());
        expected.remove(999999);
        QCOMPARE(static_cast<int>(subset.size()), expected.size());
        for (size_t i = 0; i < subset.size(); ++i) {
            const Player &a = subset[i];
            const Player &b = *byId.value(a.id);
            if (i > 0)
                QVERIFY(subset[i - 1].id < a.id); // ordered by id
            QCOMPARE(a.uid, b.uid);
            QCOMPARE(a.name, b.name);
            QCOMPARE(a.age, b.age);
            QVERIFY(a.attrLo == b.attrLo && a.attrHi == b.attrHi);
            QCOMPARE(a.assignedRoles, b.assignedRoles);
            QCOMPARE(a.inNationalSquad, b.inNationalSquad);
            QCOMPARE(a.onShortlist, b.onShortlist);
            QCOMPARE(a.trainingRole, b.trainingRole);
            QVERIFY(a.registration == b.registration);
        }
        QVERIFY(byId.value(players[8].id)->registration == registration);
        QVERIFY(db.loadPlayers(QList<int>{}).empty());
    }

    void nestedTransactions()
    {
        // Backlog #27: write methods nest as savepoints inside an outer
        // transaction, so a caller can make several of them atomic.
        QTemporaryDir dir;
        Database db(QStringLiteral("tx_test"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        QSqlQuery trigger(db.handle());
        QVERIFY(trigger.exec(QStringLiteral(
            "CREATE TRIGGER boom BEFORE INSERT ON players WHEN NEW.uid = 'boom' "
            "BEGIN SELECT RAISE(ABORT, 'boom'); END")));

        const auto onePlayer = [](const QString &uid) {
            std::vector<Player> batch(1);
            batch[0].uid = uid;
            batch[0].name = uid;
            return batch;
        };

        // 1) Outer rollback discards inner work that "succeeded".
        {
            ScopedTransaction tx(db);
            QVERIFY(tx.isActive());
            auto a = onePlayer(QStringLiteral("a"));
            QVERIFY(db.upsertPlayers(a));
            QVERIFY(db.setSetting(QStringLiteral("k"), QStringLiteral("v")));
        } // no commit -> rollback
        QVERIFY(db.loadPlayers().empty());
        // The settings cache must not keep the rolled-back value.
        QCOMPARE(db.setting(QStringLiteral("k"), QStringLiteral("default")),
                 QStringLiteral("default"));

        // 2) A failing inner step only undoes itself; the outer can go on.
        {
            ScopedTransaction tx(db);
            auto b = onePlayer(QStringLiteral("b"));
            QVERIFY(db.upsertPlayers(b));
            auto bad = onePlayer(QStringLiteral("boom"));
            QVERIFY(!db.upsertPlayers(bad));
            QVERIFY(db.errorString().contains(QStringLiteral("boom")));
            QVERIFY(tx.commit());
        }
        const auto players = db.loadPlayers();
        QCOMPARE(static_cast<int>(players.size()), 1);
        QCOMPARE(players[0].uid, QStringLiteral("b"));

        // 3) Without an outer transaction every method is still atomic alone.
        auto c = onePlayer(QStringLiteral("c"));
        QVERIFY(db.upsertPlayers(c));
        QCOMPARE(static_cast<int>(db.loadPlayers().size()), 2);
    }

    void uniqueConnectionNames()
    {
        // Overlapping worker jobs must never share a connection name (backlog #28).
        const QString a = Database::uniqueConnectionName(QStringLiteral("import_worker"));
        const QString b = Database::uniqueConnectionName(QStringLiteral("import_worker"));
        QVERIFY(a != b);
        QVERIFY(a.startsWith(QStringLiteral("import_worker_")));
        QVERIFY(b.startsWith(QStringLiteral("import_worker_")));
    }

    void backupIncludesUncheckpointedWalData()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString dbFile = dir.filePath(QStringLiteral("save.db"));
        const QString backupsDir = dir.filePath(QStringLiteral("backups"));

        {
            Database db(QStringLiteral("test_backup_src"));
            QVERIFY(db.open(dbFile));

            Player player;
            player.uid = QStringLiteral("r-42");
            player.name = QStringLiteral("Müller");
            std::vector<Player> batch{player};
            QVERIFY(db.upsertPlayers(batch));

            // Back up while the source connection is still open (the import
            // scenario: the app holds the live connection during the backup).
            QString error;
            QVERIFY2(Database::createBackup(dbFile, backupsDir, &error), qPrintable(error));
            db.close();
        }

        const QDir bdir(backupsDir);
        const QStringList files = bdir.entryList({QStringLiteral("*.db")}, QDir::Files);
        QCOMPARE(files.size(), 1);

        Database backup(QStringLiteral("test_backup_dst"));
        QVERIFY(backup.open(bdir.filePath(files.first())));
        const std::vector<Player> restored = backup.loadPlayers();
        QCOMPARE(static_cast<int>(restored.size()), 1);
        QCOMPARE(restored[0].uid, QStringLiteral("r-42"));
        QCOMPARE(restored[0].name, QStringLiteral("Müller"));
        backup.close();
    }

    // dwrs_latest must always hold the newest rating per (player, role), and an
    // out-of-order (older) append must never make it go backwards.
    void dwrsLatestTracksNewest()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db(QStringLiteral("test_latest"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("save.db"))));

        Player p;
        p.uid = QStringLiteral("p1");
        p.name = QStringLiteral("Test"); // players.name is NOT NULL
        std::vector<Player> batch{p};
        QVERIFY2(db.upsertPlayers(batch), qPrintable(db.errorString()));
        const int id = batch[0].id;

        const auto entry = [id](double normalized, const QString &ts) {
            DwrsEntry e;
            e.playerId = id;
            e.role = QStringLiteral("CD");
            e.absolute = normalized / 5.0;
            e.normalized = normalized;
            e.timestamp = ts;
            return e;
        };

        QVERIFY(db.appendDwrsRatings({entry(50.0, QStringLiteral("2024-01-01 00:00:00"))}));
        QVERIFY(db.appendDwrsRatings({entry(60.0, QStringLiteral("2024-06-01 00:00:00"))}));
        QCOMPARE(db.latestDwrsRatings().value({id, QStringLiteral("CD")}).second, 60.0);

        // An older append is ignored by dwrs_latest.
        QVERIFY(db.appendDwrsRatings({entry(30.0, QStringLiteral("2023-01-01 00:00:00"))}));
        QCOMPARE(db.latestDwrsRatings().value({id, QStringLiteral("CD")}).second, 60.0);
        db.close();
    }

    // Opening a v1 database must materialize dwrs_latest from the existing
    // history and drop the dead registration/information columns.
    void migratesV1ToV2()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString dbFile = dir.filePath(QStringLiteral("v1.db"));

        // Build a minimal v1-shaped database by hand.
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                         QStringLiteral("v1setup"));
            raw.setDatabaseName(dbFile);
            QVERIFY(raw.open());
            QSqlQuery q(raw);
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE players (id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, "
                "name TEXT, registration TEXT, information TEXT)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE dwrs_history (player_id INTEGER, role TEXT, absolute REAL, "
                "normalized REAL, ts TEXT, PRIMARY KEY (player_id, role, ts))")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO players (id, uid, name) VALUES (1, 'p1', 'Test')")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO dwrs_history VALUES (1, 'CD', 10, 50, '2024-01-01 00:00:00')")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO dwrs_history VALUES (1, 'CD', 12, 60, '2024-06-01 00:00:00')")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 1")));
            raw.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v1setup"));

        // Opening through fm::Database triggers the v1 -> v2 migration.
        Database db(QStringLiteral("test_migrate"));
        QVERIFY2(db.open(dbFile), qPrintable(db.errorString()));

        const LatestRatings latest = db.latestDwrsRatings();
        QCOMPARE(latest.size(), 1);
        QCOMPARE(latest.value({1, QStringLiteral("CD")}).second, 60.0);

        QSqlQuery info(db.handle());
        QVERIFY(info.exec(QStringLiteral("PRAGMA table_info(players)")));
        QStringList cols;
        while (info.next())
            cols << info.value(1).toString();
        QVERIFY(!cols.contains(QStringLiteral("registration")));
        QVERIFY(!cols.contains(QStringLiteral("information")));
        // The v2 -> v3 step also ran as part of the chain.
        QVERIFY(cols.contains(QStringLiteral("last_seen_update")));
        db.close();
    }

    void lastSeenUpdateRoundTrips()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("test_lastseen"));
        QVERIFY2(db.open(dir.filePath(QStringLiteral("t.db"))), qPrintable(db.errorString()));

        std::vector<Player> batch(1);
        batch[0].uid = QStringLiteral("p1");
        batch[0].name = QStringLiteral("Stamp");
        batch[0].lastSeenUpdate = 7;
        QVERIFY2(db.upsertPlayers(batch), qPrintable(db.errorString()));

        const auto reloaded = db.loadPlayers();
        QCOMPARE(static_cast<int>(reloaded.size()), 1);
        QCOMPARE(reloaded[0].lastSeenUpdate, 7);
    }

    void migratesV2ToV3()
    {
        QTemporaryDir dir;
        const QString dbFile = dir.filePath(QStringLiteral("v2.db"));

        // Create a current-shape database, then roll it back to a v2 state by
        // dropping the freshness column and resetting the schema version.
        {
            Database db(QStringLiteral("v2seed"));
            QVERIFY2(db.open(dbFile), qPrintable(db.errorString()));
            std::vector<Player> batch(1);
            batch[0].uid = QStringLiteral("old");
            batch[0].name = QStringLiteral("Legacy");
            batch[0].lastSeenUpdate = 3;
            QVERIFY(db.upsertPlayers(batch));
            QSqlQuery q(db.handle());
            QVERIFY(q.exec(QStringLiteral("ALTER TABLE players DROP COLUMN last_seen_update")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 2")));
            db.close();
        }

        // Reopening runs the v2 -> v3 migration.
        Database db(QStringLiteral("v2migrate"));
        QVERIFY2(db.open(dbFile), qPrintable(db.errorString()));

        QSqlQuery info(db.handle());
        QVERIFY(info.exec(QStringLiteral("PRAGMA table_info(players)")));
        QStringList cols;
        while (info.next())
            cols << info.value(1).toString();
        QVERIFY(cols.contains(QStringLiteral("last_seen_update")));

        // Pre-existing row defaults to 0 (never stamped).
        const auto reloaded = db.loadPlayers();
        QCOMPARE(static_cast<int>(reloaded.size()), 1);
        QCOMPARE(reloaded[0].lastSeenUpdate, 0);
        db.close();
    }

    void registrationDefaultRemovesRow()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("test_registration_rows"));
        QVERIFY2(db.open(dir.filePath(QStringLiteral("t.db"))), qPrintable(db.errorString()));
        std::vector<Player> batch(1);
        batch[0].uid = QStringLiteral("p1");
        batch[0].name = QStringLiteral("P1");
        QVERIFY(db.upsertPlayers(batch));

        PlayerRegistration r;
        r.homeGrown = true;
        QVERIFY(db.setRegistrations({{batch[0].id, r}}));
        QVERIFY(db.loadPlayers()[0].registration.homeGrown);

        QVERIFY(db.setRegistrations({{batch[0].id, PlayerRegistration{}}}));
        QSqlQuery q(db.handle());
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM player_registration")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
        QVERIFY(db.loadPlayers()[0].registration.isDefault());
    }

    void mergeMovesRegistration()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("test_registration_merge"));
        QVERIFY2(db.open(dir.filePath(QStringLiteral("t.db"))), qPrintable(db.errorString()));
        std::vector<Player> batch(3);
        batch[0].uid = QStringLiteral("bad");
        batch[1].uid = QStringLiteral("good");
        batch[2].uid = QStringLiteral("flagged");
        for (Player &p : batch)
            p.name = p.uid;
        QVERIFY(db.upsertPlayers(batch));

        PlayerRegistration moved;
        moved.clubTrained = true;
        moved.homeGrown = true;
        QVERIFY(db.setRegistrations({{batch[0].id, moved}}));
        QVERIFY(db.mergePlayerInto(batch[0].id, batch[1].id));
        const auto afterFirst = db.loadPlayers(QList<int>{batch[1].id});
        QVERIFY(afterFirst.front().registration == moved);

        // A player that already has a status keeps his own.
        PlayerRegistration own;
        own.u21 = PlayerRegistration::U21::Yes;
        QVERIFY(db.setRegistrations({{batch[2].id, own}}));
        QVERIFY(db.mergePlayerInto(batch[1].id, batch[2].id));
        const auto afterSecond = db.loadPlayers(QList<int>{batch[2].id});
        QVERIFY(afterSecond.front().registration == own);
    }

    void migratesV4ToV5()
    {
        QTemporaryDir dir;
        const QString dbFile = dir.filePath(QStringLiteral("v4.db"));
        {
            Database db(QStringLiteral("v4seed"));
            QVERIFY2(db.open(dbFile), qPrintable(db.errorString()));
            std::vector<Player> batch(1);
            batch[0].uid = QStringLiteral("old");
            batch[0].name = QStringLiteral("Legacy");
            QVERIFY(db.upsertPlayers(batch));
            QSqlQuery q(db.handle());
            QVERIFY(q.exec(QStringLiteral("DROP TABLE player_registration")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 4")));
            db.close();
        }

        Database db(QStringLiteral("v4migrate"));
        QVERIFY2(db.open(dbFile), qPrintable(db.errorString()));
        const auto reloaded = db.loadPlayers();
        QCOMPARE(static_cast<int>(reloaded.size()), 1);
        QVERIFY(reloaded[0].registration.isDefault());
        PlayerRegistration r;
        r.leagueListed = true;
        QVERIFY2(db.setRegistrations({{reloaded[0].id, r}}), qPrintable(db.errorString()));
        QVERIFY(db.loadPlayers()[0].registration.leagueListed);
        db.close();
    }
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "test_database.moc"
