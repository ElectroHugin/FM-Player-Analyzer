#include <QtTest>
#include <QSqlQuery>
#include "core/Database.h"
#include "core/HtmlImporter.h"
#include "core/ImportSchema.h"
#include "core/RatingCurve.h"

using namespace fm;
class TestVisibleAttributes : public QObject {
    Q_OBJECT
private slots:
    void importPersistAndEvaluate()
    {
        for (const auto &game : {QStringLiteral("fm24"), QStringLiteral("fm27")}) {
            QTemporaryDir directory;
            Database db(Database::uniqueConnectionName(QStringLiteral("visible")));
            QVERIFY(db.open(directory.filePath(QStringLiteral("test.db"))));
            const auto mapping = attributeMapping(game);
            QStringList headers{game == QStringLiteral("fm24") ? QStringLiteral("UID") : QStringLiteral("Unique ID"),
                                game == QStringLiteral("fm24") ? QStringLiteral("Name") : QStringLiteral("Player"),
                                QStringLiteral("Age"), QStringLiteral("Club"), QStringLiteral("Position")};
            QStringList values{QStringLiteral("123"), QStringLiteral("Example"), QStringLiteral("20"),
                               QStringLiteral("Test"), QStringLiteral("ST (C)")};
            for (const auto &attribute : {QStringLiteral("Natural Fitness"), QStringLiteral("Free Kick Taking"),
                                          QStringLiteral("Long Throws"), QStringLiteral("Penalty Taking"), QStringLiteral("Punching")}) {
                QVERIFY(mapping.values().contains(attribute));
                headers.append(mapping.key(attribute));
                values.append(attribute == QStringLiteral("Penalty Taking") ? QStringLiteral("-") : QStringLiteral("12-16"));
            }
            const auto html = QStringLiteral("<table><tr><th>%1</th></tr><tr><td>%2</td></tr></table>")
                                  .arg(headers.join(QStringLiteral("</th><th>")), values.join(QStringLiteral("</td><td>")));
            const auto imported = HtmlImporter::importHtml(html, db, {}, {}, game);
            QVERIFY2(imported.success, qPrintable(imported.error));
            const auto players = db.loadPlayers();
            QCOMPARE(players.size(), size_t(1));
            QCOMPARE(players[0].attrLo[idx(Attr::NaturalFitness)], uint8_t(12));
            QCOMPARE(players[0].attrHi[idx(Attr::NaturalFitness)], uint8_t(16));
            QCOMPARE(players[0].attrLo[idx(Attr::PenaltyTaking)], uint8_t(0));
            const auto curve = harvestgreenCurve({-12.6, -4, 0, 2.8}, 4);
            QVERIFY(curve);
            const auto score = evaluateCurves({{*curve, players[0].attrLo[idx(Attr::NaturalFitness)],
                                                 players[0].attrHi[idx(Attr::NaturalFitness)], 1}});
            QVERIFY(score.normalized);
        }
    }
    void migrationBacksUpAndPreservesUnknowns()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("old.db"));
        {
            Database seed(Database::uniqueConnectionName(QStringLiteral("seed")));
            QVERIFY(seed.open(path));
            std::vector<Player> players(1);
            players[0].uid = QStringLiteral("existing");
            players[0].name = QStringLiteral("Existing player");
            QVERIFY(seed.upsertPlayers(players));
            QSqlQuery query(seed.handle());
            for (const auto &attribute : {QStringLiteral("Natural Fitness"), QStringLiteral("Free Kick Taking"),
                                          QStringLiteral("Long Throws"), QStringLiteral("Penalty Taking"), QStringLiteral("Punching")})
                for (const auto &suffix : {QStringLiteral("_lo"), QStringLiteral("_hi")})
                    QVERIFY(query.exec(QStringLiteral("ALTER TABLE players DROP COLUMN %1")
                                           .arg(Database::attrColumnName(attribute) + suffix)));
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 16")));
        }
        {
            Database migrated(Database::uniqueConnectionName(QStringLiteral("migrated")));
            QVERIFY2(migrated.open(path), qPrintable(migrated.errorString()));
            const auto players = migrated.loadPlayers();
            QCOMPARE(players.size(), size_t(1));
            QVERIFY(!players[0].hasAttr(idx(Attr::NaturalFitness)));
        }
        const auto backups = QDir(directory.path()).entryList({QStringLiteral("old.db.pre-2.3.0-*.db")});
        QCOMPARE(backups.size(), 1);
        // Read the backup directly: Database::open would intentionally migrate it.
        const auto connection = Database::uniqueConnectionName(QStringLiteral("backup"));
        {
            auto sql = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            sql.setDatabaseName(directory.filePath(backups.front()));
            sql.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            QVERIFY(sql.open());
            QSqlQuery query(sql);
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 16);
            QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM players")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 1);
        }
        QSqlDatabase::removeDatabase(connection);
        Database reopened(Database::uniqueConnectionName(QStringLiteral("reopened")));
        QVERIFY(reopened.open(path));
        QCOMPARE(QDir(directory.path()).entryList({QStringLiteral("old.db.pre-2.3.0-*.db")}).size(), 1);
    }
};
QTEST_GUILESS_MAIN(TestVisibleAttributes)
#include "test_visibleattributes.moc"
