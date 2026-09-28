#include "core/AppConfig.h"
#include "core/Definitions.h"
#include "core/Player.h"
#include "core/PlayerStore.h"
#include "core/RatingsUpdater.h"
#include "core/RoleAssignment.h"
#include "core/SquadBuilder.h"

#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <vector>

using namespace fm;

// Backlog #23: a role removed from a player must stop counting everywhere — its
// last dwrs_latest row survives (history is kept), and a primary role pointing
// at it must not lock the player out of every other slot.

namespace {

QString definitionsPath()
{
    return QStringLiteral(LEGACY_DIR) + QStringLiteral("/config/definitions.json");
}

Player makePlayer(int id, const QString &uid, const QStringList &roles,
                  const QString &primaryRole = QString())
{
    Player p;
    p.id = id;
    p.uid = uid;
    p.name = uid;
    p.age = 25;
    p.positionRaw = QStringLiteral("DM, M (C)");
    p.assignedRoles = roles;
    p.primaryRole = primaryRole;
    return p;
}

const QString kCm = QStringLiteral("CM-S");
const QString kBwm = QStringLiteral("BWM-D");

} // namespace

class TestRoleRatings : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Definitions> m_definitions;
    std::unique_ptr<AppConfig> m_config;
    std::unique_ptr<SquadBuilder> m_builder;
    QTemporaryDir m_dir;

    // Starting-XI occupant of the given slot for a one-slot-per-role tactic.
    QString xiPick(const PlayerStore &store, const RoleRatings &ratings,
                   const QHash<QString, QString> &positions, const QString &slot) const
    {
        std::vector<const Player *> pool;
        for (const Player &p : store.players())
            pool.push_back(&p);
        const SquadResult squad = m_builder->calculateSquadAndSurplus(
            pool, positions, positions.keys(), ratings);
        return squad.startingXi.value(slot).playerUid;
    }

private slots:
    void initTestCase()
    {
        m_definitions = std::make_unique<Definitions>();
        QVERIFY2(m_definitions->load(definitionsPath()),
                 qPrintable(m_definitions->errorString()));
        m_config = std::make_unique<AppConfig>(m_dir.filePath(QStringLiteral("config.ini")));
        m_builder = std::make_unique<SquadBuilder>(*m_definitions, *m_config);
    }

    void ratingsKeepOnlyAssignedRoles()
    {
        PlayerStore store;
        store.reset({makePlayer(1, QStringLiteral("a"), {kCm})});
        LatestRatings latest;
        latest.insert({1, kCm}, {10.0, 70.0});
        latest.insert({1, kBwm}, {12.0, 90.0}); // role removed since
        latest.insert({2, kCm}, {9.0, 50.0});   // player unknown to the store

        const RoleRatings ratings = RatingsUpdater::roleRatingsForAssigned(store, latest);
        QCOMPARE(ratings.value(kCm).value(QStringLiteral("a")), 70.0);
        QCOMPARE(ratings.value(kCm).size(), 1);
        QVERIFY(!ratings.value(kBwm).contains(QStringLiteral("a")));
    }

    void removedRoleDoesNotWinSlot()
    {
        // "a" once played BWM-D at 95 but the role was taken away; "b" is the
        // real BWM-D at 60 and must get the slot.
        PlayerStore store;
        store.reset({makePlayer(1, QStringLiteral("a"), {kCm}),
                     makePlayer(2, QStringLiteral("b"), {kBwm})});
        LatestRatings latest;
        latest.insert({1, kCm}, {10.0, 70.0});
        latest.insert({1, kBwm}, {12.0, 95.0});
        latest.insert({2, kBwm}, {8.0, 60.0});

        const RoleRatings ratings = RatingsUpdater::roleRatingsForAssigned(store, latest);
        const QHash<QString, QString> positions = {{QStringLiteral("DMC"), kBwm}};
        QCOMPARE(xiPick(store, ratings, positions, QStringLiteral("DMC")), QStringLiteral("b"));
    }

    void stalePrimaryRoleDoesNotLockPlayerOut()
    {
        PlayerStore store;
        store.reset({makePlayer(1, QStringLiteral("a"), {kCm}, kBwm)}); // BWM-D removed
        LatestRatings latest;
        latest.insert({1, kCm}, {10.0, 70.0});

        const RoleRatings ratings = RatingsUpdater::roleRatingsForAssigned(store, latest);
        const QHash<QString, QString> positions = {{QStringLiteral("MCL"), kCm}};
        QCOMPARE(xiPick(store, ratings, positions, QStringLiteral("MCL")), QStringLiteral("a"));
    }

    void validPrimaryRoleStillPins()
    {
        // A primary role that IS assigned keeps restricting the player to it.
        PlayerStore store;
        store.reset({makePlayer(1, QStringLiteral("a"), {kCm, kBwm}, kBwm)});
        LatestRatings latest;
        latest.insert({1, kCm}, {10.0, 70.0});
        latest.insert({1, kBwm}, {12.0, 80.0});

        const RoleRatings ratings = RatingsUpdater::roleRatingsForAssigned(store, latest);
        const QHash<QString, QString> positions = {{QStringLiteral("MCL"), kCm}};
        QVERIFY(xiPick(store, ratings, positions, QStringLiteral("MCL")).isEmpty());
    }

    void clearStalePrimaryRole()
    {
        Player stale = makePlayer(1, QStringLiteral("a"), {kCm}, kBwm);
        QVERIFY(RoleAssignment::clearStalePrimaryRole(stale));
        QVERIFY(stale.primaryRole.isEmpty());

        Player valid = makePlayer(2, QStringLiteral("b"), {kCm, kBwm}, kBwm);
        QVERIFY(!RoleAssignment::clearStalePrimaryRole(valid));
        QCOMPARE(valid.primaryRole, kBwm);

        Player none = makePlayer(3, QStringLiteral("c"), {kCm});
        QVERIFY(!RoleAssignment::clearStalePrimaryRole(none));
    }
};

QTEST_APPLESS_MAIN(TestRoleRatings)
#include "test_roleratings.moc"
