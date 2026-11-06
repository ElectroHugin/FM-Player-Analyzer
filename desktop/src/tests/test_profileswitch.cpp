// End-to-end check of the save profiles through AppContext, the way the app
// opens and switches saves: update from 2.0.4 (shared config.ini and
// definitions file), new saves, switching A -> B -> A, restart, copying a
// profile. Runs in a temporary data folder (QStandardPaths test mode).
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>

#include "AppContext.h"
#include "core/ProfileStore.h"

using namespace fm;

namespace {

const QString kFm24 = QStringLiteral("fm24");
const QString kTop = QStringLiteral("Extremely Important");
const QString kOwnTactic = QStringLiteral("Edmunds 4-4-2");

QJsonObject bundledFm24()
{
    QFile file(QStringLiteral(":/defaults/definitions.json"));
    file.open(QIODevice::ReadOnly);
    return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonObject withTactic(QJsonObject root, const QString &name)
{
    QJsonObject tactics = root.value(QStringLiteral("tactic_roles")).toObject();
    tactics.insert(name, tactics.begin().value());
    root.insert(QStringLiteral("tactic_roles"), tactics);
    QJsonObject layouts = root.value(QStringLiteral("tactic_layouts")).toObject();
    if (!layouts.isEmpty()) {
        layouts.insert(name, layouts.begin().value());
        root.insert(QStringLiteral("tactic_layouts"), layouts);
    }
    return root;
}

bool hasTactic(AppContext &context, const QString &name)
{
    return context.definitions().root().value(QStringLiteral("tactic_roles")).toObject().contains(
        name);
}

} // namespace

class TestProfileSwitch : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QByteArray m_logo;

    void startApp(AppContext &context)
    {
        context.paths().setDataDir(m_dir.path());
        QString error;
        QVERIFY2(context.initialize(&error, false), qPrintable(error));
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_dir.isValid());
        AppPaths paths;
        paths.setDataDir(m_dir.path());
        QVERIFY(paths.ensureDataDirInitialized());

        // --- A data folder as 2.0.4 left it ---
        { AppConfig seed(paths.configFile()); }
        QSettings ini(paths.configFile(), QSettings::IniFormat);
        ini.setValue(QStringLiteral("Weights/extremely_important"), QStringLiteral("11"));
        ini.setValue(QStringLiteral("ThemeSettings/current_mode"), QStringLiteral("day"));
        ini.setValue(QStringLiteral("ThemeSettings/day_primary_color"), QStringLiteral("#dc052d"));
        ini.sync();
        QFile defs(paths.definitionsFile(kFm24));
        QVERIFY(defs.open(QIODevice::WriteOnly));
        defs.write(QJsonDocument(withTactic(bundledFm24(), kOwnTactic)).toJson());
        defs.close();
        // The logo lived as a file in assets/.
        QImage image(8, 8, QImage::Format_ARGB32);
        image.fill(Qt::red);
        const QString logoPath = QDir(paths.assetsDir()).filePath(QStringLiteral("logo_alt.png"));
        QVERIFY(image.save(logoPath, "PNG"));
        QFile logoFile(logoPath);
        QVERIFY(logoFile.open(QIODevice::ReadOnly));
        m_logo = logoFile.readAll();
        Database old(QStringLiteral("old"));
        QVERIFY(old.open(paths.databaseFile(QStringLiteral("alt"))));
        QVERIFY(old.setSetting(QStringLiteral("club_logo_path"), logoPath));
        QVERIFY(old.setSetting(QStringLiteral("user_club"), QStringLiteral("Alt FC")));
    }

    void updateFrom204KeepsEverything()
    {
        AppContext context;
        startApp(context);
        QSignalSpy profileSpy(&context, &AppContext::profileChanged);
        QString error;
        QVERIFY2(context.openDatabase(QStringLiteral("alt"), &error), qPrintable(error));
        QCOMPARE(profileSpy.count(), 1);
        QCOMPARE(context.config().profileSeed(), QStringLiteral("config.ini"));
        QCOMPARE(context.config().weight(kFm24, kTop), 11.0);
        QCOMPARE(context.config().themeSettings().value(QStringLiteral("current_mode")),
                 QStringLiteral("day"));
        QVERIFY(hasTactic(context, kOwnTactic));
        QCOMPARE(context.profileImage(AppContext::ProfileImage::ClubLogo), m_logo);
        QCOMPARE(context.userClub(), QStringLiteral("Alt FC"));
    }

    void newSaveIsIndependent()
    {
        AppContext context;
        startApp(context);
        QString error;
        QVERIFY2(context.createDatabase(QStringLiteral("neu"), kFm24, &error), qPrintable(error));
        QCOMPARE(context.config().profileSeed(), QStringLiteral("defaults"));
        QCOMPARE(context.config().weight(kFm24, kTop), 8.0);
        QVERIFY(!hasTactic(context, kOwnTactic));
        QVERIFY(context.profileImage(AppContext::ProfileImage::ClubLogo).isEmpty());

        // Configure B: weights, colors, a tactic of its own.
        AppConfig::Transaction tx(context.config());
        context.config().setWeight(kFm24, kTop, 3.0);
        context.config().saveThemeSettings({{QStringLiteral("current_mode"), QStringLiteral("night")},
                                            {QStringLiteral("night_primary_color"),
                                             QStringLiteral("#0057b8")}});
        QVERIFY(tx.commit());
        context.definitions().setRoot(withTactic(context.definitions().root(),
                                                 QStringLiteral("Nur in Neu")));
        QVERIFY2(context.saveDefinitions(&error), qPrintable(error));
        QVERIFY(hasTactic(context, QStringLiteral("Nur in Neu")));
    }

    // Acceptance: A -> B -> A, then a restart.
    void switchAndRestart()
    {
        for (int restart = 0; restart < 2; ++restart) {
            AppContext context;
            startApp(context);
            QString error;
            for (int round = 0; round < 2; ++round) {
                QVERIFY2(context.openDatabase(QStringLiteral("alt"), &error), qPrintable(error));
                QCOMPARE(context.config().weight(kFm24, kTop), 11.0);
                QCOMPARE(context.config().themeSettings().value(QStringLiteral("current_mode")),
                         QStringLiteral("day"));
                QVERIFY(hasTactic(context, kOwnTactic));
                QVERIFY(!hasTactic(context, QStringLiteral("Nur in Neu")));
                QCOMPARE(context.profileImage(AppContext::ProfileImage::ClubLogo), m_logo);

                QVERIFY2(context.openDatabase(QStringLiteral("neu"), &error), qPrintable(error));
                QCOMPARE(context.config().weight(kFm24, kTop), 3.0);
                const auto theme = context.config().themeSettings();
                QCOMPARE(theme.value(QStringLiteral("current_mode")), QStringLiteral("night"));
                QCOMPARE(theme.value(QStringLiteral("night_primary_color")),
                         QStringLiteral("#0057b8"));
                QVERIFY(!hasTactic(context, kOwnTactic));
                QVERIFY(hasTactic(context, QStringLiteral("Nur in Neu")));
                QVERIFY(context.profileImage(AppContext::ProfileImage::ClubLogo).isEmpty());
            }
        }
        // config.ini's shared 2.0.x values were never overwritten.
        QSettings ini(QDir(m_dir.path()).filePath(QStringLiteral("config.ini")),
                      QSettings::IniFormat);
        QCOMPARE(ini.value(QStringLiteral("Weights/extremely_important")).toString(),
                 QStringLiteral("11"));
        QCOMPARE(ini.value(QStringLiteral("ThemeSettings/current_mode")).toString(),
                 QStringLiteral("day"));
        // The start screen shows the last opened save's look.
        QCOMPARE(ini.value(QStringLiteral("StartTheme/current_mode")).toString(),
                 QStringLiteral("night"));
    }

    void logoBelongsToOneSave()
    {
        AppContext context;
        startApp(context);
        QString error;
        QVERIFY(context.openDatabase(QStringLiteral("neu"), &error));
        QVERIFY(context.setProfileImage(AppContext::ProfileImage::ClubLogo, m_logo, &error));
        QVERIFY(context.openDatabase(QStringLiteral("alt"), &error));
        QVERIFY(context.setProfileImage(AppContext::ProfileImage::ClubLogo, {}, &error));
        QVERIFY(context.profileImage(AppContext::ProfileImage::ClubLogo).isEmpty());
        QVERIFY(context.openDatabase(QStringLiteral("neu"), &error));
        QCOMPARE(context.profileImage(AppContext::ProfileImage::ClubLogo), m_logo);
        // Removing it in "alt" does not bring the old file back on reopen.
        QVERIFY(context.openDatabase(QStringLiteral("alt"), &error));
        QVERIFY(context.profileImage(AppContext::ProfileImage::ClubLogo).isEmpty());
        QVERIFY(context.setProfileImage(AppContext::ProfileImage::ClubLogo, m_logo, &error));
    }

    void newSaveCopiesAnotherProfile()
    {
        AppContext context;
        startApp(context);
        QString error;
        QVERIFY2(context.createDatabase(QStringLiteral("kopie"), kFm24, QStringLiteral("neu"),
                                        &error),
                 qPrintable(error));
        QCOMPARE(context.config().profileSeed(), QStringLiteral("copy:neu"));
        QCOMPARE(context.config().weight(kFm24, kTop), 3.0);
        QVERIFY(hasTactic(context, QStringLiteral("Nur in Neu")));
        QCOMPARE(context.profileImage(AppContext::ProfileImage::ClubLogo), m_logo);
        QCOMPARE(context.userClub(), QString()); // club settings are not copied

        // A copy is a copy: changing it leaves the source alone.
        QVERIFY(context.config().setWeight(kFm24, kTop, 5.0));
        QVERIFY(context.openDatabase(QStringLiteral("neu"), &error));
        QCOMPARE(context.config().weight(kFm24, kTop), 3.0);
    }

    void copyFromSaveLastUsedWith204()
    {
        // A 2.0.x save that 2.1.0 never opened: copied without touching it.
        AppPaths paths;
        {
            Database other(QStringLiteral("other"));
            QVERIFY(other.open(paths.databaseFile(QStringLiteral("alt2"))));
        }
        AppContext context;
        startApp(context);
        QString error;
        QVERIFY2(context.createDatabase(QStringLiteral("von-alt2"), kFm24, QStringLiteral("alt2"),
                                        &error),
                 qPrintable(error));
        QCOMPARE(context.config().weight(kFm24, kTop), 11.0);
        QVERIFY(hasTactic(context, kOwnTactic));
        Database other(QStringLiteral("other2"));
        QVERIFY(other.open(paths.databaseFile(QStringLiteral("alt2"))));
        QVERIFY(!ProfileStore(other).isInitialized());
    }
};

QTEST_MAIN(TestProfileSwitch)
#include "test_profileswitch.moc"
