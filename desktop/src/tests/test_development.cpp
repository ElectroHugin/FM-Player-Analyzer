#include "core/Development.h"

#include <QtTest>

#include <cmath>

using namespace fm;

namespace {

DwrsEntry row(int playerId, const QString &role, double value, const QString &ts)
{
    return {playerId, role, 0.0, value, ts};
}

QDateTime at(const QString &ts)
{
    return QDateTime::fromString(ts, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace

class TestDevelopment : public QObject
{
    Q_OBJECT

private slots:
    void groupsUploadsMinutesApart()
    {
        // Real pattern: squad, shortlist and full export of one game week
        // within five minutes, then the next week two days later.
        const QList<QDateTime> times = {
            at(QStringLiteral("2026-07-05 21:13:27")), at(QStringLiteral("2026-07-05 21:14:39")),
            at(QStringLiteral("2026-07-05 21:15:46")), at(QStringLiteral("2026-07-05 21:18:05")),
            at(QStringLiteral("2026-07-07 17:28:57")), at(QStringLiteral("2026-07-07 17:39:30")),
        };
        const auto sessions = Development::groupSessions(times);
        QCOMPARE(sessions.size(), 3);
        QCOMPARE(sessions[0].first, times[0]);
        QCOMPARE(sessions[0].last, times[3]);
        QCOMPARE(sessions[1].first, times[4]); // 10:33 apart: a new snapshot
        QCOMPARE(sessions[2].first, times[5]);
        QVERIFY(Development::groupSessions({}).isEmpty());
    }

    void carriesValuesForwardOnAnEvenAxis()
    {
        // Player 1 changes in snapshots 0, 2 and 3; player 2 appears in 1.
        const std::vector<DwrsEntry> history = {
            row(1, QStringLiteral("AF-A"), 60, QStringLiteral("2026-09-01 20:00:00")),
            row(1, QStringLiteral("AF-A"), 64, QStringLiteral("2026-09-03 20:00:00")),
            row(1, QStringLiteral("AF-A"), 63, QStringLiteral("2026-09-04 20:00:00")),
            row(2, QStringLiteral("AF-A"), 70, QStringLiteral("2026-09-02 20:00:00")),
        };
        const auto timeline = Development::buildTimeline(history, {});
        QCOMPARE(timeline.size(), 4);

        const Development::Track *first = timeline.track(1, QStringLiteral("AF-A"));
        QVERIFY(first);
        QCOMPARE(first->firstSession, 0);
        QCOMPARE(first->values, (QVector<double>{60, 60, 64, 63}));
        QCOMPARE(first->changes, (QVector<double>{0, 0, 4, -1}));
        QCOMPARE(first->adjusted, first->values); // no recalc: nothing re-based
        QCOMPARE(first->current(), 63.0);

        const Development::Track *second = timeline.track(2, QStringLiteral("AF-A"));
        QVERIFY(second);
        QCOMPARE(second->firstSession, 1);
        QVERIFY(std::isnan(second->values[0]));
        QVERIFY(std::isnan(second->adjusted[0]));
        QCOMPARE(second->values[3], 70.0);
        QVERIFY(!timeline.track(3, QStringLiteral("AF-A")));
    }

    void windowsCountChangesAfterTheirBaseline()
    {
        const std::vector<DwrsEntry> history = {
            row(1, QStringLiteral("CM-S"), 50, QStringLiteral("2026-09-01 20:00:00")),
            row(1, QStringLiteral("CM-S"), 53, QStringLiteral("2026-09-02 20:00:00")),
            row(1, QStringLiteral("CM-S"), 55, QStringLiteral("2026-09-03 20:00:00")),
            row(1, QStringLiteral("CM-S"), 58, QStringLiteral("2026-09-04 20:00:00")),
        };
        const auto timeline = Development::buildTimeline(history, {});
        const Development::Track &track = *timeline.track(1, QStringLiteral("CM-S"));
        QCOMPARE(Development::windowStart(4, 1), 2);
        QCOMPARE(Development::change(track, 1), 3.0);  // 55 -> 58
        QCOMPARE(Development::change(track, 2), 5.0);  // 53 -> 58
        QCOMPARE(Development::change(track, 10), 8.0); // window longer than history
        QCOMPARE(Development::windowStart(4, 10), 0);
        QCOMPARE(Development::change(track, 0), 8.0);  // whole history
        QCOMPARE(Development::windowStart(4, 0), 0);
    }

    void recalcsRebaseInsteadOfCounting()
    {
        // A formula change at 09-03 12:00 lifts the rating by 10 without any
        // development; the import of the same evening adds 2 real points.
        const QString recalc = QStringLiteral("2026-09-03 12:00:00");
        const std::vector<DwrsEntry> history = {
            row(1, QStringLiteral("GK-D"), 50, QStringLiteral("2026-09-02 20:00:00")),
            row(1, QStringLiteral("GK-D"), 60, recalc),
            row(1, QStringLiteral("GK-D"), 62, QStringLiteral("2026-09-03 20:00:00")),
        };
        const auto timeline = Development::buildTimeline(history, {recalc});
        QCOMPARE(timeline.size(), 3);
        const Development::Track &track = *timeline.track(1, QStringLiteral("GK-D"));
        QCOMPARE(track.values, (QVector<double>{50, 60, 62}));
        QCOMPARE(track.rebases, (QVector<double>{0, 10, 0}));
        QCOMPARE(track.adjusted, (QVector<double>{60, 60, 62})); // the old 50 on today's scale
        QCOMPARE(Development::recalcMarkers({&track}, 0), QList<int>{1});
        QVERIFY(Development::recalcMarkers({&track}, 1).isEmpty()); // before the window
        QCOMPARE(Development::change(track, 0), 2.0);

        // Merged into the same snapshot as an import, only the import counts.
        const std::vector<DwrsEntry> merged = {
            row(1, QStringLiteral("GK-D"), 50, QStringLiteral("2026-09-02 20:00:00")),
            row(1, QStringLiteral("GK-D"), 60, QStringLiteral("2026-09-03 20:00:00")),
            row(1, QStringLiteral("GK-D"), 62, QStringLiteral("2026-09-03 20:04:00")),
        };
        const auto mergedTimeline =
            Development::buildTimeline(merged, {QStringLiteral("2026-09-03 20:00:00")});
        QCOMPARE(mergedTimeline.size(), 2);
        QCOMPARE(Development::change(*mergedTimeline.track(1, QStringLiteral("GK-D")), 1), 2.0);
    }

    void squadTrendIgnoresLateArrivals()
    {
        // Player 1 grows 2 per snapshot; player 2 (a new signing far above the
        // squad level) appears in snapshot 1 and grows 4 afterwards.
        const std::vector<DwrsEntry> history = {
            row(1, QStringLiteral("ST-A"), 50, QStringLiteral("2026-09-01 20:00:00")),
            row(1, QStringLiteral("ST-A"), 52, QStringLiteral("2026-09-02 20:00:00")),
            row(1, QStringLiteral("ST-A"), 54, QStringLiteral("2026-09-03 20:00:00")),
            row(2, QStringLiteral("ST-A"), 80, QStringLiteral("2026-09-02 20:00:00")),
            row(2, QStringLiteral("ST-A"), 84, QStringLiteral("2026-09-03 20:00:00")),
        };
        const auto timeline = Development::buildTimeline(history, {});
        const QList<const Development::Track *> tracks = {
            timeline.track(1, QStringLiteral("ST-A")), timeline.track(2, QStringLiteral("ST-A"))};
        const QVector<double> trend = Development::cumulativeMeanChange(tracks, 0);
        QCOMPARE(trend.size(), 3);
        QCOMPARE(trend[0], 0.0);
        QCOMPARE(trend[1], 2.0); // only player 1 counts: player 2 just arrived
        QCOMPARE(trend[2], 5.0); // + mean(2, 4)

        const QVector<double> late = Development::cumulativeMeanChange(tracks, 1);
        QVERIFY(std::isnan(late[0]));
        QCOMPARE(late[1], 0.0);
        QCOMPARE(late[2], 3.0);
    }
};

QTEST_APPLESS_MAIN(TestDevelopment)
#include "test_development.moc"
