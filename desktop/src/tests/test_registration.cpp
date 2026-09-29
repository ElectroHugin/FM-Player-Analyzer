#include "core/Registration.h"

#include <QtTest>

using namespace fm;

class TestRegistration : public QObject
{
    Q_OBJECT

private:
    static Player playerAged(int age)
    {
        Player p;
        p.age = age;
        return p;
    }

private slots:
    void u21ByAge()
    {
        using S = Registration::U21Status;
        QCOMPARE(Registration::u21StatusByAge(17), S::Yes);
        QCOMPARE(Registration::u21StatusByAge(20), S::Yes);
        QCOMPARE(Registration::u21StatusByAge(21), S::Uncertain);
        QCOMPARE(Registration::u21StatusByAge(22), S::No);
        QCOMPARE(Registration::u21StatusByAge(0), S::Uncertain); // unknown age
    }

    void overrideBeatsAge()
    {
        Player p = playerAged(21);
        QVERIFY(!Registration::isU21(p)); // uncertain needs a slot
        p.registration.u21 = PlayerRegistration::U21::Yes;
        QVERIFY(Registration::isU21(p));
        p = playerAged(19);
        p.registration.u21 = PlayerRegistration::U21::No;
        QVERIFY(!Registration::isU21(p));
    }

    void clubTrainedImpliesHomeGrown()
    {
        Player p = playerAged(25);
        p.registration.clubTrained = true;
        QVERIFY(Registration::isHomeGrown(p)); // even before normalize
        Registration::normalize(p.registration);
        QVERIFY(p.registration.homeGrown);
    }

    void listBNeedsU21AndClubTrained()
    {
        Player p = playerAged(19);
        QVERIFY(!Registration::isListBEligible(p));
        p.registration.clubTrained = true;
        QVERIFY(Registration::isListBEligible(p));
        p.age = 23;
        QVERIFY(!Registration::isListBEligible(p));
    }

    void rulesPerFmVersion()
    {
        QVERIFY(Registration::leagueRulesFor(QStringLiteral("fm24"))
                == QList<Registration::LeagueRules>{Registration::LeagueRules::PremierLeague});
        QVERIFY(Registration::uefaRulesFor(QStringLiteral("fm24")));
        QVERIFY(Registration::leagueRulesFor(QStringLiteral("fm99")).isEmpty());
        QVERIFY(!Registration::uefaRulesFor(QStringLiteral("fm99")));
    }

    void keysRoundTrip()
    {
        using L = Registration::LeagueRules;
        for (const L rules : {L::None, L::PremierLeague})
            QCOMPARE(Registration::leagueRulesFromKey(Registration::leagueRulesKey(rules)), rules);
        QCOMPARE(Registration::leagueRulesFromKey(QStringLiteral("bogus")), L::None);
    }
};

QTEST_GUILESS_MAIN(TestRegistration)
#include "test_registration.moc"
