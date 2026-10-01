#pragma once

#include "Player.h"
#include "SquadBuilder.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <utility>
#include <vector>

namespace fm {

class Definitions;

// Squad-registration rules (e.g. Premier League 25-man list, UEFA list A/B).
// FM exports no home-grown data, so the status per player is maintained by hand
// (PlayerRegistration); this module holds the rule sets and the derived checks.
namespace Registration {

// League rule set. None is the default for every league whose rules are not
// modelled (yet): no restriction. Bundesliga is selectable by name but has no
// effective restriction either (99 places, no quota), so it behaves like None.
enum class LeagueRules { None, Bundesliga, PremierLeague };

// Stable settings keys ("none", "bundesliga", "premier_league").
QString leagueRulesKey(LeagueRules rules);
LeagueRules leagueRulesFromKey(const QString &key); // unknown -> None

// The league's own name (a proper noun, not translated); empty for None.
QString leagueDisplayName(LeagueRules rules);

// The domestic cup(s) that go with the league (proper nouns); empty for None.
// Cups need no registration: every player may play.
QString cupDisplayName(LeagueRules rules);

// Whether the league's registration limits who may play at all. Without that
// there is no squad list to build: every player is eligible.
bool restrictsSquad(LeagueRules rules);

// League rule sets available for an FM version (rules are per game release).
QList<LeagueRules> leagueRulesFor(const QString &fmVersionId);
// Whether the UEFA club competitions (CL/EL/ECL, identical rules) are modelled.
bool uefaRulesFor(const QString &fmVersionId);

// Per-database choice (settings registration_league / _uefa / _min_goalkeepers).
struct Settings {
    LeagueRules league = LeagueRules::None;
    bool uefa = false;
    int minGoalkeepers = 2; // not a rule anywhere — a sane-squad floor

    // Some competition needs a squad list (markings + assistant are in use).
    bool active() const { return restrictsSquad(league) || uefa; }
};

// U21 per the PL/UEFA cut-off (born on/after 1 Jan of season start year − 21).
// FM exports only the whole age, so 21 is ambiguous: most 21-year-olds qualify
// at the summer deadline, all at the winter one. Unknown age is ambiguous too.
enum class U21Status { Yes, No, Uncertain };
U21Status u21StatusByAge(int age);         // the automatic rule alone
U21Status u21Status(const Player &player); // honours the manual override

// Free of a registration slot: definitely U21. Uncertain counts as not U21 —
// registering him is always legal, leaving him off may not be.
bool isU21(const Player &player);

// Trained in the association — club-trained players are home-grown too.
bool isHomeGrown(const Player &player);
bool isClubTrained(const Player &player);

// UEFA list B needs U21 and two uninterrupted years at the club since age 15;
// club-trained is the closest hand-maintained stand-in for the latter.
bool isListBEligible(const Player &player);

// Keeps the flags consistent: club-trained implies home-grown.
void normalize(PlayerRegistration &registration);

// --- Squad-list proposal ---

// Which list is being built: the league squad list or UEFA list A.
enum class Competition { League, Uefa };

// Capacity rules of one list. Categories are nested (non-home-grown ⊂
// not-club-trained ⊂ all), so the limits form a chain.
struct Quota {
    int maxSize = 25;
    int maxNonHomeGrown = 17;   // players neither home-grown nor club-trained
    int maxNonClubTrained = 25; // PL: no club-trained quota; UEFA: 21
    int minGoalkeepers = 0;
    // Exempt keepers (U21) count towards the minimum: true for the league
    // (they may play), false for UEFA (the minimum applies to list A itself).
    bool exemptKeepersCount = true;
};
Quota quotaFor(Competition competition, const Settings &settings);

// Eligible without a slot: U21 (league) or list B (UEFA).
bool isExempt(const Player &player, Competition competition);

enum class Category { NonHomeGrown, HomeGrown, ClubTrained };
Category category(const Player &player);

// How much the squad needs a player, from the favorite tactics' squads.
enum class Tier { StartingXi, BTeam, Depth, Reserve };

struct RankedPlayer {
    const Player *player = nullptr;
    Tier tier = Tier::Reserve;
    double bestDwrs = 0.0; // best rating across the assigned roles
};

// Ranks the pool by its best tier over all given tactics (XI > B-team > depth >
// rest), then by best DWRS. U21 players are ranked too — they compete for the
// XI even though they need no slot.
std::vector<RankedPlayer> rankPool(const SquadBuilder &builder, const Definitions &definitions,
                                   const std::vector<const Player *> &pool,
                                   const QStringList &tactics, const RoleRatings &ratings);

enum class LeftOutReason {
    ListFull,           // all slots taken
    NonHomeGrownFull,   // the non-home-grown quota is used up
    NonClubTrainedFull, // only club-trained places are left (UEFA)
};

struct Proposal {
    Quota quota;
    std::vector<RankedPlayer> listed; // registered, in rank order
    std::vector<RankedPlayer> exempt; // eligible without a slot, in rank order
    std::vector<std::pair<RankedPlayer, LeftOutReason>> leftOut; // in rank order
    int nonHomeGrown = 0;  // listed per category
    int homeGrownOnly = 0; // home-grown but not club-trained
    int clubTrained = 0;
    int goalkeepers = 0;   // counted towards the minimum (see Quota)
    int emptySlots = 0;    // places no eligible player could fill
};

// Fills the list in rank order: the needed goalkeepers first, then everyone who
// still fits a quota. Walking the whole ranking means free home-grown /
// club-trained places always go to the best remaining such player (youth
// included) instead of staying empty. Greedy is optimal here because the
// quotas are nested limits.
Proposal propose(const std::vector<RankedPlayer> &ranked, Competition competition,
                 const Quota &quota);

// --- Saved squad list ---

// On the saved list of the competition (league squad list / UEFA list A).
bool isListed(const Player &player, Competition competition);

// May play in the competition: on its saved list, or eligible without a slot.
bool isEligible(const Player &player, Competition competition);

// Whether a list has been saved for the competition at all (someone is listed).
bool hasSavedList(const std::vector<const Player *> &players, Competition competition);

// A hand-picked list measured against the quota — the user ticks who is really
// registered, which need not be the proposal.
struct ListCheck {
    int size = 0;
    int nonHomeGrown = 0;
    int homeGrownOnly = 0; // home-grown but not club-trained
    int clubTrained = 0;
    int goalkeepers = 0;   // counted towards the minimum (see Quota)

    bool tooMany = false;              // more players than places
    bool tooManyNonHomeGrown = false;
    bool tooManyNonClubTrained = false;
    bool tooFewGoalkeepers = false;

    bool valid() const
    {
        return !tooMany && !tooManyNonHomeGrown && !tooManyNonClubTrained && !tooFewGoalkeepers;
    }
};
// listed: the players taking a slot; exempt: those eligible without one (their
// keepers count towards the minimum only where the quota says so).
ListCheck checkList(const std::vector<const Player *> &listed,
                    const std::vector<const Player *> &exempt, const Quota &quota);

// Average rating of the starting XI (filled slots) per tactic: without any
// registration restriction versus with only the eligible players.
struct QuotaCost {
    QString tactic;
    double unrestricted = 0.0;
    double registered = 0.0;
};
std::vector<QuotaCost> quotaCosts(const SquadBuilder &builder, const Definitions &definitions,
                                  const std::vector<const Player *> &pool,
                                  const std::vector<const Player *> &eligible,
                                  const QStringList &tactics, const RoleRatings &ratings);

} // namespace Registration

} // namespace fm
