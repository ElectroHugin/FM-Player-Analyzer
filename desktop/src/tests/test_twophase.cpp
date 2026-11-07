        // Kai is the better left player, so the analyzer puts him left; the
        // user swapped him to the right by hand. The advisor must start from
        // that line-up, and a left-only signing then replaces Lio, not Kai.
#include "core/AppConfig.h"
#include "core/Definitions.h"
#include "core/MinCostFlow.h"
#include "core/Player.h"
#include "core/SigningAdvisor.h"
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

    void squadForTacticFoldsBothLineUps()
    {
        // The pages and advisors see a two-phase tactic as one SquadResult:
        // slots without ball, rating = value over both line-ups, depth over
        // the roles of both line-ups.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AMR", "IF"}, {"DR", "FB"}}),
                          slotMap({{"AMR", "TW"}, {"DR", "PFB"}})));
        const std::vector<Player> players = {makePlayer(QStringLiteral("olise"), QStringLiteral("AM (R)")),
                                             makePlayer(QStringLiteral("gnabry"), QStringLiteral("AM (R)")),
                                             makePlayer(QStringLiteral("boey"), QStringLiteral("D (R)")),
                                             makePlayer(QStringLiteral("musiala"), QStringLiteral("AM (C)"))};
        RoleRatings ratings;
        ratings[QStringLiteral("IF")] = {{QStringLiteral("olise"), 90}, {QStringLiteral("gnabry"), 80},
                                         {QStringLiteral("musiala"), 70}};
        ratings[QStringLiteral("TW")] = {{QStringLiteral("olise"), 55}, {QStringLiteral("gnabry"), 75}};
        ratings[QStringLiteral("FB")] = {{QStringLiteral("boey"), 70}};
        ratings[QStringLiteral("PFB")] = {{QStringLiteral("boey"), 72}};

        SquadBuilder builder(defs, *m_config);
        std::vector<const Player *> pool;
        for (const Player &p : players)
            pool.push_back(&p);
        QCOMPARE(m_config->ipSharePercent(), 50);
        const SquadResult squad =
            builder.calculateSquadForTactic(pool, QStringLiteral("T"), ratings, false);
        QVERIFY(squad.twoPhase);
        QCOMPARE(squad.startingXi.value(QStringLiteral("AMR")).playerUid, QStringLiteral("gnabry"));
        QCOMPARE(squad.startingXi.value(QStringLiteral("AMR")).rating, 77.0);
        QCOMPARE(squad.valueOfUid.value(QStringLiteral("gnabry")), 77.5);
        QCOMPARE(squad.bTeam.value(QStringLiteral("AMR")).playerUid, QStringLiteral("olise"));
        // Musiala fits no slot but is the depth option for the role with ball.
        QCOMPARE(squad.bestDepthOptions.value(QStringLiteral("IF")).value(0).playerUid,
                 QStringLiteral("musiala"));
        QVERIFY(squad.coreSquadUids.contains(QStringLiteral("musiala")));
        QCOMPARE(squad.depthPool.size(), size_t(1));
    }
    void primaryRoleWithBallKeepsDepthWithoutBall()
    {
        // Review 2026-11-02 (P2): a primary role with ball pins only the phase
        // with ball. The depth options must apply the same rule as starting XI
        // and B-team: the third player stays a depth option for TW (without
        // ball) although IF (with ball) is his primary role.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AMR", "IF"}}), slotMap({{"AMR", "TW"}})));
        std::vector<Player> players = {makePlayer(QStringLiteral("a"), QStringLiteral("AM (R)")),
                                       makePlayer(QStringLiteral("b"), QStringLiteral("AM (R)")),
                                       makePlayer(QStringLiteral("c"), QStringLiteral("AM (R)"))};
        for (Player &p : players)
            p.assignedRoles = {QStringLiteral("IF"), QStringLiteral("TW")};
        RoleRatings ratings;
        ratings[QStringLiteral("IF")] = {{QStringLiteral("a"), 90}, {QStringLiteral("b"), 80},
                                         {QStringLiteral("c"), 70}};
        ratings[QStringLiteral("TW")] = {{QStringLiteral("a"), 90}, {QStringLiteral("b"), 80},
                                         {QStringLiteral("c"), 70}};

        SquadBuilder builder(defs, *m_config);
        const auto depthFor = [&](const QString &role) {
            std::vector<const Player *> pool;
            for (const Player &p : players)
                pool.push_back(&p);
            const SquadResult squad =
                builder.calculateSquadForTactic(pool, QStringLiteral("T"), ratings, false);
            return squad.bestDepthOptions.value(role);
        };

        QCOMPARE(depthFor(QStringLiteral("TW")).size(), 1);
        QCOMPARE(depthFor(QStringLiteral("TW")).first().playerUid, QStringLiteral("c"));

        players[2].primaryRole = QStringLiteral("IF");
        QCOMPARE(depthFor(QStringLiteral("TW")).size(), 1);
        QCOMPARE(depthFor(QStringLiteral("TW")).first().playerUid, QStringLiteral("c"));
        QCOMPARE(depthFor(QStringLiteral("IF")).first().playerUid, QStringLiteral("c"));

        // Within its own phase the pin still holds: pinned to TW (without
        // ball), he is no depth option for another role without ball, and a
        // role with ball stays open to him.
        Definitions defs2;
        defs2.setRoot(root(slotMap({{"AMR", "IF"}}), slotMap({{"MR", "WM"}})));
        std::vector<Player> wide = {makePlayer(QStringLiteral("a"), QStringLiteral("AM (R), M (R)")),
                                    makePlayer(QStringLiteral("b"), QStringLiteral("AM (R), M (R)")),
                                    makePlayer(QStringLiteral("c"), QStringLiteral("AM (R), M (R)"))};
        for (Player &p : wide)
            p.assignedRoles = {QStringLiteral("IF"), QStringLiteral("WM"), QStringLiteral("TW")};
        wide[2].primaryRole = QStringLiteral("TW");
        RoleRatings ratings2;
        ratings2[QStringLiteral("IF")] = ratings[QStringLiteral("IF")];
        ratings2[QStringLiteral("WM")] = ratings[QStringLiteral("TW")];
        SquadBuilder builder2(defs2, *m_config);
        std::vector<const Player *> pool2;
        for (const Player &p : wide)
            pool2.push_back(&p);
        const SquadResult squad2 =
            builder2.calculateSquadForTactic(pool2, QStringLiteral("T"), ratings2, false);
        QVERIFY(squad2.bestDepthOptions.value(QStringLiteral("WM")).isEmpty());
        QCOMPARE(squad2.bestDepthOptions.value(QStringLiteral("IF")).value(0).playerUid,
                 QStringLiteral("c"));
    }

    void manualBridgeCostsValue()
    {
        // Olise is the better inside forward, Musiala the better winger in the
        // centre. Swapping them by hand is possible, but costs value; the
        // options list it with the resulting total.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AMR", "IF"}, {"AMC", "W"}}),
                          slotMap({{"MR", "WM"}, {"ML", "WM"}})));
        const std::vector<Player> players = {
            makePlayer(QStringLiteral("olise"), QStringLiteral("AM (RC), M (RL)")),
            makePlayer(QStringLiteral("musiala"), QStringLiteral("AM (RC), M (RL)"))};
        RoleRatings ratings;
        ratings[QStringLiteral("IF")] = {{QStringLiteral("olise"), 90}, {QStringLiteral("musiala"), 70}};
        ratings[QStringLiteral("W")] = {{QStringLiteral("olise"), 60}, {QStringLiteral("musiala"), 85}};
        ratings[QStringLiteral("WM")] = {{QStringLiteral("olise"), 60}, {QStringLiteral("musiala"), 60}};
        SquadBuilder builder(defs, *m_config);
        std::vector<const Player *> pool;
        for (const Player &p : players)
            pool.push_back(&p);

        const auto best = builder.calculateTwoPhaseSquad(pool, QStringLiteral("T"), ratings, 0.5, false);
        QCOMPARE(best.startingIp.value(QStringLiteral("AMR")).playerUid, QStringLiteral("olise"));
        QCOMPARE(SquadBuilder::startingValue(best), 147.5);

        SquadBuilder::Bridges fixed;
        fixed.insert(QStringLiteral("olise"), qMakePair(QStringLiteral("AMC"), QString()));
        const auto manual =
            builder.calculateTwoPhaseSquad(pool, QStringLiteral("T"), ratings, 0.5, false, fixed);
        QCOMPARE(manual.startingIp.value(QStringLiteral("AMC")).playerUid, QStringLiteral("olise"));
        QCOMPARE(manual.startingIp.value(QStringLiteral("AMR")).playerUid, QStringLiteral("musiala"));
        QCOMPARE(SquadBuilder::startingValue(manual), 125.0);

        const auto options = builder.bridgeOptions(pool, QStringLiteral("T"), ratings, 0.5, {},
                                                   QStringLiteral("olise"), true, false);
        QCOMPARE(options.size(), 1);
        QCOMPARE(options[0].slot, QStringLiteral("AMC"));
        QCOMPARE(options[0].swapUid, QStringLiteral("musiala"));
        QCOMPARE(options[0].total, 125.0);
        // Out of possession both slots are worth the same: a free swap.
        const auto oopOptions = builder.bridgeOptions(pool, QStringLiteral("T"), ratings, 0.5, {},
                                                      QStringLiteral("olise"), false, false);
        QCOMPARE(oopOptions.size(), 1);
        QCOMPARE(oopOptions[0].total, 147.5);
    }
    void xiLimitCapsAGroupInBothLineUps()
    {
        // Three right-sided players for two places; the two best are limited
        // (at most one may start): the best of them and the third start.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AMR", "IF"}, {"AMC", "W"}}),
                          slotMap({{"MR", "WM"}, {"ML", "WM"}})));
        const std::vector<Player> players = {
            makePlayer(QStringLiteral("a"), QStringLiteral("AM (RC), M (RL)")),
            makePlayer(QStringLiteral("b"), QStringLiteral("AM (RC), M (RL)")),
            makePlayer(QStringLiteral("c"), QStringLiteral("AM (RC), M (RL)"))};
        RoleRatings ratings;
        for (const char *role : {"IF", "W", "WM"})
            ratings[QLatin1String(role)] = {{QStringLiteral("a"), 90},
                                            {QStringLiteral("b"), 85},
                                            {QStringLiteral("c"), 60}};
        SquadBuilder builder(defs, *m_config);
        std::vector<const Player *> pool;
        for (const Player &p : players)
            pool.push_back(&p);
        XiLimit limit;
        limit.uids = {QStringLiteral("a"), QStringLiteral("b")};
        limit.max = 1;
        const auto squad = builder.calculateTwoPhaseSquad(pool, QStringLiteral("T"), ratings, 0.5,
                                                          false, {}, limit);
        QSet<QString> ip, oop;
        for (const XiCell &cell : squad.startingIp)
            ip.insert(cell.playerUid);
        for (const XiCell &cell : squad.startingOop)
            oop.insert(cell.playerUid);
        QCOMPARE(ip, (QSet<QString>{QStringLiteral("a"), QStringLiteral("c")}));
        QCOMPARE(oop, ip);
        // Without the cap the two best start.
        const auto free = builder.calculateTwoPhaseSquad(pool, QStringLiteral("T"), ratings, 0.5,
                                                         false);
        QSet<QString> freeIp;
        for (const XiCell &cell : free.startingIp)
            freeIp.insert(cell.playerUid);
        QCOMPARE(freeIp, (QSet<QString>{QStringLiteral("a"), QStringLiteral("b")}));
    }

    void rolesOfBothLineUpsAreLabelledAlike()
    {
        // Every role of an FM27 tactic counts (not only the line-up without
        // ball), with ball first; every phase-split role carries its phase
        // the same way, whether or not the id has a suffix.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AMR", "IF"}, {"DR", "FB"}}),
                          slotMap({{"AMR", "TW"}, {"DR", "PFB"}})));
        const QStringList roles = defs.tacticRoleList(QStringLiteral("T"));
        QCOMPARE(roles.size(), 4);
        QVERIFY(roles.indexOf(QStringLiteral("IF")) < roles.indexOf(QStringLiteral("TW")));
        QVERIFY(roles.indexOf(QStringLiteral("FB")) < roles.indexOf(QStringLiteral("PFB")));
        QCOMPARE(defs.roleLabel(QStringLiteral("IF")), QStringLiteral("IF-IP"));
        QCOMPARE(defs.roleLabel(QStringLiteral("TW")), QStringLiteral("TW-OOP"));
        QCOMPARE(defs.roleDisplayMap().value(QStringLiteral("TW")), QStringLiteral("TW (OOP)"));
        QCOMPARE(Definitions::baseRoleAbbr(QStringLiteral("CFD-OOP")), QStringLiteral("CFD"));
        QCOMPARE(Definitions::baseRoleAbbr(QStringLiteral("CM-IP")), QStringLiteral("CM"));
        // Without phases (FM24) ids and names stay as they are.
        Definitions plain;
        QCOMPARE(plain.roleLabel(QStringLiteral("CM-S")), QStringLiteral("CM-S"));
    }

    void signingAdvisorFollowsManualBridges()
    {
        // Left: IF with ball, WM without; right the same. Kai is the better
        // left player, so the analyzer puts him left; the user swapped him to
        // the right by hand. The advisor must start from that line-up: a
        // right-footed signing who only plays right replaces Lio on the right
        // ... no, Kai stays right (bridged), so the right-only signing takes
        // Kai's place only if it is clearly better (bridge dropped for him),
        // and a left-only signing replaces Lio on the left.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AML", "IF"}, {"AMR", "W"}}),
                          slotMap({{"ML", "WM"}, {"MR", "TW"}})));
        const std::vector<Player> players = {
            makePlayer(QStringLiteral("kai"), QStringLiteral("AM (RL), M (RL)")),
            makePlayer(QStringLiteral("lio"), QStringLiteral("AM (RL), M (RL)"))};
        const std::vector<Player> market = {
            makePlayer(QStringLiteral("leftie"), QStringLiteral("AM (L), M (L)"))};
        RoleRatings ratings;
        ratings[QStringLiteral("IF")] = {{QStringLiteral("kai"), 80}, {QStringLiteral("lio"), 60},
                                         {QStringLiteral("leftie"), 85}};
        ratings[QStringLiteral("W")] = {{QStringLiteral("kai"), 70}, {QStringLiteral("lio"), 70}};
        ratings[QStringLiteral("WM")] = {{QStringLiteral("kai"), 70}, {QStringLiteral("lio"), 60},
                                         {QStringLiteral("leftie"), 85}};
        ratings[QStringLiteral("TW")] = {{QStringLiteral("kai"), 70}, {QStringLiteral("lio"), 70}};
        SquadBuilder builder(defs, *m_config);
        std::vector<const Player *> pool, marketPool;
        for (const Player &p : players)
            pool.push_back(&p);
        for (const Player &p : market)
            marketPool.push_back(&p);
        const auto positions = defs.tacticRoles().value(QStringLiteral("T"));
        const QStringList order = defs.tacticSlotOrder(QStringLiteral("T"));

        // Without a manual change Kai plays left.
        const SigningAnalysis free = SigningAdvisor::analyze(
            builder, pool, marketPool, positions, order, ratings, 12, 15, QStringLiteral("T"));
        const auto currentAt = [](const SigningAnalysis &a, const QString &slot) {
            for (const SlotNeed &need : a.needs) {
                if (need.slot == slot)
                    return need.current.playerUid;
            }
            return QString();
        };
        QCOMPARE(currentAt(free, QStringLiteral("ML")), QStringLiteral("kai"));

        // Swapped by hand: Kai right, Lio left - the advisor shows that line-up
        // and the left-only signing replaces Lio, not Kai.
        SquadBuilder::Bridges bridges;
        bridges.insert(QStringLiteral("kai"), qMakePair(QStringLiteral("AMR"), QStringLiteral("MR")));
        const SigningAnalysis manual =
            SigningAdvisor::analyze(builder, pool, marketPool, positions, order, ratings, 12, 15,
                                    QStringLiteral("T"), bridges);
        QCOMPARE(currentAt(manual, QStringLiteral("ML")), QStringLiteral("lio"));
        QCOMPARE(currentAt(manual, QStringLiteral("MR")), QStringLiteral("kai"));
        QCOMPARE(manual.needs.front().slot, QStringLiteral("ML"));
        QCOMPARE(manual.needs.front().options.size(), size_t(1));
        const SigningOption &option = manual.needs.front().options.front();
        QCOMPARE(option.player->uid, QStringLiteral("leftie"));
        QCOMPARE(option.moves.size(), size_t(1));
        QCOMPARE(option.moves.front().player->uid, QStringLiteral("lio"));

        // Kai bridged left: the left-only signing may still take his place
        // (his bridge is dropped for that simulation).
        SquadBuilder::Bridges left;
        left.insert(QStringLiteral("kai"), qMakePair(QStringLiteral("AML"), QStringLiteral("ML")));
        const SigningAnalysis displaced =
            SigningAdvisor::analyze(builder, pool, marketPool, positions, order, ratings, 12, 15,
                                    QStringLiteral("T"), left);
        QCOMPARE(currentAt(displaced, QStringLiteral("ML")), QStringLiteral("kai"));
        QCOMPARE(displaced.needs.front().slot, QStringLiteral("ML"));
        QCOMPARE(displaced.needs.front().options.size(), size_t(1));
    }

    // --- Squad depth: one extra player per missing starter (2.1.0) ---

    void replacementTakesTheFullBridge()
    {
        // Gnabry is out: Olise has AM (R), so he takes both of Gnabry's places
        // (a full bridge) and nobody else moves.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AMR", "IF"}, {"DR", "FB"}}),
                          slotMap({{"AMR", "TW"}, {"DR", "PFB"}})));
        const std::vector<Player> players = {
            makePlayer(QStringLiteral("gnabry"), QStringLiteral("AM (R)")),
            makePlayer(QStringLiteral("boey"), QStringLiteral("D (R)")),
            makePlayer(QStringLiteral("olise"), QStringLiteral("AM (R)")),
            makePlayer(QStringLiteral("keeper"), QStringLiteral("GK"))};
        RoleRatings ratings;
        ratings[QStringLiteral("IF")] = {{QStringLiteral("olise"), 90}, {QStringLiteral("gnabry"), 80}};
        ratings[QStringLiteral("TW")] = {{QStringLiteral("olise"), 55}, {QStringLiteral("gnabry"), 75}};
        ratings[QStringLiteral("FB")] = {{QStringLiteral("boey"), 70}};
        ratings[QStringLiteral("PFB")] = {{QStringLiteral("boey"), 72}};
        SquadBuilder builder(defs, *m_config);
        std::vector<const Player *> pool;
        for (const Player &p : players)
            pool.push_back(&p);

        const QList<OutageCover> covers =
            builder.replacementPlans(pool, QStringLiteral("T"), ratings, false);
        QCOMPARE(covers.size(), 2); // the two starters, in pitch order with ball
        const auto coverOf = [&](const QString &uid) {
            for (const OutageCover &c : covers) {
                if (c.outUid == uid)
                    return c;
            }
            return OutageCover();
        };
        const OutageCover gnabry = coverOf(QStringLiteral("gnabry"));
        QCOMPARE(gnabry.outSlots, qMakePair(QStringLiteral("AMR"), QStringLiteral("AMR")));
        QCOMPARE(gnabry.current, 148.5);
        QCOMPARE(gnabry.plans.size(), 1); // the keeper fits nowhere
        const ReplacementPlan &plan = gnabry.plans.front();
        QVERIFY(plan.complete);
        QCOMPARE(plan.inUid, QStringLiteral("olise"));
        QCOMPARE(plan.inSlots, gnabry.outSlots);
        QVERIFY(plan.moves.isEmpty());
        QCOMPARE(plan.inValue, 72.5);
        QCOMPARE(plan.total, 143.5);

        // Nobody else plays D (R): Boey's absence leaves the place empty.
        const OutageCover boey = coverOf(QStringLiteral("boey"));
        QCOMPARE(boey.plans.size(), 1);
        QVERIFY(!boey.plans.front().complete);
        QVERIFY(boey.plans.front().inUid.isEmpty());
        QCOMPARE(boey.plans.front().total, 77.5);
    }

    void replacementMayMoveOtherStarters()
    {
        // Left and right wingers with ball, wide midfielders without. "left"
        // is out; the only extra player plays the right side, so "both" (who
        // plays either side) moves over to the left in both line-ups.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AML", "W"}, {"AMR", "IF"}}),
                          slotMap({{"ML", "WM"}, {"MR", "TW"}})));
        const std::vector<Player> players = {
            makePlayer(QStringLiteral("left"), QStringLiteral("AM (L), M (L)")),
            makePlayer(QStringLiteral("both"), QStringLiteral("AM (RL), M (RL)")),
            makePlayer(QStringLiteral("right"), QStringLiteral("AM (R), M (R)"))};
        RoleRatings ratings;
        ratings[QStringLiteral("W")] = {{QStringLiteral("left"), 80}, {QStringLiteral("both"), 70}};
        ratings[QStringLiteral("IF")] = {{QStringLiteral("both"), 70}, {QStringLiteral("right"), 60}};
        ratings[QStringLiteral("WM")] = {{QStringLiteral("left"), 80}, {QStringLiteral("both"), 70}};
        ratings[QStringLiteral("TW")] = {{QStringLiteral("both"), 70}, {QStringLiteral("right"), 60}};
        SquadBuilder builder(defs, *m_config);
        std::vector<const Player *> pool;
        for (const Player &p : players)
            pool.push_back(&p);

        const QList<OutageCover> covers =
            builder.replacementPlans(pool, QStringLiteral("T"), ratings, false);
        QCOMPARE(covers.size(), 2);
        QCOMPARE(covers.front().outUid, QStringLiteral("left")); // AML comes first
        const ReplacementPlan &plan = covers.front().plans.front();
        QVERIFY(plan.complete);
        QCOMPARE(plan.inUid, QStringLiteral("right"));
        QCOMPARE(plan.inSlots, qMakePair(QStringLiteral("AMR"), QStringLiteral("MR")));
        QCOMPARE(plan.moves.size(), 1);
        QCOMPARE(plan.moves.front().uid, QStringLiteral("both"));
        QCOMPARE(plan.moves.front().from, qMakePair(QStringLiteral("AMR"), QStringLiteral("MR")));
        QCOMPARE(plan.moves.front().to, qMakePair(QStringLiteral("AML"), QStringLiteral("ML")));
        QCOMPARE(plan.total, 130.0);
    }

    void replacementPrefersNobodyMoving()
    {
        // Two centre-backs who play both sides equally well. When the right
        // one is out, his replacement takes his places; the left one does
        // not swap sides for nothing.
        Definitions defs;
        defs.setRoot(root(slotMap({{"DCL", "FB"}, {"DCR", "FB"}}),
                          slotMap({{"DCL", "PFB"}, {"DCR", "PFB"}})));
        const std::vector<Player> players = {
            makePlayer(QStringLiteral("l"), QStringLiteral("D (C)")),
            makePlayer(QStringLiteral("r"), QStringLiteral("D (C)")),
            makePlayer(QStringLiteral("x"), QStringLiteral("D (C)"))};
        RoleRatings ratings;
        ratings[QStringLiteral("FB")] = {{QStringLiteral("l"), 70}, {QStringLiteral("r"), 70},
                                         {QStringLiteral("x"), 50}};
        ratings[QStringLiteral("PFB")] = ratings[QStringLiteral("FB")];
        SquadBuilder builder(defs, *m_config);
        std::vector<const Player *> pool;
        for (const Player &p : players)
            pool.push_back(&p);
        const QList<OutageCover> covers =
            builder.replacementPlans(pool, QStringLiteral("T"), ratings, false);
        QCOMPARE(covers.size(), 2);
        for (const OutageCover &cover : covers) {
            const ReplacementPlan &plan = cover.plans.front();
            QCOMPARE(plan.inUid, QStringLiteral("x"));
            QCOMPARE(plan.inSlots, cover.outSlots);
            QVERIFY(plan.moves.isEmpty());
        }
    }

    void replacementNeverDropsAnotherStarter()
    {
        // Exactly one extra player: "star" could only come in if "a" made
        // room, which would leave "a" out - not allowed. Then nobody fits.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AML", "W"}, {"AMR", "W"}}),
                          slotMap({{"ML", "WM"}, {"MR", "WM"}})));
        const std::vector<Player> players = {
            makePlayer(QStringLiteral("a"), QStringLiteral("AM (L), M (L)")),
            makePlayer(QStringLiteral("b"), QStringLiteral("AM (R), M (R)")),
            makePlayer(QStringLiteral("star"), QStringLiteral("AM (L), M (L)"))};
        RoleRatings ratings;
        ratings[QStringLiteral("W")] = {{QStringLiteral("a"), 70}, {QStringLiteral("b"), 70},
                                        {QStringLiteral("star"), 60}};
        ratings[QStringLiteral("WM")] = ratings[QStringLiteral("W")];
        SquadBuilder builder(defs, *m_config);
        std::vector<const Player *> pool;
        for (const Player &p : players)
            pool.push_back(&p);

        const QList<OutageCover> covers =
            builder.replacementPlans(pool, QStringLiteral("T"), ratings, false);
        for (const OutageCover &cover : covers) {
            if (cover.outUid == QStringLiteral("b")) {
                QVERIFY(!cover.plans.front().complete);
                QVERIFY(cover.plans.front().moves.isEmpty());
            } else {
                QCOMPARE(cover.plans.front().inUid, QStringLiteral("star"));
            }
        }
    }

    void replacementPlansFollowManualBridges()
    {
        // "both" is put on the right by hand. Whoever is out, the plans keep
        // that manual bridge: "both" never moves, the extra player comes in.
        Definitions defs;
        defs.setRoot(root(slotMap({{"AML", "W"}, {"AMR", "IF"}, {"AMC", "IF"}}),
                          slotMap({{"ML", "WM"}, {"MR", "TW"}, {"MC", "WM"}})));
        const std::vector<Player> players = {
            makePlayer(QStringLiteral("left"), QStringLiteral("AM (LC), M (LC)")),
            makePlayer(QStringLiteral("both"), QStringLiteral("AM (RLC), M (RLC)")),
            makePlayer(QStringLiteral("mid"), QStringLiteral("AM (RLC), M (RLC)")),
            makePlayer(QStringLiteral("extra"), QStringLiteral("AM (RLC), M (RLC)"))};
        RoleRatings ratings;
        for (const char *role : {"W", "IF", "WM", "TW"}) {
            ratings[QLatin1String(role)] = {{QStringLiteral("left"), 80}, {QStringLiteral("both"), 70},
                                            {QStringLiteral("mid"), 75}, {QStringLiteral("extra"), 50}};
        }
        SquadBuilder builder(defs, *m_config);
        std::vector<const Player *> pool;
        for (const Player &p : players)
            pool.push_back(&p);
        SquadBuilder::Bridges fixed;
        fixed.insert(QStringLiteral("both"), qMakePair(QStringLiteral("AMR"), QStringLiteral("MR")));

        const QList<OutageCover> covers =
            builder.replacementPlans(pool, QStringLiteral("T"), ratings, false, fixed);
        QCOMPARE(covers.size(), 3);
        for (const OutageCover &cover : covers) {
            QCOMPARE(cover.plans.front().inUid, QStringLiteral("extra"));
            for (const PlaceMove &move : cover.plans.front().moves)
                QVERIFY(move.uid != QStringLiteral("both"));
        }
    }

    void replacementPlansForOneLineUp()
    {
        // FM24 tactic (one line-up): ip and oop are the same slot.
        Definitions defs;
        QJsonObject roles{{QStringLiteral("W"), QStringLiteral("W")},
                          {QStringLiteral("IF"), QStringLiteral("IF")}};
        defs.setRoot(QJsonObject{
            {QStringLiteral("player_roles"), QJsonObject{{QStringLiteral("Attack"), roles}}},
            {QStringLiteral("tactic_roles"),
             QJsonObject{{QStringLiteral("T"), slotMap({{"AML", "W"}, {"AMR", "IF"}})}}},
        });
        QVERIFY(!defs.isTwoPhaseTactic(QStringLiteral("T")));
        const std::vector<Player> players = {
            makePlayer(QStringLiteral("left"), QStringLiteral("AM (L)")),
            makePlayer(QStringLiteral("both"), QStringLiteral("AM (RL)")),
            makePlayer(QStringLiteral("right"), QStringLiteral("AM (R)"))};
        RoleRatings ratings;
        ratings[QStringLiteral("W")] = {{QStringLiteral("left"), 80}, {QStringLiteral("both"), 70}};
        ratings[QStringLiteral("IF")] = {{QStringLiteral("both"), 70}, {QStringLiteral("right"), 60}};
        SquadBuilder builder(defs, *m_config);
        std::vector<const Player *> pool;
        for (const Player &p : players)
            pool.push_back(&p);

        const QList<OutageCover> covers =
            builder.replacementPlans(pool, QStringLiteral("T"), ratings, false);
        QCOMPARE(covers.size(), 2);
        const OutageCover &left = covers.front();
        QCOMPARE(left.outUid, QStringLiteral("left"));
        QCOMPARE(left.outSlots, qMakePair(QStringLiteral("AML"), QStringLiteral("AML")));
        const ReplacementPlan &plan = left.plans.front();
        QCOMPARE(plan.inUid, QStringLiteral("right"));
        QCOMPARE(plan.inSlots, qMakePair(QStringLiteral("AMR"), QStringLiteral("AMR")));
        QCOMPARE(plan.moves.size(), 1);
        QCOMPARE(plan.moves.front().to, qMakePair(QStringLiteral("AML"), QStringLiteral("AML")));
        QCOMPARE(plan.total, 130.0);
    }
};

QTEST_GUILESS_MAIN(TestTwoPhase)
#include "test_twophase.moc"
