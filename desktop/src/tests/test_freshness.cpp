#include "core/Freshness.h"
#include "core/Player.h"

#include <QtTest>

using namespace fm;

namespace {

Player playerAt(int lastSeen, int age = 30, const QString &club = QStringLiteral("Some FC"))
{
    Player p;
    p.lastSeenUpdate = lastSeen;
    p.age = age;
    p.club = club;
    return p;
}

} // namespace

class TestFreshness : public QObject
{
    Q_OBJECT

private slots:
    void uploadsSinceSeen_basics()
    {
        // Present in the latest upload -> fresh.
        QCOMPARE(Freshness::uploadsSinceSeen(playerAt(5), 5), 0);
        // Missed three uploads.
        QCOMPARE(Freshness::uploadsSinceSeen(playerAt(5), 8), 3);
    }

    void uploadsSinceSeen_neverStampedOrNoUploads()
    {
        // No upload recorded yet -> nothing is stale.
        QCOMPARE(Freshness::uploadsSinceSeen(playerAt(0), 0), 0);
        // Legacy row (lastSeen 0) while uploads exist -> treated as fresh, not
        // flagged with a huge miss count.
        QCOMPARE(Freshness::uploadsSinceSeen(playerAt(0), 7), 0);
        // Never negative even if a stamp is somehow ahead of the counter.
        QCOMPARE(Freshness::uploadsSinceSeen(playerAt(9), 4), 0);
    }

    void isStale_threshold()
    {
        // Y = 5: missing exactly 5 uploads is stale; 4 is not.
        QVERIFY(!Freshness::isStale(playerAt(1), 5, 5)); // missed 4
        QVERIFY(Freshness::isStale(playerAt(1), 6, 5));  // missed 5
        // Y < 1 disables staleness entirely.
        QVERIFY(!Freshness::isStale(playerAt(1), 100, 0));
    }

    void isRetired_ageClubAndStaleness()
    {
        const QString myClub = QStringLiteral("My United");
        // Old, stale, not my club -> retired.
        QVERIFY(Freshness::isRetired(playerAt(1, 36, QStringLiteral("Rivals")), 6, 35, 5, myClub));
        // Too young -> not retired even if stale.
        QVERIFY(!Freshness::isRetired(playerAt(1, 34, QStringLiteral("Rivals")), 6, 35, 5, myClub));
        // In my own club -> never retired.
        QVERIFY(!Freshness::isRetired(playerAt(1, 40, myClub), 6, 35, 5, myClub));
        // Old and not mine but still fresh -> not retired.
        QVERIFY(!Freshness::isRetired(playerAt(6, 40, QStringLiteral("Rivals")), 6, 35, 5, myClub));
        // Empty userClub means no player is "own club".
        QVERIFY(Freshness::isRetired(playerAt(1, 40, QStringLiteral("Anything")), 6, 35, 5,
                                     QString()));
    }
};

QTEST_APPLESS_MAIN(TestFreshness)
#include "test_freshness.moc"
