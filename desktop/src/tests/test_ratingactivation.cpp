// Rating profile selection through AppContext: defaults for new saves, the
// one-time FM27 question for existing saves, activation and personal weights.
// Runs in a temporary data folder; no real save is touched.
#include <QtTest>
#include <QSettings>
#include <QSignalSpy>

#include "AppContext.h"
#include "core/DwrsEngine.h"
#include "core/ImportPipeline.h"

using namespace fm;

class TestRatingActivation : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

    void startApp(AppContext &context)
    {
        context.paths().setDataDir(m_dir.path());
        QString error;
        QVERIFY2(context.initialize(&error, false), qPrintable(error));
    }
    // A save as 2.2.3 left it: players, no rating selection yet.
    void existingSave(const QString &name, const QString &game)
    {
        AppPaths paths;
        paths.setDataDir(m_dir.path());
        Database db(Database::uniqueConnectionName(name));
        QVERIFY(db.open(paths.databaseFile(name)));
        QVERIFY(db.setSetting(QStringLiteral("fm_version"), game));
        std::vector<Player> players(1);
        players[0].uid = QStringLiteral("p1");
        players[0].name = QStringLiteral("Test Player");
        for (int a = 0; a < kAttrCount; ++a)
            players[0].attrLo[a] = players[0].attrHi[a] = 5 + a % 12;
        QVERIFY(db.upsertPlayers(players));
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_dir.isValid());
        AppPaths paths;
        paths.setDataDir(m_dir.path());
        QVERIFY(paths.ensureDataDirInitialized());
    }

    void newSavesGetTheirGameDefault()
    {
        AppContext context;
        startApp(context);
        QString error;
        QVERIFY2(context.createDatabase(QStringLiteral("new27"), QStringLiteral("fm27"), &error), qPrintable(error));
        QCOMPARE(context.activeRatingProfileId(), QStringLiteral("dwrs-fm27-harvestgreen22"));
        QVERIFY(context.ratingSnapshot() && !context.ratingSnapshot()->isClassic());
        QVERIFY(!context.ratingDecisionPending());
        QVERIFY2(context.createDatabase(QStringLiteral("new24"), QStringLiteral("fm24"), &error), qPrintable(error));
        QCOMPARE(context.activeRatingProfileId(), QStringLiteral("dwrs-classic-florin133"));
        QVERIFY(context.ratingSnapshot()->isClassic());
        // Harvestgreen is FM27-only.
        for (const auto &profile : context.compatibleRatingProfiles())
            QVERIFY(profile.id != QStringLiteral("dwrs-fm27-harvestgreen22"));
        QVERIFY(!context.activateRatingProfile(QStringLiteral("dwrs-fm27-harvestgreen22")).isEmpty());
    }

    void existingFm27SaveAsksOnceAndSwitches()
    {
        existingSave(QStringLiteral("old27"), QStringLiteral("fm27"));
        {
            AppContext context;
            startApp(context);
            QString error;
            QVERIFY2(context.openDatabase(QStringLiteral("old27"), &error), qPrintable(error));
            QVERIFY(context.ratingDecisionPending());
            QVERIFY(context.ratingSnapshot()->isClassic()); // previous ratings stay until the choice
            QSignalSpy recalc(&context, &AppContext::ratingRecalcRequested);
            QVERIFY(context.activateRatingProfile(QStringLiteral("dwrs-fm27-harvestgreen22")).isEmpty());
            QCOMPARE(recalc.count(), 1);
            QVERIFY(!context.ratingDecisionPending());
            QVERIFY(!context.ratingSnapshot()->isClassic());
        }
        AppContext reopened;
        startApp(reopened);
        QString error;
        QVERIFY(reopened.openDatabase(QStringLiteral("old27"), &error));
        QCOMPARE(reopened.activeRatingProfileId(), QStringLiteral("dwrs-fm27-harvestgreen22"));
        QVERIFY(!reopened.ratingDecisionPending());
    }

    void existingFm27SaveCanKeepItsRatings()
    {
        existingSave(QStringLiteral("keep27"), QStringLiteral("fm27"));
        {
            AppContext context;
            startApp(context);
            QString error;
            QVERIFY(context.openDatabase(QStringLiteral("keep27"), &error));
            QVERIFY(context.ratingDecisionPending());
            QVERIFY(context.keepPreviousRatingProfile().isEmpty());
            QVERIFY(!context.ratingDecisionPending());
        }
        AppContext reopened;
        startApp(reopened);
        QString error;
        QVERIFY(reopened.openDatabase(QStringLiteral("keep27"), &error));
        QVERIFY(!reopened.ratingDecisionPending());
        QVERIFY(reopened.ratingSnapshot()->isClassic());
    }

    void customWeightsBecomePersonalProfile()
    {
        existingSave(QStringLiteral("own24"), QStringLiteral("fm24"));
        AppPaths paths;
        paths.setDataDir(m_dir.path());
        {
            QSettings ini(paths.configFile(), QSettings::IniFormat);
            ini.setValue(QStringLiteral("Weights/extremely_important"), QStringLiteral("11"));
        }
        AppContext context;
        startApp(context);
        QString error;
        QVERIFY(context.openDatabase(QStringLiteral("own24"), &error));
        QVERIFY(context.activeRatingProfileId().startsWith(QStringLiteral("my-weights-fm24")));
        QVERIFY(!context.ratingDecisionPending());
        // Same numbers as the 2.2.3 engine with those weights.
        DwrsEngine engine(context.definitions(), context.config());
        const Player &player = context.store().at(0);
        for (const QString &role : context.definitions().validRoles()) {
            const auto score = context.ratingSnapshot()->evaluate(player, role);
            QVERIFY(score.normalized);
            QCOMPARE(*score.normalized, engine.calculate(player, role).second);
        }
        bool listed = false;
        for (const auto &profile : context.compatibleRatingProfiles())
            listed = listed || profile.id == context.activeRatingProfileId();
        QVERIFY(listed);
    }
    // Optional: FM27_TEST_SAVE=<copy of a real .fm save>. Imports it into a new
    // temporary FM27 save through the app's pipeline and prints the best
    // ratings of FM27_TEST_PLAYER (default Harry Kane). The file is only read.
    void optionalRealSaveImport()
    {
        const QString source = qEnvironmentVariable("FM27_TEST_SAVE");
        if (source.isEmpty())
            QSKIP("FM27_TEST_SAVE not set");
        AppContext context;
        startApp(context);
        QString error;
        QVERIFY(context.createDatabase(QStringLiteral("real27"), QStringLiteral("fm27"), &error));
        ImportPipeline::Request request;
        request.filePath = source;
        request.dbFile = context.database().filePath();
        request.fmVersionId = QStringLiteral("fm27");
        const auto result = ImportPipeline::run(request, context.definitions(), *context.ratingSnapshot());
        QVERIFY2(result.import.success, qPrintable(result.import.error));
        QVERIFY2(result.recalc.success, qPrintable(result.recalc.error));
        const QString name = qEnvironmentVariable("FM27_TEST_PLAYER", QStringLiteral("Harry Kane"));
        for (const Player &player : result.store.players()) {
            if (player.name != name) continue;
            QList<QPair<double, QString>> rows;
            for (auto it = result.latestRatings.begin(); it != result.latestRatings.end(); ++it)
                if (it.key().first == player.id) rows.append({it.value().second, it.key().second});
            std::sort(rows.begin(), rows.end(), [](auto a, auto b) { return a.first > b.first; });
            QString line = player.name + QStringLiteral(" (") + player.club + QStringLiteral("):");
            for (const auto &row : rows) line += QStringLiteral(" %1 %2").arg(row.second).arg(row.first, 0, 'f', 1);
            qInfo("%s", qPrintable(line));
        }
    }
};

QTEST_MAIN(TestRatingActivation)
#include "test_ratingactivation.moc"
