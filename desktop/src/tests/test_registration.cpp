#include "core/AppConfig.h"
#include "core/Definitions.h"
#include "core/Registration.h"

#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace fm;

namespace {

enum Status { None, Home, Club };

// A pool in the given (rank) order; later players rank lower.
struct Squad {
    std::vector<Player> players;

    void add(const QString &uid, int age, Status status = None, bool keeper = false)
    {
        Player p;
        p.uid = uid;
        p.name = uid;
        p.age = age;
        p.positionRaw = keeper ? QStringLiteral("GK") : QStringLiteral("M (C)");
        p.registration.homeGrown = status != None;
        p.registration.clubTrained = status == Club;
        players.push_back(p);
    }

    std::vector<Registration::RankedPlayer> ranked() const
    {
        std::vector<Registration::RankedPlayer> out;
        double dwrs = 100.0;
        for (const Player &p : players)
            out.push_back({&p, Registration::Tier::Depth, dwrs--});
        return out;
    }
};

Registration::Settings plSettings(int minGoalkeepers)
{
    Registration::Settings settings;
    settings.league = Registration::LeagueRules::PremierLeague;
    settings.uefa = true;
    settings.minGoalkeepers = minGoalkeepers;
    return settings;
}

} // namespace

class TestRegistration : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Definitions> m_definitions;
    std::unique_ptr<AppConfig> m_config;
    std::unique_ptr<SquadBuilder> m_builder;
    QTemporaryDir m_dir;

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
        using L = Registration::LeagueRules;
        QVERIFY(Registration::leagueRulesFor(QStringLiteral("fm24"))
                == (QList<L>{L::Bundesliga, L::PremierLeague}));
        QVERIFY(Registration::uefaRulesFor(QStringLiteral("fm24")));
        QVERIFY(Registration::leagueRulesFor(QStringLiteral("fm99")).isEmpty());
        QVERIFY(!Registration::uefaRulesFor(QStringLiteral("fm99")));
    }

    void bundesligaBehavesLikeDefault()
    {
        using L = Registration::LeagueRules;
        QVERIFY(!Registration::restrictsSquad(L::None));
        QVERIFY(!Registration::restrictsSquad(L::Bundesliga));
        QVERIFY(Registration::restrictsSquad(L::PremierLeague));
        QCOMPARE(Registration::leagueDisplayName(L::Bundesliga), QStringLiteral("Bundesliga"));
        QVERIFY(Registration::leagueDisplayName(L::None).isEmpty());
        QCOMPARE(Registration::cupDisplayName(L::Bundesliga), QStringLiteral("DFB-Pokal"));
        QVERIFY(!Registration::cupDisplayName(L::PremierLeague).isEmpty());
        QVERIFY(Registration::cupDisplayName(L::None).isEmpty());

        // Nothing to register in the league — the assistant stays off unless
        // the UEFA rules are switched on.
        Registration::Settings settings;
        settings.league = L::Bundesliga;
        QVERIFY(!settings.active());
        settings.uefa = true;
        QVERIFY(settings.active());

        Registration::Settings none;
        const auto bundesliga =
            Registration::quotaFor(Registration::Competition::League, settings);
        const auto standard = Registration::quotaFor(Registration::Competition::League, none);
        QCOMPARE(bundesliga.maxNonHomeGrown, standard.maxNonHomeGrown);
        QCOMPARE(bundesliga.maxNonHomeGrown, bundesliga.maxSize); // no home-grown quota
        QCOMPARE(bundesliga.maxNonClubTrained, standard.maxNonClubTrained);
    }

    void eligibilityFromSavedList()
    {
        using C = Registration::Competition;
        Player senior = playerAged(27);
        Player youngster = playerAged(19);
        Player academy = playerAged(19);
        academy.registration.clubTrained = true;

        // Nobody listed yet: no saved list; only the exempt may "play".
        std::vector<const Player *> squad = {&senior, &youngster, &academy};
        QVERIFY(!Registration::hasSavedList(squad, C::League));
        QVERIFY(!Registration::isEligible(senior, C::League));
        QVERIFY(Registration::isEligible(youngster, C::League)); // U21 needs no slot
        QVERIFY(!Registration::isEligible(youngster, C::Uefa));  // list B needs club-trained
        QVERIFY(Registration::isEligible(academy, C::Uefa));

        // The two lists are independent.
        senior.registration.leagueListed = true;
        QVERIFY(Registration::hasSavedList(squad, C::League));
        QVERIFY(!Registration::hasSavedList(squad, C::Uefa));
        QVERIFY(Registration::isEligible(senior, C::League));
        QVERIFY(!Registration::isEligible(senior, C::Uefa));
        youngster.registration.uefaListed = true;
        QVERIFY(Registration::isEligible(youngster, C::Uefa));
    }

    void checkListFlagsViolations()
    {
        using C = Registration::Competition;
        Squad s;
        for (int i = 0; i < 18; ++i)
            s.add(QStringLiteral("n%1").arg(i), 25);
        for (int i = 0; i < 5; ++i)
            s.add(QStringLiteral("h%1").arg(i), 25, Home);
        s.add(QStringLiteral("c0"), 25, Club);
        s.add(QStringLiteral("gk0"), 30, Club, true);
        s.add(QStringLiteral("ygk"), 19, Club, true); // exempt keeper
        std::vector<const Player *> listed;
        for (int i = 0; i < 25; ++i)
            listed.push_back(&s.players[static_cast<size_t>(i)]);
        const std::vector<const Player *> exempt = {&s.players.back()};

        // League: 18 non-home-grown is one too many; the U21 keeper counts.
        auto check =
            Registration::checkList(listed, exempt, Registration::quotaFor(C::League, plSettings(2)));
        QCOMPARE(check.size, 25);
        QCOMPARE(check.nonHomeGrown, 18);
        QCOMPARE(check.homeGrownOnly, 5);
        QCOMPARE(check.clubTrained, 2);
        QCOMPARE(check.goalkeepers, 2);
        QVERIFY(check.tooManyNonHomeGrown);
        QVERIFY(!check.tooMany && !check.tooManyNonClubTrained && !check.tooFewGoalkeepers);
        QVERIFY(!check.valid());

        // UEFA: additionally 23 without club-trained (max 21) and only one
        // keeper on list A itself.
        check = Registration::checkList(listed, exempt, Registration::quotaFor(C::Uefa, plSettings(2)));
        QVERIFY(check.tooManyNonHomeGrown);
        QVERIFY(check.tooManyNonClubTrained);
        QVERIFY(check.tooFewGoalkeepers);
        QCOMPARE(check.goalkeepers, 1);

        // One player over the limit.
        listed.push_back(&s.players.back());
        check = Registration::checkList(listed, {}, Registration::quotaFor(C::League, plSettings(0)));
        QVERIFY(check.tooMany);

        // The proposal itself always passes its own check.
        const auto quota = Registration::quotaFor(C::Uefa, plSettings(2));
        const auto proposal = Registration::propose(s.ranked(), C::Uefa, quota);
        std::vector<const Player *> proposed, free;
        for (const auto &entry : proposal.listed)
            proposed.push_back(entry.player);
        for (const auto &entry : proposal.exempt)
            free.push_back(entry.player);
        check = Registration::checkList(proposed, free, quota);
        QCOMPARE(check.size, static_cast<int>(proposal.listed.size()));
        QCOMPARE(check.goalkeepers, proposal.goalkeepers);
        QVERIFY(!check.tooMany && !check.tooManyNonHomeGrown && !check.tooManyNonClubTrained);
    }

    void initTestCase()
    {
        m_definitions = std::make_unique<Definitions>();
        QVERIFY2(m_definitions->load(QStringLiteral(LEGACY_DIR)
                                     + QStringLiteral("/config/definitions.json")),
                 qPrintable(m_definitions->errorString()));
        m_config = std::make_unique<AppConfig>(m_dir.filePath(QStringLiteral("config.ini")));
        m_builder = std::make_unique<SquadBuilder>(*m_definitions, *m_config);
    }

    void premierLeagueQuota()
    {
        // 20 strong non-HG, then 3 HG, then 3 weak HG youth over 21, 2 U21.
        Squad s;
        for (int i = 0; i < 20; ++i)
            s.add(QStringLiteral("n%1").arg(i), 25);
        for (int i = 0; i < 3; ++i)
            s.add(QStringLiteral("h%1").arg(i), 26, Home);
        for (int i = 0; i < 3; ++i)
            s.add(QStringLiteral("y%1").arg(i), 22, Club);
        s.add(QStringLiteral("u0"), 19);
        s.add(QStringLiteral("u1"), 18);

        const auto proposal = Registration::propose(
            s.ranked(), Registration::Competition::League,
            Registration::quotaFor(Registration::Competition::League, plSettings(0)));

        QCOMPARE(proposal.nonHomeGrown, 17);
        QCOMPARE(proposal.homeGrownOnly, 3);
        // The weak club-trained youth take the otherwise empty places.
        QCOMPARE(proposal.clubTrained, 3);
        QCOMPARE(static_cast<int>(proposal.listed.size()), 23);
        QCOMPARE(proposal.emptySlots, 2);
        QCOMPARE(static_cast<int>(proposal.exempt.size()), 2);
        QCOMPARE(static_cast<int>(proposal.leftOut.size()), 3);
        for (const auto &[entry, reason] : proposal.leftOut) {
            QVERIFY(entry.player->uid.startsWith(QLatin1Char('n')));
            QVERIFY(reason == Registration::LeftOutReason::NonHomeGrownFull);
        }
        // The three weakest non-HG are the ones left out.
        QCOMPARE(proposal.leftOut.front().first.player->uid, QStringLiteral("n17"));
    }

    void listFullWhenEnoughHomeGrown()
    {
        Squad s;
        for (int i = 0; i < 30; ++i)
            s.add(QStringLiteral("h%1").arg(i), 25, Home);
        const auto proposal = Registration::propose(
            s.ranked(), Registration::Competition::League,
            Registration::quotaFor(Registration::Competition::League, plSettings(0)));
        QCOMPARE(static_cast<int>(proposal.listed.size()), 25);
        QCOMPARE(proposal.emptySlots, 0);
        QVERIFY(proposal.leftOut.back().second == Registration::LeftOutReason::ListFull);
    }

    void goalkeeperMinimum()
    {
        // 25 outfielders outrank two weak keepers; the minimum still lists them.
        Squad s;
        for (int i = 0; i < 25; ++i)
            s.add(QStringLiteral("h%1").arg(i), 25, Home);
        s.add(QStringLiteral("gk0"), 30, Home, true);
        s.add(QStringLiteral("gk1"), 31, None, true);
        auto proposal = Registration::propose(
            s.ranked(), Registration::Competition::League,
            Registration::quotaFor(Registration::Competition::League, plSettings(2)));
        QCOMPARE(proposal.goalkeepers, 2);
        QCOMPARE(static_cast<int>(proposal.listed.size()), 25);
        QCOMPARE(proposal.leftOut.size(), size_t(2)); // two outfielders made way

        // A U21 keeper counts for the league minimum — but not for UEFA list A.
        Squad t;
        for (int i = 0; i < 25; ++i)
            t.add(QStringLiteral("c%1").arg(i), 25, Club);
        t.add(QStringLiteral("gk0"), 30, Home, true);
        t.add(QStringLiteral("ygk"), 19, Club, true);
        proposal = Registration::propose(
            t.ranked(), Registration::Competition::League,
            Registration::quotaFor(Registration::Competition::League, plSettings(2)));
        QCOMPARE(proposal.goalkeepers, 2);
        QCOMPARE(proposal.leftOut.size(), size_t(1)); // only one keeper needed a slot
        proposal = Registration::propose(
            t.ranked(), Registration::Competition::Uefa,
            Registration::quotaFor(Registration::Competition::Uefa, plSettings(0)));
        QCOMPARE(proposal.quota.minGoalkeepers, 2); // UEFA floor even with setting 0
        QCOMPARE(proposal.goalkeepers, 1);          // list-B keeper does not count
    }

    void uefaClubTrainedPlaces()
    {
        Squad s;
        for (int i = 0; i < 18; ++i)
            s.add(QStringLiteral("n%1").arg(i), 25);
        for (int i = 0; i < 6; ++i)
            s.add(QStringLiteral("h%1").arg(i), 25, Home);
        for (int i = 0; i < 5; ++i)
            s.add(QStringLiteral("c%1").arg(i), 25, Club);
        const auto proposal = Registration::propose(
            s.ranked(), Registration::Competition::Uefa,
            Registration::quotaFor(Registration::Competition::Uefa, plSettings(0)));
        QCOMPARE(proposal.nonHomeGrown, 17);
        QCOMPARE(proposal.homeGrownOnly, 4); // 17 + 4 = 21, the rest is club-trained only
        QCOMPARE(proposal.clubTrained, 4);
        QCOMPARE(static_cast<int>(proposal.listed.size()), 25);
        int clubOnly = 0;
        for (const auto &[entry, reason] : proposal.leftOut)
            if (reason == Registration::LeftOutReason::NonClubTrainedFull)
                ++clubOnly;
        QCOMPARE(clubOnly, 2); // h4, h5
    }

    void rankPoolUsesSquadTiers()
    {
        const QStringList tactics = m_definitions->tacticNames();
        QVERIFY(!tactics.isEmpty());
        const QString gkRole =
            m_definitions->tacticRoles().value(tactics.first()).value(QStringLiteral("GK"));
        QVERIFY(!gkRole.isEmpty());
        // Three keepers: the best starts, the second is B-team, the third is not.
        std::vector<Player> players(3);
        RoleRatings ratings;
        for (int i = 0; i < 3; ++i) {
            players[i].uid = QStringLiteral("gk%1").arg(i);
            players[i].name = players[i].uid;
            players[i].age = 28;
            players[i].positionRaw = QStringLiteral("GK");
            players[i].assignedRoles = {gkRole};
            ratings[gkRole].insert(players[i].uid, 80.0 - 10 * i);
        }
        std::vector<const Player *> pool;
        for (const Player &p : players)
            pool.push_back(&p);
        const auto ranked = Registration::rankPool(*m_builder, *m_definitions, pool,
                                                   {tactics.first()}, ratings);
        QCOMPARE(ranked.size(), size_t(3));
        QCOMPARE(ranked[0].player->uid, QStringLiteral("gk0"));
        QVERIFY(ranked[0].tier == Registration::Tier::StartingXi);
        QVERIFY(ranked[1].tier == Registration::Tier::BTeam);
        QVERIFY(ranked[2].tier > Registration::Tier::BTeam);

        // No tactic: everyone is reserve, ordered by DWRS.
        const auto plain = Registration::rankPool(*m_builder, *m_definitions, pool, {}, ratings);
        QVERIFY(plain[0].tier == Registration::Tier::Reserve);
        QCOMPARE(plain[2].player->uid, QStringLiteral("gk2"));
    }

    void keysRoundTrip()
    {
        using L = Registration::LeagueRules;
        for (const L rules : {L::None, L::Bundesliga, L::PremierLeague})
            QCOMPARE(Registration::leagueRulesFromKey(Registration::leagueRulesKey(rules)), rules);
        QCOMPARE(Registration::leagueRulesFromKey(QStringLiteral("bogus")), L::None);
    }
};

QTEST_GUILESS_MAIN(TestRegistration)
#include "test_registration.moc"
