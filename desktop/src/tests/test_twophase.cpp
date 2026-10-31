#include "core/AppConfig.h"
#include "core/Definitions.h"
#include "core/MinCostFlow.h"
#include "core/Player.h"
#include "core/SquadBuilder.h"

#include <QJsonObject>

#include <functional>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <memory>
#include <numeric>
#include <vector>

using namespace fm;

// FM27 best XI with two free line-ups (fahrplan/algorithmus-zwei-aufstellungen.md):
// every player bridges one in-possession slot and one out-of-possession slot,
// the bridges are chosen by an exact minimum-cost flow.

namespace {

QJsonObject slotMap(std::initializer_list<std::pair<const char *, const char *>> entries)
{
    QJsonObject obj;
    for (const auto &[slot, role] : entries)
        obj.insert(QLatin1String(slot), QLatin1String(role));
    return obj;
}

// Roles in possession: IF, W, FB; out of possession: TW, WM, PFB.
QJsonObject root(const QJsonObject &ip, const QJsonObject &oop)
{
    QJsonObject phases;
    for (const char *r : {"IF", "W", "FB"})
        phases.insert(QLatin1String(r), QStringLiteral("ip"));
    for (const char *r : {"TW", "WM", "PFB"})
        phases.insert(QLatin1String(r), QStringLiteral("oop"));
    QJsonObject roles;
    for (auto it = phases.begin(); it != phases.end(); ++it)
        roles.insert(it.key(), it.key());
    return QJsonObject{
        {QStringLiteral("player_roles"), QJsonObject{{QStringLiteral("Attack"), roles}}},
        {QStringLiteral("role_phases"), phases},
        {QStringLiteral("tactic_roles"),
         QJsonObject{{QStringLiteral("T"),
                      QJsonObject{{QStringLiteral("ip"), ip}, {QStringLiteral("oop"), oop}}}}},
    };
}

Player makePlayer(const QString &uid, const QString &positions)
{
    Player p;
    p.uid = uid;
    p.name = uid;
    p.age = 25;
    p.positionRaw = positions;
    return p;
}

// Brute force for a bipartite assignment: best total value over all ways to
// give rows distinct columns (value <= 0 = forbidden), maximising first the
// number of rows placed, then the value.
std::pair<int, std::int64_t> bruteForce(const std::vector<std::vector<std::int64_t>> &value)
{
    const int rows = static_cast<int>(value.size());
    const int cols = static_cast<int>(value[0].size());
    std::pair<int, std::int64_t> best{0, 0};
    std::vector<int> choice(static_cast<size_t>(rows), -1);
    std::vector<bool> used(static_cast<size_t>(cols), false);
    std::function<void(int, int, std::int64_t)> rec = [&](int r, int placed, std::int64_t sum) {
        if (r == rows) {
            best = std::max(best, std::make_pair(placed, sum));
            return;
        }
        rec(r + 1, placed, sum); // row stays empty
        for (int c = 0; c < cols; ++c) {
            if (used[static_cast<size_t>(c)] || value[static_cast<size_t>(r)][static_cast<size_t>(c)] <= 0)
                continue;
            used[static_cast<size_t>(c)] = true;
            rec(r + 1, placed + 1, sum + value[static_cast<size_t>(r)][static_cast<size_t>(c)]);
            used[static_cast<size_t>(c)] = false;
        }
    };
    rec(0, 0, 0);
    return best;
}

} // namespace

class TestTwoPhase : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<AppConfig> m_config;

    TwoPhaseSquadResult solve(const Definitions &defs, const std::vector<Player> &players,
                              const RoleRatings &ratings, double ipShare) const
    {
        SquadBuilder builder(defs, *m_config);
        std::vector<const Player *> pool;
        for (const Player &p : players)
            pool.push_back(&p);
        return builder.calculateTwoPhaseSquad(pool, QStringLiteral("T"), ratings, ipShare, false);
    }

private slots:
    void initTestCase()
    {
        m_config = std::make_unique<AppConfig>(m_dir.filePath(QStringLiteral("config.ini")));
    }

    void minCostFlowMatchesBruteForce()
    {
        QRandomGenerator rng(27);
        for (int round = 0; round < 200; ++round) {
            const int rows = 1 + rng.bounded(5);
            const int cols = 1 + rng.bounded(5);
            std::vector<std::vector<std::int64_t>> value(
                static_cast<size_t>(rows), std::vector<std::int64_t>(static_cast<size_t>(cols), 0));
            for (auto &row : value) {
                for (auto &v : row)
                    v = rng.bounded(3) == 0 ? 0 : 1 + rng.bounded(100);
            }
            // Flow with a per-unit reward that dominates any value sum.
            constexpr std::int64_t kReward = 1'000'000;
            MinCostFlow flow(2 + rows + cols);
            for (int r = 0; r < rows; ++r)
                flow.addEdge(0, 2 + r, 1, 0);
            for (int c = 0; c < cols; ++c)
                flow.addEdge(2 + rows + c, 1, 1, 0);
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    const std::int64_t v = value[static_cast<size_t>(r)][static_cast<size_t>(c)];
                    if (v > 0)
                        flow.addEdge(2 + r, 2 + rows + c, 1, -(kReward + v));
                }
            }
            const auto [units, cost] = flow.solve(0, 1, rows);
            const auto [placed, sum] = bruteForce(value);
            QCOMPARE(units, placed);
            QCOMPARE(-cost - kReward * units, sum);
        }
    }

    void tradesBallSkillForDefensiveWork()
    {
        // The worked example of the algorithm note: Gnabry loses 10 in
        // possession against Olise but gains 20 out of it, so he starts at 50 %.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AMR", "IF"}, {"DR", "FB"}}),
                          slotMap({{"AMR", "TW"}, {"DR", "PFB"}})));
        QVERIFY(defs.isTwoPhaseTactic(QStringLiteral("T")));
        const std::vector<Player> players = {makePlayer(QStringLiteral("olise"), QStringLiteral("AM (R)")),
                                             makePlayer(QStringLiteral("gnabry"), QStringLiteral("AM (R)")),
                                             makePlayer(QStringLiteral("boey"), QStringLiteral("D (R)"))};
        RoleRatings ratings;
        ratings[QStringLiteral("IF")] = {{QStringLiteral("olise"), 90}, {QStringLiteral("gnabry"), 80}};
        ratings[QStringLiteral("TW")] = {{QStringLiteral("olise"), 55}, {QStringLiteral("gnabry"), 75}};
        ratings[QStringLiteral("FB")] = {{QStringLiteral("boey"), 70}};
        ratings[QStringLiteral("PFB")] = {{QStringLiteral("boey"), 72}};

        auto r = solve(defs, players, ratings, 0.5);
        QCOMPARE(r.startingIp.value(QStringLiteral("AMR")).playerUid, QStringLiteral("gnabry"));
        QCOMPARE(r.startingOop.value(QStringLiteral("AMR")).playerUid, QStringLiteral("gnabry"));
        QCOMPARE(r.startingIp.value(QStringLiteral("DR")).playerUid, QStringLiteral("boey"));
        QCOMPARE(r.valueOfUid.value(QStringLiteral("gnabry")), 77.5);
        QCOMPARE(r.startingIp.value(QStringLiteral("AMR")).rating, 80.0);
        QCOMPARE(r.startingOop.value(QStringLiteral("AMR")).rating, 75.0);
        // Olise drops to the B-team: he fits AMR in both line-ups, DR stays empty.
        QCOMPARE(r.bTeamIp.value(QStringLiteral("AMR")).playerUid, QStringLiteral("olise"));
        QVERIFY(!r.bTeamIp.value(QStringLiteral("DR")).isFilled());

        // More weight on the ball flips it to Olise (79.5 vs 78.5).
        r = solve(defs, players, ratings, 0.7);
        QCOMPARE(r.startingIp.value(QStringLiteral("AMR")).playerUid, QStringLiteral("olise"));
    }

    void playersMayCrossSidesBetweenLineUps()
    {
        // In possession two wingers, out of possession two wide midfielders.
        // "x" can only play AM (L) and M (R), "y" only AM (R) and M (L): the
        // only full solution crosses both of them over.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AML", "W"}, {"AMR", "W"}}),
                          slotMap({{"ML", "WM"}, {"MR", "WM"}})));
        const std::vector<Player> players = {makePlayer(QStringLiteral("x"), QStringLiteral("AM (L), M (R)")),
                                             makePlayer(QStringLiteral("y"), QStringLiteral("AM (R), M (L)"))};
        RoleRatings ratings;
        ratings[QStringLiteral("W")] = {{QStringLiteral("x"), 70}, {QStringLiteral("y"), 70}};
        ratings[QStringLiteral("WM")] = {{QStringLiteral("x"), 60}, {QStringLiteral("y"), 60}};

        const auto r = solve(defs, players, ratings, 0.5);
        QCOMPARE(r.slotsOfUid.value(QStringLiteral("x")),
                 qMakePair(QStringLiteral("AML"), QStringLiteral("MR")));
        QCOMPARE(r.slotsOfUid.value(QStringLiteral("y")),
                 qMakePair(QStringLiteral("AMR"), QStringLiteral("ML")));
    }

    void playerNeedsAPositionInBothLineUps()
    {
        // Olise has no D (R): even as the only right-sided attacker he cannot be
        // the out-of-possession full-back, so the team stays one short.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AMR", "IF"}, {"DR", "FB"}}),
                          slotMap({{"DR", "PFB"}, {"MR", "WM"}})));
        const std::vector<Player> players = {makePlayer(QStringLiteral("olise"), QStringLiteral("AM (R)")),
                                             makePlayer(QStringLiteral("boey"), QStringLiteral("D (R)"))};
        RoleRatings ratings;
        ratings[QStringLiteral("IF")] = {{QStringLiteral("olise"), 95}};
        ratings[QStringLiteral("PFB")] = {{QStringLiteral("olise"), 99}, {QStringLiteral("boey"), 72}};
        ratings[QStringLiteral("FB")] = {{QStringLiteral("boey"), 70}};

        const auto r = solve(defs, players, ratings, 0.5);
        QVERIFY(!r.slotsOfUid.contains(QStringLiteral("olise")));
        QCOMPARE(r.slotsOfUid.value(QStringLiteral("boey")),
                 qMakePair(QStringLiteral("DR"), QStringLiteral("DR")));
        QCOMPARE(r.depthPool.size(), size_t(1));
    }

    void moreSlotsFilledBeatsHigherValue()
    {
        // "star" is worth most as ML, but then "a" (only M (L) out of
        // possession) cannot play; placing both players wins over the value.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AML", "W"}, {"AMR", "W"}}),
                          slotMap({{"ML", "WM"}, {"MR", "WM"}})));
        const std::vector<Player> players = {
            makePlayer(QStringLiteral("star"), QStringLiteral("AM (L), M (RL)")),
            makePlayer(QStringLiteral("a"), QStringLiteral("AM (R), M (L)"))};
        RoleRatings ratings;
        ratings[QStringLiteral("W")] = {{QStringLiteral("star"), 99}, {QStringLiteral("a"), 10}};
        ratings[QStringLiteral("WM")] = {{QStringLiteral("star"), 99}, {QStringLiteral("a"), 10}};
        const auto r = solve(defs, players, ratings, 0.5);
        QCOMPARE(r.startingOop.value(QStringLiteral("MR")).playerUid, QStringLiteral("star"));
        QCOMPARE(r.startingOop.value(QStringLiteral("ML")).playerUid, QStringLiteral("a"));
        QCOMPARE(r.startingIp.value(QStringLiteral("AML")).playerUid, QStringLiteral("star"));
        QCOMPARE(r.startingIp.value(QStringLiteral("AMR")).playerUid, QStringLiteral("a"));
    }

    void primaryRolePinsItsPhase()
    {
        // Gnabry is pinned to IF: he may still play any out-of-possession role,
        // but in possession only IF — which this tactic does not have.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AMR", "W"}}), slotMap({{"MR", "WM"}})));
        std::vector<Player> players = {makePlayer(QStringLiteral("gnabry"), QStringLiteral("AM (R), M (R)")),
                                       makePlayer(QStringLiteral("sane"), QStringLiteral("AM (R), M (R)"))};
        players[0].assignedRoles = {QStringLiteral("IF"), QStringLiteral("W"), QStringLiteral("WM")};
        players[0].primaryRole = QStringLiteral("IF");
        RoleRatings ratings;
        ratings[QStringLiteral("W")] = {{QStringLiteral("gnabry"), 90}, {QStringLiteral("sane"), 60}};
        ratings[QStringLiteral("WM")] = {{QStringLiteral("gnabry"), 90}, {QStringLiteral("sane"), 60}};
        const auto r = solve(defs, players, ratings, 0.5);
        QCOMPARE(r.startingIp.value(QStringLiteral("AMR")).playerUid, QStringLiteral("sane"));
    }
};

QTEST_GUILESS_MAIN(TestTwoPhase)
#include "test_twophase.moc"
