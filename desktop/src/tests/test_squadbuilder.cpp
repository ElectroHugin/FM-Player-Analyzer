#include "core/AppConfig.h"
#include "core/Assignment.h"
#include "core/Definitions.h"
#include "core/SquadBuilder.h"

#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>

using namespace fm;

namespace {

// Minimal cost over every way to give each row its own column (brute force).
std::int64_t bruteForceMinCost(const std::vector<std::vector<std::int64_t>> &cost)
{
    const int rows = static_cast<int>(cost.size());
    const int columns = static_cast<int>(cost.front().size());
    std::vector<int> perm(static_cast<size_t>(columns));
    std::iota(perm.begin(), perm.end(), 0);
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    do {
        std::int64_t total = 0;
        for (int r = 0; r < rows; ++r)
            total += cost[static_cast<size_t>(r)][static_cast<size_t>(perm[static_cast<size_t>(r)])];
        best = std::min(best, total);
    } while (std::next_permutation(perm.begin(), perm.end()));
    return best;
}

} // namespace

class TestSquadBuilder : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_definitions = std::make_unique<Definitions>();
        QVERIFY2(m_definitions->load(QStringLiteral(LEGACY_DIR)
                                     + QStringLiteral("/config/definitions.json")),
                 qPrintable(m_definitions->errorString()));
        m_config = std::make_unique<AppConfig>(m_dir.filePath(QStringLiteral("config.ini")));
        m_builder = std::make_unique<SquadBuilder>(*m_definitions, *m_config);
    }

    void assignmentMatchesBruteForce()
    {
        // Random rectangular problems, negative costs as in the squad builder.
        QRandomGenerator rng(20261010);
        for (int round = 0; round < 200; ++round) {
            const int rows = 1 + static_cast<int>(rng.bounded(5));
            const int columns = rows + static_cast<int>(rng.bounded(3));
            std::vector<std::vector<std::int64_t>> cost(
                static_cast<size_t>(rows), std::vector<std::int64_t>(static_cast<size_t>(columns)));
            for (auto &row : cost)
                for (auto &value : row)
                    value = -static_cast<std::int64_t>(rng.bounded(1000));
            const std::vector<int> result = Assignment::solveMinCost(cost);
            QCOMPARE(static_cast<int>(result.size()), rows);
            std::int64_t total = 0;
            std::vector<bool> used(static_cast<size_t>(columns), false);
            for (int r = 0; r < rows; ++r) {
                const int c = result[static_cast<size_t>(r)];
                QVERIFY(c >= 0 && c < columns);
                QVERIFY(!used[static_cast<size_t>(c)]); // each column at most once
                used[static_cast<size_t>(c)] = true;
                total += cost[static_cast<size_t>(r)][static_cast<size_t>(c)];
            }
            QCOMPARE(total, bruteForceMinCost(cost));
        }
    }

    void versatilePlayerStartsWhereTheBTeamLosesLeast()
    {
        // The real case behind the switch to the exact method (Wrexham,
        // 2026-10): Jafari is the better DM (73) but a greedy pick started him
        // at centre-back, so the B-team had to field a 49% teenager at DM.
        // Aguilera is pinned to centre-back by his primary role.
        struct Spec {
            const char *name;
            const char *position;
            double bpd; // 0 = no rating for the role
            double dm;
            const char *primaryRole;
        };
        const Spec specs[] = {
            {"Casas", "D (C), DM", 73, 71, ""},   {"Jafari", "D (RC), DM", 72, 73, ""},
            {"Kerche", "D (C)", 72, 0, ""},       {"Dumont", "D (RC)", 70, 0, ""},
            {"Aguilera", "D (C), DM", 68, 67, "BPD-D"},
            {"Vlcek", "D (L), DM", 0, 72, ""},    {"Tani", "DM, M (C)", 0, 71, ""},
            {"Junio", "DM, M (C)", 0, 70, ""},    {"Hughes", "D (C), DM", 43, 49, ""},
        };
        std::vector<Player> players;
        RoleRatings ratings;
        for (const Spec &spec : specs) {
            Player p;
            p.uid = QString::fromLatin1(spec.name);
            p.name = p.uid;
            p.positionRaw = QString::fromLatin1(spec.position);
            p.primaryRole = QString::fromLatin1(spec.primaryRole);
            if (spec.bpd > 0) {
                p.assignedRoles << QStringLiteral("BPD-D");
                ratings[QStringLiteral("BPD-D")].insert(p.uid, spec.bpd);
            }
            if (spec.dm > 0) {
                p.assignedRoles << QStringLiteral("DM-S");
                ratings[QStringLiteral("DM-S")].insert(p.uid, spec.dm);
            }
            players.push_back(p);
        }
        std::vector<const Player *> pool;
        for (const Player &p : players)
            pool.push_back(&p);
        const QHash<QString, QString> positions{{QStringLiteral("DCL"), QStringLiteral("BPD-D")},
                                                {QStringLiteral("DCR"), QStringLiteral("BPD-D")},
                                                {QStringLiteral("DMCL"), QStringLiteral("DM-S")},
                                                {QStringLiteral("DMCR"), QStringLiteral("DM-S")}};
        const QStringList slotOrder{QStringLiteral("DCL"), QStringLiteral("DCR"),
                                    QStringLiteral("DMCL"), QStringLiteral("DMCR")};

        const SquadResult result =
            m_builder->calculateSquadAndSurplus(pool, positions, slotOrder, ratings);
        const auto namesAt = [](const QHash<QString, XiCell> &team, const QString &a,
                                const QString &b) {
            QStringList names{team.value(a).playerUid, team.value(b).playerUid};
            names.sort();
            return names;
        };
        const QString dcl = QStringLiteral("DCL"), dcr = QStringLiteral("DCR");
        const QString dml = QStringLiteral("DMCL"), dmr = QStringLiteral("DMCR");
        // Best possible XI (290): Jafari at DM, Kerche beside Casas.
        QCOMPARE(namesAt(result.startingXi, dcl, dcr),
                 (QStringList{QStringLiteral("Casas"), QStringLiteral("Kerche")}));
        QCOMPARE(namesAt(result.startingXi, dml, dmr),
                 (QStringList{QStringLiteral("Jafari"), QStringLiteral("Vlcek")}));
        // B-team without the teenager; Aguilera only at centre-back.
        QCOMPARE(namesAt(result.bTeam, dcl, dcr),
                 (QStringList{QStringLiteral("Aguilera"), QStringLiteral("Dumont")}));
        QCOMPARE(namesAt(result.bTeam, dml, dmr),
                 (QStringList{QStringLiteral("Junio"), QStringLiteral("Tani")}));
        // Depth: Hughes is left, and the pinned Aguilera is no DM option.
        for (const DepthOption &option : result.bestDepthOptions.value(QStringLiteral("DM-S")))
            QVERIFY(option.playerUid != QStringLiteral("Aguilera"));
    }

    void xiNeverWeakenedForTheBTeam()
    {
        // Star at A (90) or B (60); others: X only at A (85), Y only at B (59).
        // XI-first: star at A? 90 + 59 = 149 vs X at A + star at B: 85 + 60 =
        // 145. The XI takes 149 even though the B-team then misses a player.
        std::vector<Player> players(3);
        RoleRatings ratings;
        const QString roleA = QStringLiteral("BPD-D"), roleB = QStringLiteral("DM-S");
        players[0].uid = QStringLiteral("star");
        players[0].positionRaw = QStringLiteral("D (C), DM");
        players[0].assignedRoles = {roleA, roleB};
        ratings[roleA].insert(QStringLiteral("star"), 90);
        ratings[roleB].insert(QStringLiteral("star"), 60);
        players[1].uid = QStringLiteral("x");
        players[1].positionRaw = QStringLiteral("D (C)");
        players[1].assignedRoles = {roleA};
        ratings[roleA].insert(QStringLiteral("x"), 85);
        players[2].uid = QStringLiteral("y");
        players[2].positionRaw = QStringLiteral("DM");
        players[2].assignedRoles = {roleB};
        ratings[roleB].insert(QStringLiteral("y"), 59);
        std::vector<const Player *> pool;
        for (Player &p : players) {
            p.name = p.uid;
            pool.push_back(&p);
        }
        const QHash<QString, QString> positions{{QStringLiteral("DCL"), roleA},
                                                {QStringLiteral("DMCL"), roleB}};
        const SquadResult result = m_builder->calculateSquadAndSurplus(
            pool, positions, {QStringLiteral("DCL"), QStringLiteral("DMCL")}, ratings);
        QCOMPARE(result.startingXi.value(QStringLiteral("DCL")).playerUid, QStringLiteral("star"));
        QCOMPARE(result.startingXi.value(QStringLiteral("DMCL")).playerUid, QStringLiteral("y"));
        QCOMPARE(result.bTeam.value(QStringLiteral("DCL")).playerUid, QStringLiteral("x"));
        QVERIFY(!result.bTeam.value(QStringLiteral("DMCL")).isFilled());
    }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<Definitions> m_definitions;
    std::unique_ptr<AppConfig> m_config;
    std::unique_ptr<SquadBuilder> m_builder;
};

QTEST_GUILESS_MAIN(TestSquadBuilder)
#include "test_squadbuilder.moc"
