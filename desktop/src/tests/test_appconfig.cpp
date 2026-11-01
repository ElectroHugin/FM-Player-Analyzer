#include <QtTest>
#include <QSettings>

#include "core/AppConfig.h"
#include "core/Constants.h"

using namespace fm;

namespace {
const QString kFm24 = QStringLiteral("fm24");
}

class TestAppConfig : public QObject
{
    Q_OBJECT

private slots:
    void seedsDefaultsOnFirstRun()
    {
        QTemporaryDir dir;
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));

        QCOMPARE(config.dbName(), QStringLiteral("default"));
        QCOMPARE(config.weight(kFm24, QStringLiteral("Extremely Important")), 8.0);
        QCOMPARE(config.weight(kFm24, QStringLiteral("Almost Irrelevant")), 0.2);
        QCOMPARE(config.gkWeight(kFm24, QStringLiteral("Top Importance")), 3.0);
        QCOMPARE(config.gkWeight(kFm24, QStringLiteral("Preferable")), 0.25);
        QCOMPARE(config.roleMultiplier(QStringLiteral("key")), 1.5);
        QCOMPARE(config.roleMultiplier(QStringLiteral("preferable")), 1.2);
        QCOMPARE(config.aptWeight(QStringLiteral("Star Player")), 1.0);
        QCOMPARE(config.ageThreshold(QStringLiteral("outfielder")), 20);
        QCOMPARE(config.ageThreshold(QStringLiteral("goalkeeper")), 25);
        QCOMPARE(config.selectionBonus(QStringLiteral("natural_position")), 1.0);
        QCOMPARE(config.squadManagementSetting(QStringLiteral("max_roles_per_depth_player")), 2);
        QCOMPARE(config.squadManagementSetting(QStringLiteral("min_loan_talent_score")), 45);
        QCOMPARE(config.gapAnalysisSetting(QStringLiteral("displacement_threshold")), 8.0);
        QCOMPARE(config.themeSettings().value(QStringLiteral("current_mode")),
                 QStringLiteral("night"));

        // The file must exist on disk after seeding.
        QVERIFY(QFile::exists(config.filePath()));
    }

    void startupAndRecentSaves()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("config.ini"));
        {
            AppConfig config(path);
            QVERIFY(!config.autoOpenLastDatabase()); // start screen by default
            QVERIFY(!config.databaseLastOpened(QStringLiteral("Bayern 2026")).isValid());
            config.setAutoOpenLastDatabase(true);
            config.setDbName(QStringLiteral("Bayern 2026"));
            config.touchDatabaseLastOpened(QStringLiteral("Bayern 2026"));
            config.touchDatabaseLastOpened(QStringLiteral("brighton"));
        }
        AppConfig config(path);
        QVERIFY(config.autoOpenLastDatabase());
        // Names with spaces/umlauts survive as INI keys.
        QVERIFY(config.databaseLastOpened(QStringLiteral("Bayern 2026")).isValid());
        config.renameDatabaseEntry(QStringLiteral("Bayern 2026"), QStringLiteral("München ä/1"));
        QVERIFY(!config.databaseLastOpened(QStringLiteral("Bayern 2026")).isValid());
        QVERIFY(config.databaseLastOpened(QStringLiteral("München ä/1")).isValid());
        QCOMPARE(config.dbName(), QStringLiteral("München ä/1")); // the active save follows
        config.removeDatabaseEntry(QStringLiteral("brighton"));
        QVERIFY(!config.databaseLastOpened(QStringLiteral("brighton")).isValid());
    }

    void neutralBlueMigratesToLogoPurple()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("config.ini"));
        {
            QSettings old(path, QSettings::IniFormat);
            old.setValue(QStringLiteral("ThemeSettings/theme_preset"), QStringLiteral("neutral"));
            old.setValue(QStringLiteral("ThemeSettings/night_primary_color"),
                         QStringLiteral("#3D7DFF"));
            old.setValue(QStringLiteral("ThemeSettings/day_interactive_color"),
                         QStringLiteral("#3d7dff"));
        }
        {
            AppConfig config(path);
            const auto theme = config.themeSettings();
            QCOMPARE(theme.value(QStringLiteral("night_primary_color")), QStringLiteral("#6a1bf5"));
            QCOMPARE(theme.value(QStringLiteral("day_primary_color")), QStringLiteral("#6a1bf5"));
            QCOMPARE(theme.value(QStringLiteral("day_interactive_color")),
                     QStringLiteral("#9d6bff"));
        }

        // A club preset or a custom color is the user's choice and stays.
        const QString clubPath = dir.filePath(QStringLiteral("club.ini"));
        {
            QSettings old(clubPath, QSettings::IniFormat);
            old.setValue(QStringLiteral("ThemeSettings/theme_preset"), QStringLiteral("bayern"));
            old.setValue(QStringLiteral("ThemeSettings/night_primary_color"),
                         QStringLiteral("#3d7dff"));
        }
        AppConfig club(clubPath);
        QCOMPARE(club.themeSettings().value(QStringLiteral("night_primary_color")),
                 QStringLiteral("#3d7dff"));
    }

    void aptWeightNoneReturnsDefault()
    {
        QTemporaryDir dir;
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        QCOMPARE(config.aptWeight(QStringLiteral("None"), 1.0), 1.0);
        QCOMPARE(config.aptWeight(QString(), 0.7), 0.7);
    }

    void setAndReload()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("config.ini"));
        {
            AppConfig config(path);
            config.setDbName(QStringLiteral("bayern2026"));
            config.setWeight(kFm24, QStringLiteral("Extremely Important"), 10.0);
            config.setRoleMultiplier(QStringLiteral("key"), 8.0);
            config.setAptWeight(QStringLiteral("Star Player"), 1.3);
            config.setGapAnalysisSetting(QStringLiteral("dropoff_threshold"), 6.5);
        }
        AppConfig reloaded(path);
        QCOMPARE(reloaded.dbName(), QStringLiteral("bayern2026"));
        QCOMPARE(reloaded.weight(kFm24, QStringLiteral("Extremely Important")), 10.0);
        QCOMPARE(reloaded.roleMultiplier(QStringLiteral("key")), 8.0);
        QCOMPARE(reloaded.aptWeight(QStringLiteral("Star Player")), 1.3);
        QCOMPARE(reloaded.gapAnalysisSetting(QStringLiteral("dropoff_threshold")), 6.5);
    }

    void htmlExportDirFallsBackToDefault()
    {
        QTemporaryDir dir;
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));

        // Unset -> automatic default (Documents-based, never empty).
        QCOMPARE(config.htmlExportDir(), QString());
        QCOMPARE(config.effectiveHtmlExportDir(kFm24), AppConfig::defaultHtmlExportDir(kFm24));
        QVERIFY(!config.effectiveHtmlExportDir(kFm24).isEmpty());

        // Existing configured folder wins.
        config.setHtmlExportDir(dir.path());
        QCOMPARE(config.effectiveHtmlExportDir(kFm24), dir.path());

        // A configured folder that does not exist falls back to the default.
        config.setHtmlExportDir(dir.filePath(QStringLiteral("does-not-exist")));
        QCOMPARE(config.effectiveHtmlExportDir(kFm24), AppConfig::defaultHtmlExportDir(kFm24));

        // Clearing removes the key again.
        config.setHtmlExportDir(QString());
        QCOMPARE(config.htmlExportDir(), QString());
    }

    void readsLegacyStyleIni()
    {
        // A user's tuned legacy config.ini must be readable unchanged.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("config.ini"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Database]\n"
                    "db_name = bayern2026-2-0\n"
                    "\n"
                    "[Weights]\n"
                    "extremely_important = 10.0\n"
                    "important = 4.0\n"
                    "\n"
                    "[RoleMultipliers]\n"
                    "key_multiplier = 8.0\n");
        }
        AppConfig config(path);
        QCOMPARE(config.dbName(), QStringLiteral("bayern2026-2-0"));
        QCOMPARE(config.weight(kFm24, QStringLiteral("Extremely Important")), 10.0);
        QCOMPARE(config.roleMultiplier(QStringLiteral("key")), 8.0);
        // Sections missing from the file get seeded with defaults.
        QCOMPARE(config.ageThreshold(QStringLiteral("goalkeeper")), 25);
    }

    void resetsOldGoalkeeperWeightsOnce()
    {
        // Goalkeeper weights tuned for the old six-attribute formula are
        // replaced once by the scheme-2 defaults; the outfield weights stay.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("config.ini"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Weights]\n"
                    "extremely_important = 10.0\n"
                    "\n"
                    "[GKWeights]\n"
                    "top_importance = 4.0\n"
                    "key = 8.0\n"
                    "other = 0.4\n");
        }
        {
            AppConfig config(path);
            QCOMPARE(config.gkWeight(kFm24, QStringLiteral("Top Importance")), 3.0);
            QCOMPARE(config.gkWeight(kFm24, QStringLiteral("Key")), 0.5);
            QCOMPARE(config.gkWeight(kFm24, QStringLiteral("Other")), 0.2);
            QCOMPARE(config.weight(kFm24, QStringLiteral("Extremely Important")), 10.0);
            // Tuning after the reset must survive later starts.
            config.setGkWeight(kFm24, QStringLiteral("Top Importance"), 5.0);
        }
        AppConfig reloaded(path);
        QCOMPARE(reloaded.gkWeight(kFm24, QStringLiteral("Top Importance")), 5.0);
    }

    void weightSectionsPerFmVersion()
    {
        // FM24 keeps the legacy sections so existing config.ini files stay
        // valid; later releases get their own, so tuning never leaks across.
        QCOMPARE(AppConfig::weightsSection(QStringLiteral("fm24"), false),
                 QStringLiteral("Weights"));
        QCOMPARE(AppConfig::weightsSection(QStringLiteral("fm24"), true),
                 QStringLiteral("GKWeights"));
        // Unknown ids fall back to the default version instead of inventing
        // sections.
        QCOMPARE(AppConfig::weightsSection(QStringLiteral("fm99"), false),
                 QStringLiteral("Weights"));
        QCOMPARE(AppConfig::weightsSection(QString(), true), QStringLiteral("GKWeights"));
    }

    void fmVersionRegistry()
    {
        QCOMPARE(defaultFmVersionId(), QStringLiteral("fm24"));
        // Every registry entry is complete, and the newest supported version
        // is one of them.
        bool newestListed = false;
        for (const FmDataVersion &version : fmDataVersions()) {
            QVERIFY(!version.id.isEmpty());
            QVERIFY(!version.displayName.isEmpty());
            QVERIFY(version.definitionsFileName.endsWith(QStringLiteral(".json")));
            QVERIFY(!version.exportFolderName.isEmpty());
            QCOMPARE(fmDataVersion(version.id).id, version.id);
            newestListed = newestListed || (version.supported && version.id == newestFmVersionId());
            // Each version has a full profile.
            QVERIFY(!globalStatCategories(version.id).isEmpty());
            QVERIFY(!gkStatCategories(version.id).isEmpty());
            QVERIFY(!weightDefaults(version.id).isEmpty());
            QVERIFY(!gkWeightDefaults(version.id).isEmpty());
        }
        QVERIFY(newestListed);
        // FM24 keeps the file name its users already have.
        QCOMPARE(fmDataVersion(QStringLiteral("fm24")).definitionsFileName,
                 QStringLiteral("definitions.json"));
        // Unknown ids resolve to the default version.
        QCOMPARE(fmDataVersion(QStringLiteral("fm99")).id, defaultFmVersionId());
    }
};

QTEST_APPLESS_MAIN(TestAppConfig)
#include "test_appconfig.moc"
