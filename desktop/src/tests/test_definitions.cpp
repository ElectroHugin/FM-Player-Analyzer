#include <QJsonObject>
#include <QtTest>

#include "core/Definitions.h"

using namespace fm;

// Loads the real legacy definitions.json (tracked in git) as fixture.
static QString legacyDefinitionsPath()
{
    return QStringLiteral(LEGACY_DIR) + QStringLiteral("/config/definitions.json");
}

class TestDefinitions : public QObject
{
    Q_OBJECT

    Definitions defs;

private slots:
    void initTestCase()
    {
        QVERIFY2(defs.load(legacyDefinitionsPath()),
                 qPrintable(defs.errorString()));
    }

    void playerRolesStructure()
    {
        const auto roles = defs.playerRoles();
        QCOMPARE(roles.size(), 4);
        QVERIFY(roles.contains(QStringLiteral("Goalkeepers")));
        QVERIFY(roles.contains(QStringLiteral("Defense")));
        QVERIFY(roles.contains(QStringLiteral("Midfield")));
        QVERIFY(roles.contains(QStringLiteral("Attack")));
        QCOMPARE(roles.value(QStringLiteral("Goalkeepers"))
                     .value(QStringLiteral("GK-D")),
                 QStringLiteral("Goalkeeper (Defend)"));
    }

    void validRolesSortedAndComplete()
    {
        const QStringList roles = defs.validRoles();
        QVERIFY(roles.size() >= 60); // 65 roles shipped; user may add more
        QVERIFY(std::is_sorted(roles.begin(), roles.end()));
        QVERIFY(roles.contains(QStringLiteral("GK-D")));
        // Every valid role must have a weights entry.
        for (const QString &role : roles)
            QVERIFY2(!defs.roleWeights(role).key.isEmpty()
                         || !defs.roleWeights(role).preferable.isEmpty(),
                     qPrintable(QStringLiteral("no weights for role %1").arg(role)));
    }

    void gkRoles()
    {
        const QStringList gk = defs.gkRoles();
        QVERIFY(gk.contains(QStringLiteral("GK-D")));
        QVERIFY(gk.contains(QStringLiteral("SK-A")));
    }

    void tactics()
    {
        const QStringList tactics = defs.tacticNames();
        QVERIFY(tactics.size() >= 15);
        for (const QString &tactic : tactics) {
            const auto slotMap = defs.tacticRoles().value(tactic);
            QCOMPARE(slotMap.size(), 11); // GK + 10 outfield slots
            QVERIFY(slotMap.contains(QStringLiteral("GK")));
            // Layouts enumerate only the 10 outfield slots; GK is implicit.
            const auto layout = defs.tacticLayouts().value(tactic);
            int layoutSlotCount = 0;
            for (auto it = layout.constBegin(); it != layout.constEnd(); ++it)
                layoutSlotCount += it.value().size();
            QCOMPARE(layoutSlotCount, 10);
        }
    }

    void positionMapping()
    {
        const auto mapping = defs.positionToRoleMapping();
        QVERIFY(mapping.size() >= 14);
        QVERIFY(!mapping.value(QStringLiteral("GK")).isEmpty());
        QVERIFY(!mapping.value(QStringLiteral("D (C)")).isEmpty());
    }

    void personalityCategory()
    {
        QCOMPARE(defs.personalityCategory(QStringLiteral("Model Citizen")),
                 QStringLiteral("good"));
        QCOMPARE(defs.personalityCategory(QStringLiteral("model citizen ")),
                 QStringLiteral("good")); // case-/whitespace-insensitive
        QCOMPARE(defs.personalityCategory(QStringLiteral("Slack")), QStringLiteral("bad"));
        QCOMPARE(defs.personalityCategory(QStringLiteral("Balanced")),
                 QStringLiteral("neutral"));
        QCOMPARE(defs.personalityCategory(QString()), QString());
        QCOMPARE(defs.personalityCategory(QStringLiteral("Nonexistent")), QString());
    }

    void saveRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString copyPath = dir.filePath(QStringLiteral("definitions.json"));
        QVERIFY(QFile::copy(legacyDefinitionsPath(), copyPath));

        Definitions editable;
        QVERIFY(editable.load(copyPath));
        QVERIFY(editable.save());
        QVERIFY(!QFile::exists(copyPath + QStringLiteral(".bak"))); // removed on success

        // Reload and verify content survived.
        Definitions reloaded;
        QVERIFY(reloaded.load(copyPath));
        QCOMPARE(reloaded.validRoles(), defs.validRoles());
        QCOMPARE(reloaded.tacticNames(), defs.tacticNames());
    }

    // Backlog #30: the lookup tables are cached — they must follow every change
    // of the JSON root and survive a failed reload untouched.
    void cacheFollowsSetRoot()
    {
        Definitions edited;
        QVERIFY(edited.load(legacyDefinitionsPath()));
        QVERIFY(!edited.validRoles().contains(QStringLiteral("XX-S")));

        QJsonObject root = edited.root();
        QJsonObject roles = root.value(QLatin1String("player_roles")).toObject();
        QJsonObject midfield = roles.value(QLatin1String("Midfield")).toObject();
        midfield.insert(QStringLiteral("XX-S"), QStringLiteral("Test Role (Support)"));
        roles.insert(QStringLiteral("Midfield"), midfield);
        root.insert(QStringLiteral("player_roles"), roles);
        QJsonObject personalities = root.value(QLatin1String("personalities")).toObject();
        personalities.insert(QStringLiteral("Test Temper"), QStringLiteral("bad"));
        root.insert(QStringLiteral("personalities"), personalities);
        QJsonObject tactics = root.value(QLatin1String("tactic_roles")).toObject();
        tactics.insert(QStringLiteral("Test Tactic"),
                       QJsonObject{{QStringLiteral("MCL"), QStringLiteral("XX-S")}});
        root.insert(QStringLiteral("tactic_roles"), tactics);
        edited.setRoot(root);

        QVERIFY(edited.validRoles().contains(QStringLiteral("XX-S")));
        QCOMPARE(edited.roleDisplayMap().value(QStringLiteral("XX-S")),
                 QStringLiteral("Test Role (Support)"));
        QVERIFY(edited.naturalRoleSorter().contains(QStringLiteral("XX-S")));
        QCOMPARE(edited.personalityCategory(QStringLiteral(" test temper")), QStringLiteral("bad"));
        const auto testTactic = edited.tacticRoles().value(QStringLiteral("Test Tactic"));
        QCOMPARE(testTactic.value(QStringLiteral("MCL")), QStringLiteral("XX-S"));
        QVERIFY(edited.tacticNames().contains(QStringLiteral("Test Tactic")));
    }

    void failedLoadKeepsCache()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString broken = dir.filePath(QStringLiteral("broken.json"));
        QFile file(broken);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ not json");
        file.close();

        Definitions kept;
        QVERIFY(kept.load(legacyDefinitionsPath()));
        const QStringList before = kept.validRoles();
        QVERIFY(!kept.load(broken));
        QCOMPARE(kept.validRoles(), before);
        QCOMPARE(kept.personalityCategory(QStringLiteral("Model Citizen")), QStringLiteral("good"));
    }

    void defaultConstructedFallbacks()
    {
        const Definitions empty;
        QCOMPARE(empty.gkRoles().size(), 4); // legacy fallback list
        QVERIFY(!empty.personalities().isEmpty()); // built-in defaults
        QVERIFY(empty.validRoles().isEmpty());
    }

    void loadMissingFileFails()
    {
        Definitions missing;
        QVERIFY(!missing.load(QStringLiteral("Z:/does/not/exist.json")));
        QVERIFY(!missing.errorString().isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestDefinitions)
#include "test_definitions.moc"
