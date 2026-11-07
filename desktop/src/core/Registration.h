#pragma once

#include "Player.h"
#include "SquadBuilder.h"

#include <QDate>
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
// modelled (yet): no restriction. Leagues with 99 places and no quota the
// analyzer can check (Bundesliga, Ligue 1, Austrian Bundesliga) are selectable
// by name but behave like None. FM27 rules come from the game's "Squad
// Selection Rules" screens (docs/REGISTRIERUNG-FM27.md). Nationality quotas
// are not modelled; the non-EU limit of the squad list is (LaLiga,
// Switzerland), with the non-EU status maintained by hand.
enum class LeagueRules {
    None,
    Bundesliga,
    PremierLeague,
    LaLiga,
    SerieA,
    Ligue1,
    LigaPortugal,
    SwissSuperLeague,
    AustrianBundesliga,
    RussianPremierLiga,
    UkrainianPremierLeague,
    SuperLig,
};

// Stable settings keys ("none", "bundesliga", "premier_league", "laliga", ...).
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

// Whether the league caps wages on the squad list (wage budget, minimum wage).
// Only LaLiga does.
bool hasWageRules(LeagueRules rules);

// Most non-EU players the league's squad list may hold; 0 = no such limit.
// LaLiga 3, Swiss Super League 10. The status is the game's own judgement
// (agreements, second passports), so it is marked by hand.
int nonEuLimit(LeagueRules rules);

// Most non-EU players in the league's starting XI; 0 = no such limit. Only
// the Swiss Super League has one (5). Best XI honours it for league matches.
int nonEuStartingLimit(LeagueRules rules);

// Youth exemption of the league's squad list: who may play without a slot.
// U21 (Premier League, Ukraine): U21 per the PL cut-off. U18Eu (LaLiga): EU
// players aged 18 or younger on 1 January of the season.
enum class YouthRule { None, U21, U18Eu };
YouthRule youthRule(LeagueRules rules);

// League rule sets available for an FM version (rules are per game release).
QList<LeagueRules> leagueRulesFor(const QString &fmVersionId);
// Whether the UEFA club competitions (CL/EL/ECL, identical rules) are modelled.
bool uefaRulesFor(const QString &fmVersionId);

// Per-database choice (settings registration_league / _uefa / _min_goalkeepers,
// registration_wage_budget / _min_wage / _wage_period).
struct Settings {
    LeagueRules league = LeagueRules::None;
    bool uefa = false;
    int minGoalkeepers = 2; // not a rule anywhere — a sane-squad floor
    // League wage rules (e.g. LaLiga), per week in the save's currency; 0 = none.
    // The budget caps the summed wages of the listed players, the minimum wage
    // is what a player must earn to be registered at all.
    double wageBudget = 0.0;
    double minWage = 0.0;
    // In-game date of the newest import (invalid = unknown); settles which
    // side of a youth cut-off date the season is on.
    QDate gameDate;

    // Some competition needs a squad list (markings + assistant are in use).
    bool active() const { return restrictsSquad(league) || uefa; }
};

// U21 per the PL/UEFA cut-off (born on/after 1 Jan of season start year − 21).
// FM exports only the whole age, so 21 is ambiguous: most 21-year-olds qualify
// at the summer deadline, all at the winter one. Unknown age is ambiguous too.
enum class U21Status { Yes, No, Uncertain };
U21Status u21StatusByAge(int age);         // the automatic rule alone
U21Status u21Status(const Player &player); // honours the manual override

// A manual U21 decision settles the player's current age (typically the
// ambiguous 21). Once an import shows him older and 22+, it expires and the
// automatic rule (not U21) applies again — set it anew if FM still counts him.
// Returns whether the override was reset.
bool expireU21Override(PlayerRegistration &registration, int oldAge, int newAge);

// Settings key listing (comma-separated uids) players whose U21 override
// expired since the registration page last took stock; it reports them once.
QString expiredU21SettingKey();

// LaLiga youth exemption by age alone: aged 18 or younger on 1 January of the
// season. FM exports only the whole age, so the in-game date decides: from
// January on (until the season ends) 18 is fine and 19 unclear; before January
// 18 is unclear (he may turn 19 by then) and 17 fine. Unknown date: 17 fine,
// 18 and 19 unclear.
U21Status u18StatusByAge(int age, const QDate &gameDate);
// EU/EEA nationality by the game's three-letter codes (first or second
// nationality). Not proof on its own (passports, agreements), but without it
// the automatic EU youth exemption is never certain.
bool hasEuNationality(const Player &player);
// The same honouring the manual override (PlayerRegistration::youth), which
// settles the whole exemption (nationality included). Automatic: marked non-EU
// means no; without a recognised EU nationality it is at most unclear.
U21Status u18EuStatus(const Player &player, const QDate &gameDate);

// A manual youth decision ends once the player is 20 or older after an import.
// Returns whether the override was reset.
bool expireYouthOverride(PlayerRegistration &registration, int oldAge, int newAge);

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
    // League: who needs no slot for being young (see YouthRule). Leagues
    // whose youth exemption is not documented register everyone.
    YouthRule youth = YouthRule::U21;
    QDate gameDate; // for the U18 cut-off (see u18StatusByAge)
    // Wage rules (league only), weekly; 0 = none. Players without a known wage
    // count as 0 and are never held back by the minimum.
    double wageBudget = 0.0;
    double minWage = 0.0;
    // Non-EU players on the list (league only); 0 = no limit.
    int maxNonEu = 0;
};

// Counts as non-EU (hand-maintained) and the quota limits them.
bool countsAsNonEu(const Player &player, const Quota &quota);

// Marked as "cannot be registered" for the competition: never proposed.
bool isBlocked(const Player &player, Competition competition);

// Weekly wage of a player for the wage rules; unknown counts as 0.
double weeklyWage(const Player &player);
// Below the minimum wage (known wage only; FM rounds, so a hair below is fine).
// Never for players marked as exempt from it (PlayerRegistration::minWageExempt).
bool belowMinWage(const Player &player, const Quota &quota);
// Settings units for the wage fields (registration_wage_period).
enum class WagePeriod { Week, Month, Year };
QString wagePeriodKey(WagePeriod period);
WagePeriod wagePeriodFromKey(const QString &key); // unknown -> Week
double weeksPer(WagePeriod period);
Quota quotaFor(Competition competition, const Settings &settings);

// Youth status under the quota's youth rule (None counts as No).
U21Status youthStatus(const Player &player, const Quota &quota);
// Short label of the youth exemption ("U21", "U18 EU"); empty for None.
QString youthLabel(YouthRule rule);

// Eligible without a slot: young enough where the league exempts them (see
// YouthRule), or list B (UEFA). Without a quota the Premier League rule (U21
// exempt) applies.
bool isExempt(const Player &player, Competition competition);
bool isExempt(const Player &player, Competition competition, const Quota &quota);

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
    BelowMinWage,       // earns less than the league's minimum wage
    WageBudgetFull,     // his wage no longer fits the wage budget
    NonEuFull,          // the non-EU limit is used up
    Blocked,            // marked as not registrable
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
    int nonEu = 0;         // listed non-EU players (where the quota limits them)
    int emptySlots = 0;    // places no eligible player could fill
    double totalWage = 0.0; // weekly wages of the listed players
};

// Promised much playing time in the contract (Star Player, Important Player,
// Regular Starter, First-Choice Goalkeeper): with a wage budget these players
// are registered first and never traded for cheaper ones.
bool hasPromisedPlayingTime(const Player &player);

// What a place on the list is worth to the wage-budget optimization: the
// tier (starting XI far above B-team, depth, reserve), then the best DWRS.
double registrationValue(const RankedPlayer &entry);

// Fills the list in rank order: the needed goalkeepers first, then everyone who
// still fits a quota. Walking the whole ranking means free home-grown /
// club-trained places always go to the best remaining such player (youth
// included) instead of staying empty. Greedy is optimal here because the
// quotas are nested limits. A wage budget is not nested: there the players
// with promised playing time go first, the walk fills the rest (starters
// first, as ranked), and swaps (one non-starter out, up to two in) then trade
// expensive places for cheaper players whenever that raises the summed
// registrationValue(). The starting XI never gets weaker than the walk's.
Proposal propose(const std::vector<RankedPlayer> &ranked, Competition competition,
                 const Quota &quota);

// --- Saved squad list ---

// On the saved list of the competition (league squad list / UEFA list A).
bool isListed(const Player &player, Competition competition);

// May play in the competition: on its saved list, or eligible without a slot.
bool isEligible(const Player &player, Competition competition);
bool isEligible(const Player &player, Competition competition, const Quota &quota);

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
    double totalWage = 0.0; // weekly wages of the listed players
    int belowMinWage = 0;   // listed although under the minimum wage
    int nonEu = 0;          // listed non-EU players (where the quota limits them)

    bool tooMany = false;              // more players than places
    bool tooManyNonHomeGrown = false;
    bool tooManyNonClubTrained = false;
    bool tooFewGoalkeepers = false;
    bool overWageBudget = false;
    bool tooManyNonEu = false;

    bool valid() const
    {
        return !tooMany && !tooManyNonHomeGrown && !tooManyNonClubTrained && !tooFewGoalkeepers
               && !overWageBudget && belowMinWage == 0 && !tooManyNonEu;
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
