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

    void patchEqualsFullRebuild()
    {
        // Backlog #11: the incremental cache patch after a targeted save must
        // give exactly what a full rebuild gives (empty inner hashes aside).
        PlayerStore store;
        store.reset({makePlayer(1, QStringLiteral("a"), {kCm, kBwm}),
                     makePlayer(2, QStringLiteral("b"), {kCm})});
        LatestRatings latest;
        latest.insert({1, kCm}, {10.0, 70.0});
        latest.insert({1, kBwm}, {12.0, 80.0});
        latest.insert({2, kCm}, {9.0, 60.0});
        latest.insert({2, kBwm}, {9.0, 55.0}); // not assigned to b (yet)
        RoleRatings ratings = RatingsUpdater::roleRatingsForAssigned(store, latest);

        const auto withoutEmpty = [](RoleRatings r) {
            r.removeIf([](const auto &it) { return it.value().isEmpty(); });
            return r;
        };
        const auto change = [&](int row, auto mutate) {
            Player p = store.at(row);
            const QString previousUid = p.uid;
            mutate(p);
            store.replace(row, p);
            RatingsUpdater::patchRoleRatings(ratings, store.at(row), previousUid, latest);
            QCOMPARE(withoutEmpty(ratings),
                     withoutEmpty(RatingsUpdater::roleRatingsForAssigned(store, latest)));
        };
        change(0, [&](Player &p) { p.assignedRoles = {kCm}; });              // role removed
        change(1, [&](Player &p) { p.assignedRoles = {kCm, kBwm}; });        // role added
        change(0, [&](Player &p) { p.uid = QStringLiteral("r-a"); });         // uid renamed
        change(1, [&](Player &p) { p.club = QStringLiteral("Somewhere"); }); // unrelated field
        QCOMPARE(store.rowByUid(QStringLiteral("r-a")), 0);
        QCOMPARE(store.rowByUid(QStringLiteral("a")), -1);
    }

    void missingRoleAdditionsAreAdditive()
    {
        // Backlog #34: the Assign Roles button and the import share this rule —
        // top up missing position defaults, never drop existing/manual roles.
        RoleAssignment::DefaultRoles defaults(*m_definitions);
        const QStringList dc = defaults.forPosition(QStringLiteral("D (C)"));
        QVERIFY(dc.size() >= 2);
        const QString manual = QStringLiteral("W-S"); // not a D (C) default
        QVERIFY(!dc.contains(manual));

        std::vector<Player> players(4);
        players[0].positionRaw = QStringLiteral("D (C)");              // no roles yet
        players[1].positionRaw = QStringLiteral("D (C)");              // complete + manual
        players[1].assignedRoles = dc;
        players[1].assignedRoles << manual;
        players[2].positionRaw = QStringLiteral("D (C)");              // partial + manual
        players[2].assignedRoles = {dc.first(), manual};
        players[3].positionRaw = QStringLiteral("unknown");            // no defaults at all

        const auto additions = RoleAssignment::missingRoleAdditions(players, *m_definitions);
        QCOMPARE(static_cast<int>(additions.size()), 2);
        QCOMPARE(additions[0].first, 0);
        QCOMPARE(additions[0].second, dc);
        QCOMPARE(additions[1].first, 2);
        QVERIFY(additions[1].second.contains(manual)); // manual role kept
        for (const QString &role : dc)
            QVERIFY(additions[1].second.contains(role));
        QVERIFY(std::is_sorted(additions[1].second.cbegin(), additions[1].second.cend()));
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
