#include "core/AppConfig.h"
#include "core/Definitions.h"
#include "core/Player.h"
#include "core/SquadBuilder.h"
#include "core/TacticExplorer.h"

#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <vector>

using namespace fm;

// Backlog #37 / P7: Tactic Explorer metrics (coverage, depth, medians) and the
// coverage-first ranking across tactics, on a hand-built squad.

namespace {

// First tactic of the legacy definitions.json: 4-2-4 with two DMs.
const QString kTacticA = QStringLiteral("4-2-4 EF 424 IF HP");
// Needs an AM (C) the squad below does not have.
const QString kTacticB = QStringLiteral("4-2-3-1 Highway Star");

struct Spec {
    const char *uid;
    const char *position;
    const char *role;
    double rating;
};

// One player per slot of kTacticA, rated only for that slot's role.
const Spec kSquad[] = {
    {"gk", "GK", "SK-D", 70},       {"dl", "D (L)", "FBL-A", 60},
    {"dcl", "D (C)", "BPD-D", 72},  {"dcr", "D (C)", "BPD-D", 68},
    {"dr", "D (R)", "FBR-A", 65},   {"dm1", "DM", "DM-S", 75},
    {"dm2", "DM", "DM-S", 71},      {"aml", "AM (L)", "IFL-S", 80},
    {"amr", "AM (R)", "IFR-A", 78}, {"stl", "ST (C)", "AF-A", 82},
    {"str", "ST (C)", "PF-S", 66},
};

} // namespace

class TestTacticExplorer : public QObject
{
    Q_OBJECT

private:
    Definitions m_defs;
    QTemporaryDir m_dir;
    std::unique_ptr<AppConfig> m_config;
    std::unique_ptr<SquadBuilder> m_builder;
    std::unique_ptr<TacticExplorer> m_explorer;

    std::vector<Player> m_players;
    RoleRatings m_ratings;

    std::vector<const Player *> pool(const QString &without = QString()) const
    {
        std::vector<const Player *> result;
        for (const Player &p : m_players) {
            if (p.uid != without)
                result.push_back(&p);
        }
        return result;
    }

private slots:
    void initTestCase()
    {
        QVERIFY2(m_defs.load(QStringLiteral(LEGACY_DIR) + QStringLiteral("/config/definitions.json")),
                 qPrintable(m_defs.errorString()));
        QVERIFY(!m_defs.tacticRoles().value(kTacticA).isEmpty());
        QVERIFY(!m_defs.tacticRoles().value(kTacticB).isEmpty());
        m_config = std::make_unique<AppConfig>(m_dir.filePath(QStringLiteral("config.ini")));
        m_builder = std::make_unique<SquadBuilder>(m_defs, *m_config);
        m_explorer = std::make_unique<TacticExplorer>(m_defs, *m_builder);

        int id = 1;
        for (const Spec &s : kSquad) {
            Player p;
            p.id = id++;
            p.uid = QString::fromLatin1(s.uid);
            p.name = p.uid;
            p.age = 25;
            p.positionRaw = QString::fromLatin1(s.position);
            p.assignedRoles = {QString::fromLatin1(s.role)};
            m_players.push_back(p);
            m_ratings[QString::fromLatin1(s.role)].insert(p.uid, s.rating);
        }
    }

    void fullSquadCoversEverySlot()
    {
        const TacticMetrics m = m_explorer->analyzeTactic(pool(), kTacticA, m_ratings, false);
        QCOMPARE(m.totalSlots, 11);
        QCOMPARE(m.filledSlots, 11);
        QVERIFY(m.emptySlots.isEmpty());
        QVERIFY(m.uncoverableSlots.isEmpty());

        // Both centre-backs fit both DC slots, both DMs both DM slots; every
        // other slot has exactly one candidate.
        QCOMPARE(m.eligibleCounts.value(QStringLiteral("DCL")), 2);
        QCOMPARE(m.eligibleCounts.value(QStringLiteral("DMCR")), 2);
        QCOMPARE(m.eligibleCounts.value(QStringLiteral("GK")), 1);
        QCOMPARE(m.thinSlots.size(), 7);
        QCOMPARE(m.avgDepth, 1.36); // 15 / 11

        // Sorted ratings: 60 65 66 68 70 71 72 75 78 80 82 -> median 71.
        QCOMPARE(m.overallMedian, 71.0);
        QCOMPARE(m.overallMean, 71.5); // 787 / 11 = 71.55 -> 71.5
        QCOMPARE(m.perStratum.value(QStringLiteral("Goalkeeper")).n, 1);
        QCOMPARE(m.perStratum.value(QStringLiteral("Defense")).n, 4);
        QCOMPARE(m.perStratum.value(QStringLiteral("Strikers")).median, 74.0);
    }

    void missingKeeperIsUncoverable()
    {
        const TacticMetrics m =
            m_explorer->analyzeTactic(pool(QStringLiteral("gk")), kTacticA, m_ratings, false);
        QCOMPARE(m.filledSlots, 10);
        QCOMPARE(m.emptySlots, QStringList{QStringLiteral("GK")});
        QVERIFY(m.uncoverableSlots.contains(QStringLiteral("GK")));
        QCOMPARE(m.eligibleCounts.value(QStringLiteral("GK")), 0);
    }

    void emptyPoolHasNoMedian()
    {
        const TacticMetrics m = m_explorer->analyzeTactic({}, kTacticA, m_ratings, false);
        QCOMPARE(m.filledSlots, 0);
        QCOMPARE(m.overallMedian, -1.0);
        QCOMPARE(m.uncoverableSlots.size(), 11);
        QCOMPARE(m.avgDepth, 0.0);
    }

    void rankingIsCoverageFirst()
    {
        const auto results =
            m_explorer->analyzeAllTactics(pool(), m_ratings, {kTacticB, kTacticA}, false);
        QCOMPARE(results.size(), size_t(2));
        QCOMPARE(results[0].tactic, kTacticA);
        QCOMPARE(results[0].filledSlots, 11);
        QVERIFY(results[1].filledSlots < 11);
    }

    void unknownTacticsAreSkipped()
    {
        const auto results = m_explorer->analyzeAllTactics(
            pool(), m_ratings, {QStringLiteral("does not exist"), kTacticA}, false);
        QCOMPARE(results.size(), size_t(1));
        QCOMPARE(results[0].tactic, kTacticA);
    }
};

QTEST_GUILESS_MAIN(TestTacticExplorer)
#include "test_tacticexplorer.moc"
