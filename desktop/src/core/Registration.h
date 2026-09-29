#pragma once

#include "Player.h"

#include <QList>
#include <QString>

namespace fm {

// Squad-registration rules (e.g. Premier League 25-man list, UEFA list A/B).
// FM exports no home-grown data, so the status per player is maintained by hand
// (PlayerRegistration); this module holds the rule sets and the derived checks.
namespace Registration {

// League rule set. Only leagues whose registration actually restricts the
// squad are modelled; everything else is None.
enum class LeagueRules { None, PremierLeague };

// Stable settings keys ("none", "premier_league").
QString leagueRulesKey(LeagueRules rules);
LeagueRules leagueRulesFromKey(const QString &key); // unknown -> None

// League rule sets available for an FM version (rules are per game release).
QList<LeagueRules> leagueRulesFor(const QString &fmVersionId);
// Whether the UEFA club competitions (CL/EL/ECL, identical rules) are modelled.
bool uefaRulesFor(const QString &fmVersionId);

// Per-database choice (settings registration_league / _uefa / _min_goalkeepers).
struct Settings {
    LeagueRules league = LeagueRules::None;
    bool uefa = false;
    int minGoalkeepers = 2; // not a rule anywhere — a sane-squad floor

    bool active() const { return league != LeagueRules::None || uefa; }
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

} // namespace Registration

} // namespace fm
