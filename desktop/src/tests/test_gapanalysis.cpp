#include "core/Constants.h"
#include "core/GapAnalysis.h"
#include "core/Player.h"

#include <QtTest>

#include <vector>

using namespace fm;

// Backlog #37 / P7: gap analysis flags players pulled off their best slot
// (displacement), players on the wrong side, slots far below the team median
// (drop-off) and empty slots (always on top).

namespace {

const QString kVersion = QStringLiteral("fm24");

Player makePlayer(const QString &uid, const QString &positions, const QString &side = QString())
{
    Player p;
    p.uid = uid;
    p.name = uid;
    p.positionRaw = positions;
    p.preferredSide = side;
    return p;
}

XiCell cell(const QString &uid, double rating)
{
    XiCell c;
    c.playerUid = uid;
    c.name = uid;
    c.rating = rating;
    return c;
}

// Disabled thresholds so a test can look at one rule at a time.
constexpr double kOff = 1000.0;

} // namespace

class TestGapAnalysis : public QObject
{
    Q_OBJECT

private:
    std::vector<Player> m_players;
    QHash<QString, const Player *> m_byUid;

    void setPlayers(std::vector<Player> players)
    {
        m_players = std::move(players);
        m_byUid.clear();
        for (const Player &p : m_players)
            m_byUid.insert(p.uid, &p);
    }

private slots:
    void slotSide()
    {
        QCOMPARE(GapAnalysis::slotSide(QStringLiteral("DL")), QStringLiteral("Left"));
        QCOMPARE(GapAnalysis::slotSide(QStringLiteral("AMR")), QStringLiteral("Right"));
        QCOMPARE(GapAnalysis::slotSide(QStringLiteral("DMCL")), QStringLiteral("Left"));
        QCOMPARE(GapAnalysis::slotSide(QStringLiteral("MC")), QStringLiteral("Center"));
        QCOMPARE(GapAnalysis::slotSide(QStringLiteral("GK")), QStringLiteral("Center"));
    }

    void wellFilledTeamHasNoGaps()
    {
        setPlayers({makePlayer(QStringLiteral("a"), QStringLiteral("DM")),
                    makePlayer(QStringLiteral("b"), QStringLiteral("M (C)"))});
        const QHash<QString, QString> positions = {{QStringLiteral("DMC"), QStringLiteral("DM-S")},
                                                   {QStringLiteral("MC"), QStringLiteral("CM-S")}};
        const QStringList order = {QStringLiteral("DMC"), QStringLiteral("MC")};
        const QHash<QString, XiCell> team = {{QStringLiteral("DMC"), cell(QStringLiteral("a"), 75)},
                                             {QStringLiteral("MC"), cell(QStringLiteral("b"), 74)}};
        RoleRatings ratings;
        ratings[QStringLiteral("DM-S")].insert(QStringLiteral("a"), 75);
        ratings[QStringLiteral("CM-S")].insert(QStringLiteral("b"), 74);

        const auto gaps = GapAnalysis::analyzeTeamGaps(team, positions, order, m_byUid, ratings,
                                                       10.0, 10.0, 15.0, kVersion);
        QVERIFY(gaps.empty());
    }

    void displacementPointsAtTheBetterSlot()
    {
        // "a" is a natural DM (80) but plays MC at 50; "b" holds DMC at 75.
        setPlayers({makePlayer(QStringLiteral("a"), QStringLiteral("DM, M (C)")),
                    makePlayer(QStringLiteral("b"), QStringLiteral("DM"))});
        const QHash<QString, QString> positions = {{QStringLiteral("DMC"), QStringLiteral("DM-S")},
                                                   {QStringLiteral("MC"), QStringLiteral("CM-S")}};
        const QStringList order = {QStringLiteral("DMC"), QStringLiteral("MC")};
        const QHash<QString, XiCell> team = {{QStringLiteral("DMC"), cell(QStringLiteral("b"), 75)},
                                             {QStringLiteral("MC"), cell(QStringLiteral("a"), 50)}};
        RoleRatings ratings;
        ratings[QStringLiteral("DM-S")].insert(QStringLiteral("a"), 80);
        ratings[QStringLiteral("DM-S")].insert(QStringLiteral("b"), 75);
        ratings[QStringLiteral("CM-S")].insert(QStringLiteral("a"), 50);

        const auto gaps = GapAnalysis::analyzeTeamGaps(team, positions, order, m_byUid, ratings,
                                                       10.0, kOff, 15.0, kVersion);
        QCOMPARE(gaps.size(), size_t(1));
        const Gap &g = gaps.front();
        QCOMPARE(g.slot, QStringLiteral("MC"));
        QVERIFY(g.isDisplacement);
        QVERIFY(!g.isDropoff);
        QCOMPARE(g.bestSlot, QStringLiteral("DMC"));
        QCOMPARE(g.bestDwrs, 80.0);
        QCOMPARE(g.displacementScore, 30.0);
        QCOMPARE(g.gapScore, 30.0);
        QVERIFY(g.reason.contains(QStringLiteral("DMC")));
    }

    void dropoffAgainstTeamMedian()
    {
        setPlayers({makePlayer(QStringLiteral("a"), QStringLiteral("D (C)")),
                    makePlayer(QStringLiteral("b"), QStringLiteral("D (C)")),
                    makePlayer(QStringLiteral("c"), QStringLiteral("D (C)"))});
        const QString role = QStringLiteral("CD-D");
        const QHash<QString, QString> positions = {{QStringLiteral("DCL"), role},
                                                   {QStringLiteral("DC"), role},
                                                   {QStringLiteral("DCR"), role}};
        const QStringList order = {QStringLiteral("DCL"), QStringLiteral("DC"), QStringLiteral("DCR")};
        const QHash<QString, XiCell> team = {{QStringLiteral("DCL"), cell(QStringLiteral("a"), 80)},
                                             {QStringLiteral("DC"), cell(QStringLiteral("b"), 80)},
                                             {QStringLiteral("DCR"), cell(QStringLiteral("c"), 50)}};
        RoleRatings ratings;
        ratings[role] = {{QStringLiteral("a"), 80}, {QStringLiteral("b"), 80}, {QStringLiteral("c"), 50}};

        const auto gaps = GapAnalysis::analyzeTeamGaps(team, positions, order, m_byUid, ratings,
                                                       kOff, 20.0, 0.0, kVersion);
        QCOMPARE(gaps.size(), size_t(1));
        QCOMPARE(gaps.front().slot, QStringLiteral("DCR"));
        QVERIFY(gaps.front().isDropoff);
        QCOMPARE(gaps.front().dropoff, 30.0);
    }

    void wrongSideOnlyWhenTheRightSideExists()
    {
        // Left-footed full-back plays DR although DL is in the tactic.
        setPlayers({makePlayer(QStringLiteral("l"), QStringLiteral("D (RL)"), QStringLiteral("Left")),
                    makePlayer(QStringLiteral("x"), QStringLiteral("D (L)"))});
        const QHash<QString, QString> positions = {{QStringLiteral("DL"), QStringLiteral("FBL-S")},
                                                   {QStringLiteral("DR"), QStringLiteral("FBR-S")}};
        const QStringList order = {QStringLiteral("DL"), QStringLiteral("DR")};
        const QHash<QString, XiCell> team = {{QStringLiteral("DL"), cell(QStringLiteral("x"), 70)},
                                             {QStringLiteral("DR"), cell(QStringLiteral("l"), 70)}};
        RoleRatings ratings;
        ratings[QStringLiteral("FBL-S")].insert(QStringLiteral("x"), 70);
        ratings[QStringLiteral("FBR-S")].insert(QStringLiteral("l"), 70);

        auto gaps = GapAnalysis::analyzeTeamGaps(team, positions, order, m_byUid, ratings,
                                                 10.0, kOff, 15.0, kVersion);
        QCOMPARE(gaps.size(), size_t(1));
        QCOMPARE(gaps.front().slot, QStringLiteral("DR"));
        QVERIFY(gaps.front().wrongSide);
        QCOMPARE(gaps.front().displacementScore, 15.0);

        // A tactic without a left slot: nothing to fix, no wrong-side flag.
        const QHash<QString, QString> rightOnly = {{QStringLiteral("DR"), QStringLiteral("FBR-S")}};
        const QHash<QString, XiCell> rightTeam = {{QStringLiteral("DR"), cell(QStringLiteral("l"), 70)}};
        gaps = GapAnalysis::analyzeTeamGaps(rightTeam, rightOnly, {QStringLiteral("DR")}, m_byUid,
                                            ratings, 10.0, kOff, 15.0, kVersion);
        QVERIFY(gaps.empty());
    }

    void emptySlotsRankFirst()
    {
        setPlayers({makePlayer(QStringLiteral("a"), QStringLiteral("D (C)")),
                    makePlayer(QStringLiteral("b"), QStringLiteral("D (C)"))});
        const QString role = QStringLiteral("CD-D");
        const QHash<QString, QString> positions = {{QStringLiteral("DCL"), role},
                                                   {QStringLiteral("DC"), role},
                                                   {QStringLiteral("DCR"), role}};
        const QStringList order = {QStringLiteral("DCL"), QStringLiteral("DC"), QStringLiteral("DCR")};
        const QHash<QString, XiCell> team = {{QStringLiteral("DCL"), cell(QStringLiteral("a"), 90)},
                                             {QStringLiteral("DC"), cell(QStringLiteral("b"), 40)}};
        RoleRatings ratings;
        ratings[role] = {{QStringLiteral("a"), 90}, {QStringLiteral("b"), 40}};

        const auto gaps = GapAnalysis::analyzeTeamGaps(team, positions, order, m_byUid, ratings,
                                                       kOff, 10.0, 0.0, kVersion);
        QCOMPARE(gaps.size(), size_t(2));
        QCOMPARE(gaps[0].slot, QStringLiteral("DCR"));
        QVERIFY(gaps[0].gapScore >= 100.0);
        QCOMPARE(gaps[0].playerName, QStringLiteral("—"));
        QCOMPARE(gaps[1].slot, QStringLiteral("DC"));
    }
};

QTEST_GUILESS_MAIN(TestGapAnalysis)
#include "test_gapanalysis.moc"
