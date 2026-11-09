// Save profiles (2.1.0): per-save settings inside the .db, migration of the
// settings all 2.0.x saves shared, atomic saving, definitions per save.
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSqlQuery>

#include "core/AppConfig.h"
#include "core/Database.h"
#include "core/Definitions.h"
#include "core/ProfileStore.h"

using namespace fm;

namespace {

const QString kFm24 = QStringLiteral("fm24");
const QString kTop = QStringLiteral("Extremely Important");

QString readIni(const QString &path, const QString &key)
{
    return QSettings(path, QSettings::IniFormat).value(key).toString();
}

// A 2.0.4 config.ini with tuned values (what every save used until 2.1.0).
void writeSharedConfig(const QString &path)
{
    { AppConfig seed(path); } // the defaults, as 2.0.4 seeded them
    QSettings ini(path, QSettings::IniFormat);
    ini.setValue(QStringLiteral("Weights/extremely_important"), QStringLiteral("11"));
    ini.setValue(QStringLiteral("Phases/ip_share_percent"), QStringLiteral("70"));
    ini.setValue(QStringLiteral("Import/html_export_dir"), QStringLiteral("/exports/fm24"));
    ini.setValue(QStringLiteral("ThemeSettings/current_mode"), QStringLiteral("day"));
    ini.setValue(QStringLiteral("ThemeSettings/night_primary_color"), QStringLiteral("#dc052d"));
}

QByteArray definitionsJson(int revision, const QStringList &tactics)
{
    QJsonObject roles{{QStringLiteral("Defense"),
                       QJsonObject{{QStringLiteral("CD"), QStringLiteral("Central Defender")}}}};
    QJsonObject tacticRoles;
    for (const QString &tactic : tactics)
        tacticRoles.insert(tactic, QJsonObject{{QStringLiteral("DCL"), QStringLiteral("CD")}});
    QJsonObject root{{QStringLiteral("roles"), roles},
                     {QStringLiteral("tactic_roles"), tacticRoles},
                     {QStringLiteral("data_revision"), revision}};
    return QJsonDocument(root).toJson();
}

QStringList tacticsOf(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object().value(QStringLiteral("tactic_roles")).toObject().keys();
}

} // namespace

class TestProfile : public QObject
{
    Q_OBJECT

private slots:
    void existingSaveTakesOverSharedSettings()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("config.ini"));
        writeSharedConfig(ini);

        Database db(QStringLiteral("p1"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("alt.db"))));
        AppConfig config(ini);
        QString error;
        QVERIFY2(config.attachProfile(db, AppConfig::ProfileSeed::SharedConfig, &error),
                 qPrintable(error));
        QCOMPARE(config.profileSeed(), QStringLiteral("config.ini"));
        QCOMPARE(config.weight(kFm24, kTop), 11.0);
        QCOMPARE(config.ipSharePercent(), 70);
        QCOMPARE(config.htmlExportDir(), QStringLiteral("/exports/fm24"));
        QCOMPARE(config.themeSettings().value(QStringLiteral("current_mode")),
                 QStringLiteral("day"));
        QCOMPARE(config.themeSettings().value(QStringLiteral("night_primary_color")),
                 QStringLiteral("#dc052d"));

        // Changes go to the save only; config.ini's 2.0.x sections stay as the
        // migration source for saves not opened yet.
        QVERIFY(config.setWeight(kFm24, kTop, 4.0));
        QVERIFY(config.saveThemeSettings({{QStringLiteral("current_mode"), QStringLiteral("night")}}));
        QCOMPARE(config.weight(kFm24, kTop), 4.0);
        QCOMPARE(readIni(ini, QStringLiteral("Weights/extremely_important")), QStringLiteral("11"));
        QCOMPARE(readIni(ini, QStringLiteral("ThemeSettings/current_mode")), QStringLiteral("day"));
        // The start screen remembers the last save's look.
        QCOMPARE(readIni(ini, QStringLiteral("StartTheme/current_mode")), QStringLiteral("night"));
        config.detachProfile();
        QCOMPARE(config.themeSettings().value(QStringLiteral("current_mode")),
                 QStringLiteral("night"));
        QCOMPARE(config.weight(kFm24, kTop), 11.0); // without a save: the shared values
    }

    void newSaveStartsFromDefaults()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("config.ini"));
        writeSharedConfig(ini);
        Database db(QStringLiteral("p2"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("neu.db"))));
        AppConfig config(ini);
        QVERIFY(config.attachProfile(db, AppConfig::ProfileSeed::Defaults));
        QCOMPARE(config.profileSeed(), QStringLiteral("defaults"));
        QCOMPARE(config.weight(kFm24, kTop), 8.0);
        QCOMPARE(config.ipSharePercent(), 50);
        QCOMPARE(config.htmlExportDir(), QString());
        QCOMPARE(config.themeSettings().value(QStringLiteral("current_mode")),
                 QStringLiteral("night"));
    }

    // Acceptance: two differently configured profiles keep their settings
    // after switching A -> B -> A and after a restart.
    void profilesStaySeparateAcrossSwitchAndRestart()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("config.ini"));
        const QString pathA = dir.filePath(QStringLiteral("A.db"));
        const QString pathB = dir.filePath(QStringLiteral("B.db"));

        const auto configure = [](AppConfig &config, double weight, const QString &mode,
                                  const QString &color, int ipShare) {
            AppConfig::Transaction tx(config);
            config.setWeight(kFm24, kTop, weight);
            config.setIpSharePercent(ipShare);
            config.saveThemeSettings({{QStringLiteral("current_mode"), mode},
                                      {QStringLiteral("day_primary_color"), color}});
            QVERIFY(tx.commit());
        };
        const auto expect = [](AppConfig &config, double weight, const QString &mode,
                               const QString &color, int ipShare) {
            QCOMPARE(config.weight(kFm24, kTop), weight);
            QCOMPARE(config.ipSharePercent(), ipShare);
            const auto theme = config.themeSettings();
            QCOMPARE(theme.value(QStringLiteral("current_mode")), mode);
            QCOMPARE(theme.value(QStringLiteral("day_primary_color")), color);
        };

        {
            AppConfig config(ini);
            Database a(QStringLiteral("a1"));
            Database b(QStringLiteral("b1"));
            QVERIFY(a.open(pathA) && b.open(pathB));
            QVERIFY(config.attachProfile(a, AppConfig::ProfileSeed::Defaults));
            configure(config, 10.0, QStringLiteral("day"), QStringLiteral("#dc052d"), 60);
            QVERIFY(config.attachProfile(b, AppConfig::ProfileSeed::Defaults));
            expect(config, 8.0, QStringLiteral("night"), QStringLiteral("#6a1bf5"), 50);
            configure(config, 3.0, QStringLiteral("night"), QStringLiteral("#0057b8"), 35);
            QVERIFY(config.attachProfile(a, AppConfig::ProfileSeed::Defaults));
            expect(config, 10.0, QStringLiteral("day"), QStringLiteral("#dc052d"), 60);
            QVERIFY(config.attachProfile(b, AppConfig::ProfileSeed::Defaults));
            expect(config, 3.0, QStringLiteral("night"), QStringLiteral("#0057b8"), 35);
        }
        // Restart: fresh objects, the files on disk only.
        AppConfig config(ini);
        Database a(QStringLiteral("a2"));
        Database b(QStringLiteral("b2"));
        QVERIFY(a.open(pathA) && b.open(pathB));
        QVERIFY(config.attachProfile(a, AppConfig::ProfileSeed::SharedConfig));
        expect(config, 10.0, QStringLiteral("day"), QStringLiteral("#dc052d"), 60);
        QVERIFY(config.attachProfile(b, AppConfig::ProfileSeed::SharedConfig));
        expect(config, 3.0, QStringLiteral("night"), QStringLiteral("#0057b8"), 35);
        // Seeds are only used once: both still say "defaults".
        QCOMPARE(config.profileSeed(), QStringLiteral("defaults"));
    }

    void failedSaveWritesNothing()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("p3"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("x.db"))));
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        QVERIFY(config.attachProfile(db, AppConfig::ProfileSeed::Defaults));
        QVERIFY(db.setSetting(QStringLiteral("user_club"), QStringLiteral("Alt FC")));

        // The last value of the batch cannot be written.
        QSqlQuery trigger(db.handle());
        QVERIFY(trigger.exec(QStringLiteral(
            "CREATE TRIGGER fail_write BEFORE INSERT ON profile_settings "
            "WHEN NEW.key = 'Training/technical_locked_age' "
            "BEGIN SELECT RAISE(ABORT, 'disk full (test)'); END")));
        {
            AppConfig::Transaction tx(config);
            QVERIFY(tx.isActive());
            QVERIFY(db.setSetting(QStringLiteral("user_club"), QStringLiteral("Neu FC")));
            config.setWeight(kFm24, kTop, 12.0);
            config.setTrainingSetting(QStringLiteral("technical_locked_age"), 30);
            QCOMPARE(config.weight(kFm24, kTop), 12.0); // pending values are visible
            QString error;
            QVERIFY(!tx.commit(&error));
            QVERIFY(error.contains(QStringLiteral("disk full")));
        }
        // Memory and disk: the old state, club setting included.
        QCOMPARE(config.weight(kFm24, kTop), 8.0);
        QCOMPARE(config.trainingSetting(QStringLiteral("technical_locked_age")), 27);
        QCOMPARE(db.setting(QStringLiteral("user_club")), QStringLiteral("Alt FC"));
        QVERIFY(config.attachProfile(db, AppConfig::ProfileSeed::Defaults));
        QCOMPARE(config.weight(kFm24, kTop), 8.0);

        // Without commit (e.g. the page was left): rolled back, too.
        QVERIFY(trigger.exec(QStringLiteral("DROP TRIGGER fail_write")));
        {
            AppConfig::Transaction tx(config);
            config.setWeight(kFm24, kTop, 12.0);
        }
        QCOMPARE(config.weight(kFm24, kTop), 8.0);
        {
            AppConfig::Transaction tx(config);
            config.setWeight(kFm24, kTop, 12.0);
            QVERIFY(tx.commit());
        }
        QVERIFY(config.attachProfile(db, AppConfig::ProfileSeed::Defaults));
        QCOMPARE(config.weight(kFm24, kTop), 12.0);
    }

    void unchangedValuesAreNotWritten()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("p4"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("x.db"))));
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        QVERIFY(config.attachProfile(db, AppConfig::ProfileSeed::Defaults));
        QSqlQuery q(db.handle());
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE writes (n INTEGER)")));
        QVERIFY(q.exec(QStringLiteral("CREATE TRIGGER count_writes AFTER UPDATE ON profile_settings "
                                      "BEGIN INSERT INTO writes VALUES (1); END")));
        // A mode toggle with the whole theme map = one changed row.
        auto theme = config.themeSettings();
        theme.insert(QStringLiteral("current_mode"), QStringLiteral("day"));
        QVERIFY(config.saveThemeSettings(theme));
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM writes")) && q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }

    void definitionsPerSave()
    {
        QTemporaryDir dir;
        const QByteArray shipped = definitionsJson(1, {QStringLiteral("Standard")});
        const QByteArray shared =
            definitionsJson(1, {QStringLiteral("Standard"), QStringLiteral("Meine Taktik")});

        Database oldSave(QStringLiteral("d1"));
        Database newSave(QStringLiteral("d2"));
        QVERIFY(oldSave.open(dir.filePath(QStringLiteral("alt.db"))));
        QVERIFY(newSave.open(dir.filePath(QStringLiteral("neu.db"))));
        ProfileStore oldStore(oldSave);
        ProfileStore newStore(newSave);
        QVERIFY(oldStore.ensureTables() && newStore.ensureTables());

        // 2.0.x save: its (shared) own tactics; new save: shipped only.
        ProfileStore::DefinitionsResult oldDefs;
        ProfileStore::DefinitionsResult newDefs;
        QVERIFY(oldStore.loadDefinitions(kFm24, shipped, shared, oldDefs));
        QVERIFY(newStore.loadDefinitions(kFm24, shipped, {}, newDefs));
        QVERIFY(oldDefs.seeded && newDefs.seeded);
        QVERIFY(tacticsOf(oldDefs.json).contains(QStringLiteral("Meine Taktik")));
        QVERIFY(!tacticsOf(newDefs.json).contains(QStringLiteral("Meine Taktik")));

        // An edit lands in its own save only.
        Definitions defs;
        QVERIFY(defs.loadFromJson(newDefs.json, kFm24));
        defs.setSaveHandler([&](const QByteArray &json, QString *) {
            return newStore.saveDefinitions(kFm24, json);
        });
        QJsonObject root = defs.root();
        QJsonObject tactics = root.value(QStringLiteral("tactic_roles")).toObject();
        tactics.insert(QStringLiteral("Neu 4-4-2"),
                       QJsonObject{{QStringLiteral("DCL"), QStringLiteral("CD")}});
        root.insert(QStringLiteral("tactic_roles"), tactics);
        defs.setRoot(root);
        QVERIFY(defs.save());
        QVERIFY(newStore.loadDefinitions(kFm24, shipped, {}, newDefs));
        QVERIFY(!newDefs.seeded);
        QVERIFY(tacticsOf(newDefs.json).contains(QStringLiteral("Neu 4-4-2")));
        QVERIFY(oldStore.loadDefinitions(kFm24, shipped, shared, oldDefs));
        QVERIFY(!tacticsOf(oldDefs.json).contains(QStringLiteral("Neu 4-4-2")));
        // The shared file is not read again once the save has its own copy.
        QVERIFY(oldStore.loadDefinitions(kFm24, shipped, definitionsJson(1, {}), oldDefs));
        QVERIFY(tacticsOf(oldDefs.json).contains(QStringLiteral("Meine Taktik")));
    }

    void documentsRoundTrip()
    {
        QTemporaryDir dir;
        Database db(QStringLiteral("doc"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("x.db"))));
        ProfileStore store(db);
        QVERIFY(store.ensureTables());
        QByteArray out;
        QVERIFY(!store.readDocument(ProfileStore::clubLogoDocument(), out));
        QVERIFY(store.errorString().isEmpty()); // missing is not an error

        const QByteArray png("\x89PNG fake image bytes", 21);
        QVERIFY(store.writeDocument(ProfileStore::clubLogoDocument(), png, false));
        QVERIFY(store.readDocument(ProfileStore::clubLogoDocument(), out));
        QCOMPARE(out, png);

        const QByteArray json = definitionsJson(3, {QStringLiteral("A"), QStringLiteral("B")});
        QVERIFY(store.saveDefinitions(kFm24, json));
        QVERIFY(store.readDocument(ProfileStore::definitionsDocument(kFm24), out));
        QCOMPARE(out, json);
        QVERIFY(store.storedDocumentBytes() < png.size() + json.size()); // compressed

        QVERIFY(store.removeDocument(ProfileStore::clubLogoDocument()));
        QVERIFY(!store.hasDocument(ProfileStore::clubLogoDocument()));
    }

    void copyIntoNewSave()
    {
        QTemporaryDir dir;
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        Database a(QStringLiteral("c1"));
        Database b(QStringLiteral("c2"));
        QVERIFY(a.open(dir.filePath(QStringLiteral("A.db"))));
        QVERIFY(b.open(dir.filePath(QStringLiteral("B.db"))));
        QVERIFY(config.attachProfile(a, AppConfig::ProfileSeed::Defaults));
        QVERIFY(config.setWeight(kFm24, kTop, 9.5));
        const QHash<QString, QString> values = config.profileValues();
        QVERIFY(!values.contains(ProfileStore::formatKey()));
        QVERIFY(config.attachProfile(b, AppConfig::ProfileSeed::Defaults));
        QVERIFY(config.replaceProfileValues(values, QStringLiteral("copy:A")));
        QCOMPARE(config.weight(kFm24, kTop), 9.5);
        QCOMPARE(config.profileSeed(), QStringLiteral("copy:A"));
        QVERIFY(config.attachProfile(b, AppConfig::ProfileSeed::Defaults));
        QCOMPARE(config.weight(kFm24, kTop), 9.5);
    }

    void personalityCategoriesStaySeparateByVersion()
    {
        QTemporaryDir dir;
        Database db(Database::uniqueConnectionName(QStringLiteral("personality")));
        QVERIFY(db.open(dir.filePath(QStringLiteral("profile.db"))));
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        QVERIFY(config.attachProfile(db, AppConfig::ProfileSeed::Defaults));
        ProfileStore store(db);
        for (const QString &version : {QStringLiteral("fm24"), QStringLiteral("fm27")}) {
            QJsonObject root{{QStringLiteral("personalities"), QJsonObject{
                {QStringLiteral("Professional"), version == QLatin1String("fm24")
                    ? QStringLiteral("good") : QStringLiteral("neutral")}}}};
            QVERIFY(store.saveDefinitions(version, QJsonDocument(root).toJson()));
        }
        for (const QString &version : {QStringLiteral("fm24"), QStringLiteral("fm27")}) {
            QByteArray json;
            QVERIFY(store.readDocument(ProfileStore::definitionsDocument(version), json));
            Definitions defs;
            QVERIFY(defs.loadFromJson(json, version));
            QCOMPARE(defs.personalityCategory(QStringLiteral("Professional")),
                     version == QLatin1String("fm24") ? QStringLiteral("good") : QStringLiteral("neutral"));
        }
    }

    void withoutProfileBehavesLike204()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("config.ini"));
        AppConfig config(ini);
        QVERIFY(!config.hasProfile());
        QVERIFY(config.setWeight(kFm24, kTop, 7.0));
        QCOMPARE(readIni(ini, QStringLiteral("Weights/extremely_important")), QStringLiteral("7"));
        QVERIFY(AppConfig::isProfileKey(QStringLiteral("Weights_fm27/top_importance")));
        QVERIFY(AppConfig::isProfileKey(QStringLiteral("ThemeSettings/current_mode")));
        QVERIFY(!AppConfig::isProfileKey(QStringLiteral("Database/db_name")));
        QVERIFY(!AppConfig::isProfileKey(QStringLiteral("RecentDatabases/x")));
        QVERIFY(!AppConfig::isProfileKey(QStringLiteral("StartTheme/current_mode")));
    }
};

QTEST_GUILESS_MAIN(TestProfile)
#include "test_profile.moc"
