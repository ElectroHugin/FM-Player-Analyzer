#include "core/Player.h"
#include "core/PlayerStatus.h"

#include <QtTest>

using namespace fm;

// Backlog #24/#25/#33: one shared availability/eligibility rule set for every
// pool and suggestion list, plus the departure bookkeeping.

namespace {

Player makePlayer(const QString &club, int age = 25, const QString &nat = QStringLiteral("GER"),
                  const QString &nat2 = QString())
{
    Player p;
    p.uid = QStringLiteral("u");
    p.name = QStringLiteral("Test");
    p.club = club;
    p.age = age;
    p.nationality = nat;
    p.secondNationality = nat2;
    return p;
}

PlayerStatus::FreshnessContext freshness(int counter = 10, const QString &userClub = {})
{
    PlayerStatus::FreshnessContext f;
    f.currentCounter = counter;
    f.retirementAge = 35;
    f.staleAfterUploads = 5;
    f.userClub = userClub;
    return f;
}

} // namespace

class TestPlayerStatus : public QObject
{
    Q_OBJECT

private slots:
    void retiredClubConvention()
    {
        QVERIFY(PlayerStatus::isRetiredClub(makePlayer(QStringLiteral("Retired"))));
        QVERIFY(PlayerStatus::isRetiredClub(makePlayer(QStringLiteral("  retired "))));
        QVERIFY(!PlayerStatus::isRetiredClub(makePlayer(QStringLiteral("FC Bayern"))));
        QVERIFY(!PlayerStatus::isRetiredClub(makePlayer(QString())));
    }

    void retiredCombinesClubAndFreshness()
    {
        // Manual club tag counts regardless of freshness data.
        Player tagged = makePlayer(QStringLiteral("Retired"), 22);
        tagged.lastSeenUpdate = 10;
        QVERIFY(PlayerStatus::isRetired(tagged, freshness()));

        // Auto-retired: old enough and missing from the last 5 uploads.
        Player vanished = makePlayer(QStringLiteral("Some Club"), 36);
        vanished.lastSeenUpdate = 4; // 6 uploads ago
        QVERIFY(PlayerStatus::isRetired(vanished, freshness()));

        // Merely stale but young: still in the game (scouting data just old).
        Player staleYoung = makePlayer(QStringLiteral("Some Club"), 24);
        staleYoung.lastSeenUpdate = 1;
        QVERIFY(!PlayerStatus::isRetired(staleYoung, freshness()));

        // Own-club players are never auto-retired.
        Player ownVeteran = makePlayer(QStringLiteral("FC Bayern"), 37);
        ownVeteran.lastSeenUpdate = 1;
        QVERIFY(!PlayerStatus::isRetired(ownVeteran, freshness(10, QStringLiteral("FC Bayern"))));
    }

    void inactiveHidesStalePlayersAtAnyAge()
    {
        const QString myClub = QStringLiteral("Wrexham");
        // Stale (missed 9 of the last full exports) and young: no longer relevant.
        Player staleYoung = makePlayer(QStringLiteral("Some Club"), 22);
        staleYoung.lastSeenUpdate = 1;
        QVERIFY(PlayerStatus::isInactive(staleYoung, freshness(10, myClub)));

        // Missed fewer than Y full exports: still shown.
        Player recent = makePlayer(QStringLiteral("Some Club"), 22);
        recent.lastSeenUpdate = 6;
        QVERIFY(!PlayerStatus::isInactive(recent, freshness(10, myClub)));

        // Shortlisted players stay until retired.
        Player shortlisted = staleYoung;
        shortlisted.onShortlist = true;
        QVERIFY(!PlayerStatus::isInactive(shortlisted, freshness(10, myClub)));
        Player shortlistedVeteran = makePlayer(QStringLiteral("Some Club"), 36);
        shortlistedVeteran.lastSeenUpdate = 1;
        shortlistedVeteran.onShortlist = true;
        QVERIFY(PlayerStatus::isInactive(shortlistedVeteran, freshness(10, myClub)));

        // Own-club players are never hidden by freshness.
        Player own = makePlayer(myClub, 22);
        own.lastSeenUpdate = 1;
        QVERIFY(!PlayerStatus::isInactive(own, freshness(10, myClub)));

        // The national pool applies the same rule.
        PlayerStatus::NationalCriteria criteria;
        criteria.countryCode = QStringLiteral("GER");
        criteria.ageLimit = 99;
        criteria.freshness = freshness();
        QVERIFY(!PlayerStatus::isAvailableForNation(staleYoung, criteria));
    }

    void nationalEligibility_data()
    {
        QTest::addColumn<QString>("nat");
        QTest::addColumn<QString>("nat2");
        QTest::addColumn<int>("age");
        QTest::addColumn<QString>("code");
        QTest::addColumn<int>("ageLimit");
        QTest::addColumn<bool>("expected");

        QTest::newRow("nationality") << "GER" << "" << 25 << "GER" << 99 << true;
        QTest::newRow("second nationality") << "TUR" << "GER" << 25 << "GER" << 99 << true;
        QTest::newRow("other nation") << "FRA" << "" << 25 << "GER" << 99 << false;
        QTest::newRow("no code configured") << "GER" << "" << 25 << "" << 99 << false;
        QTest::newRow("within U21") << "GER" << "" << 21 << "GER" << 21 << true;
        QTest::newRow("over U21") << "GER" << "" << 22 << "GER" << 21 << false;
        QTest::newRow("unknown age, limit active") << "GER" << "" << 0 << "GER" << 21 << false;
        QTest::newRow("unknown age, no limit") << "GER" << "" << 0 << "GER" << 99 << true;
        QTest::newRow("limit 0 = no limit") << "GER" << "" << 40 << "GER" << 0 << true;
    }

    void nationalEligibility()
    {
        QFETCH(QString, nat);
        QFETCH(QString, nat2);
        QFETCH(int, age);
        QFETCH(QString, code);
        QFETCH(int, ageLimit);
        QFETCH(bool, expected);
        const Player p = makePlayer(QStringLiteral("Club"), age, nat, nat2);
        QCOMPARE(PlayerStatus::isNationalEligible(p, code, ageLimit), expected);
    }

    void availableForNationExcludesRetired()
    {
        PlayerStatus::NationalCriteria criteria;
        criteria.countryCode = QStringLiteral("GER");
        criteria.ageLimit = 99;
        criteria.freshness = freshness();

        Player current = makePlayer(QStringLiteral("Club"));
        current.lastSeenUpdate = 10; // in the latest full export
        QVERIFY(PlayerStatus::isAvailableForNation(current, criteria));
        QVERIFY(!PlayerStatus::isAvailableForNation(makePlayer(QStringLiteral("Retired")),
                                                    criteria));
        QVERIFY(!PlayerStatus::isAvailableForNation(
            makePlayer(QStringLiteral("Club"), 25, QStringLiteral("FRA")), criteria));
    }

    void departureClearsListing()
    {
        Player p = makePlayer(QStringLiteral("FC Bayern"));
        p.transferStatus = true;
        p.loanStatus = true;
        p.newClub = QStringLiteral("Somewhere");
        PlayerStatus::applyDeparture(p, PlayerStatus::freeAgentClubTag());
        QCOMPARE(p.club, QStringLiteral("FrA"));
        QVERIFY(!p.transferStatus);
        QVERIFY(!p.loanStatus);
        QVERIFY(p.newClub.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestPlayerStatus)
#include "test_playerstatus.moc"
