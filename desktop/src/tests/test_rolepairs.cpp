#include "core/AppConfig.h"
#include "core/Definitions.h"
#include "core/Player.h"
#include "core/RatingsUpdater.h"
#include "core/SquadBuilder.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <vector>

using namespace fm;

// Two roles per slot (FM26+, KONZEPT_ZWEI_ROLLEN.md): a slot holds a role in
// possession and one out of possession; its rating is the weighted mean of
// both, and a player needs both roles to be rated. FM24 tactics (one role per
// slot) behave exactly as before.

namespace {

QJsonObject minimalRoot()
{
    QJsonObject roles{{QStringLiteral("CB-IP"), QStringLiteral("Centre-Back")},
                      {QStringLiteral("BPCB"), QStringLiteral("Ball-Playing Centre-Back")},
                      {QStringLiteral("CB-OOP"), QStringLiteral("Centre-Back")},
                      {QStringLiteral("SCB"), QStringLiteral("Stopping Centre-Back")}};
    QJsonObject phases{{QStringLiteral("CB-IP"), QStringLiteral("ip")},
                       {QStringLiteral("BPCB"), QStringLiteral("ip")},
                       {QStringLiteral("CB-OOP"), QStringLiteral("oop")},
                       {QStringLiteral("SCB"), QStringLiteral("oop")}};
    QJsonObject pairSlot{{QStringLiteral("ip"), QStringLiteral("BPCB")},
                         {QStringLiteral("oop"), QStringLiteral("SCB")},
                         {QStringLiteral("ipSlot"), QStringLiteral("DMC")}};
    QJsonObject sameSlot{{QStringLiteral("ip"), QStringLiteral("SCB")},
                         {QStringLiteral("oop"), QStringLiteral("SCB")}};
    QJsonObject tactic{{QStringLiteral("DCL"), pairSlot},
                       {QStringLiteral("DCR"), sameSlot},
                       {QStringLiteral("DC"), QStringLiteral("CB-OOP")}};
    return QJsonObject{
        {QStringLiteral("player_roles"), QJsonObject{{QStringLiteral("Defense"), roles}}},
        {QStringLiteral("role_phases"), phases},
        {QStringLiteral("tactic_roles"), QJsonObject{{QStringLiteral("T"), tactic}}},
    };
}

Player makePlayer(const QString &uid, const QStringList &roles)
{
    Player p;
    p.uid = uid;
    p.name = uid;
    p.age = 25;
    p.positionRaw = QStringLiteral("D (C)");
    p.assignedRoles = roles;
    return p;
}

} // namespace

class TestRolePairs : public QObject
{
    Q_OBJECT

private slots:
    void pairHelpers()
    {
        QCOMPARE(Definitions::rolePair(QStringLiteral("A"), QStringLiteral("B")), QStringLiteral("A|B"));
        QCOMPARE(Definitions::rolePair(QStringLiteral("A"), QStringLiteral("A")), QStringLiteral("A"));
        QCOMPARE(Definitions::rolePair(QString(), QStringLiteral("B")), QStringLiteral("B"));
        QCOMPARE(Definitions::rolePair(QStringLiteral("A"), QString()), QStringLiteral("A"));
        QVERIFY(Definitions::isRolePair(QStringLiteral("A|B")));
        QVERIFY(!Definitions::isRolePair(QStringLiteral("CD-D")));
        QCOMPARE(Definitions::splitRolePair(QStringLiteral("A|B")),
                 qMakePair(QStringLiteral("A"), QStringLiteral("B")));
        QCOMPARE(Definitions::splitRolePair(QStringLiteral("CD-D")),
                 qMakePair(QStringLiteral("CD-D"), QStringLiteral("CD-D")));
        QCOMPARE(Definitions::pairParts(QStringLiteral("A|B")),
                 (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
    }

    void definitionsReadTwoRoleSlots()
    {
        Definitions defs;
        defs.setRoot(minimalRoot());
        const auto slotRoles = defs.tacticRoles().value(QStringLiteral("T"));
        QCOMPARE(slotRoles.value(QStringLiteral("DCL")), QStringLiteral("BPCB|SCB"));
        QCOMPARE(slotRoles.value(QStringLiteral("DCR")), QStringLiteral("SCB")); // same role twice
        QCOMPARE(slotRoles.value(QStringLiteral("DC")), QStringLiteral("CB-OOP")); // FM24 form
        QCOMPARE(defs.rolePairs(), QStringList{QStringLiteral("BPCB|SCB")});
        QCOMPARE(defs.tacticIpSlots().value(QStringLiteral("T")).value(QStringLiteral("DCL")),
                 QStringLiteral("DMC"));
        QCOMPARE(defs.roleDisplayMap().value(QStringLiteral("BPCB|SCB")),
                 QStringLiteral("Ball-Playing Centre-Back / Stopping Centre-Back"));
        QCOMPARE(defs.rolePhase(QStringLiteral("BPCB")), QStringLiteral("ip"));
        QCOMPARE(defs.rolePhase(QStringLiteral("SCB")), QStringLiteral("oop"));
        QCOMPARE(defs.rolePhase(QStringLiteral("CD-D")), QStringLiteral("both"));
        // Pair keys are not roles a player can be given.
        QVERIFY(!defs.validRoles().contains(QStringLiteral("BPCB|SCB")));
    }

    void combinedRatingNeedsBothRoles()
    {
        RoleRatings ratings;
        ratings[QStringLiteral("BPCB")] = {{QStringLiteral("a"), 80.0}, {QStringLiteral("b"), 90.0}};
        ratings[QStringLiteral("SCB")] = {{QStringLiteral("a"), 60.0}, {QStringLiteral("c"), 70.0}};
        const QStringList pairs{QStringLiteral("BPCB|SCB")};

        RatingsUpdater::addRolePairRatings(ratings, pairs, 0.5);
        QCOMPARE(ratings.value(QStringLiteral("BPCB|SCB")).size(), 1);
        QCOMPARE(ratings.value(QStringLiteral("BPCB|SCB")).value(QStringLiteral("a")), 70.0);

        RatingsUpdater::addRolePairRatings(ratings, pairs, 0.75);
        QCOMPARE(ratings.value(QStringLiteral("BPCB|SCB")).value(QStringLiteral("a")), 75.0);

        // Single-player refresh: "c" gains the in-possession role.
        ratings[QStringLiteral("BPCB")].insert(QStringLiteral("c"), 50.0);
        RatingsUpdater::addRolePairRatings(ratings, pairs, 0.5, QStringLiteral("c"));
        QCOMPARE(ratings.value(QStringLiteral("BPCB|SCB")).value(QStringLiteral("c")), 60.0);
        QCOMPARE(ratings.value(QStringLiteral("BPCB|SCB")).value(QStringLiteral("a")), 75.0);
    }

    void bestXiUsesCombinedRating()
    {
        QTemporaryDir dir;
        Definitions defs;
        defs.setRoot(minimalRoot());
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        QCOMPARE(config.ipSharePercent(), 50);
        SquadBuilder builder(defs, config);

        // "a" is the better ball-player, "b" the better stopper; combined, "b" wins.
        std::vector<Player> players = {
            makePlayer(QStringLiteral("a"), {QStringLiteral("BPCB"), QStringLiteral("SCB")}),
            makePlayer(QStringLiteral("b"), {QStringLiteral("BPCB"), QStringLiteral("SCB")}),
            makePlayer(QStringLiteral("c"), {QStringLiteral("BPCB")}), // no out-of-possession role
        };
        players[1].primaryRole = QStringLiteral("SCB"); // pinned to one half: still eligible
        RoleRatings ratings;
        ratings[QStringLiteral("BPCB")] = {{QStringLiteral("a"), 90.0}, {QStringLiteral("b"), 70.0},
                                           {QStringLiteral("c"), 99.0}};
        ratings[QStringLiteral("SCB")] = {{QStringLiteral("a"), 50.0}, {QStringLiteral("b"), 80.0}};
        RatingsUpdater::addRolePairRatings(ratings, defs.rolePairs(), config.ipSharePercent() / 100.0);

        std::vector<const Player *> pool;
        for (const Player &p : players)
            pool.push_back(&p);
        const QHash<QString, QString> positions = {
            {QStringLiteral("DC"), QStringLiteral("BPCB|SCB")}};
        const SquadResult squad =
            builder.calculateSquadAndSurplus(pool, positions, {QStringLiteral("DC")}, ratings, false);
        QCOMPARE(squad.startingXi.value(QStringLiteral("DC")).playerUid, QStringLiteral("b"));
        QCOMPARE(squad.startingXi.value(QStringLiteral("DC")).rating, 75.0);
    }
};

QTEST_GUILESS_MAIN(TestRolePairs)
#include "test_rolepairs.moc"
