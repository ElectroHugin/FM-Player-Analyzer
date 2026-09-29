#pragma once

#include "Player.h"

#include <QString>

namespace fm {

// Shared "is this player still available / eligible?" rules, so every pool and
// suggestion list (transfer targets, call-ups, squad selection, matrices)
// applies the same logic instead of a hand-rolled copy per page.
namespace PlayerStatus {

// Club values the app writes itself (departure dialog, manual edits).
QString retiredClubTag();   // "Retired" — user convention: player left the game
QString freeAgentClubTag(); // "FrA" — released / without club

// Manual convention: club set to "Retired" (trimmed, case-insensitive).
bool isRetiredClub(const Player &player);

// Inputs for the live freshness-based retirement (see Freshness).
struct FreshnessContext {
    int currentCounter = 0;
    int retirementAge = 35;
    int staleAfterUploads = 5;
    QString userClub;
};

// Out of the game: club marked "Retired" OR auto-retired by data freshness
// (old enough, missing from the last uploads, not in the user's club). Only
// retirement counts — merely stale scouting data does not exclude a player.
bool isRetired(const Player &player, const FreshnessContext &freshness);

// Nationality or second nationality matches countryCode (non-empty). The age
// limit applies when 0 < ageLimit < 99 and then requires a known age within
// it; <= 0 or >= 99 means no limit.
bool isNationalEligible(const Player &player, const QString &countryCode, int ageLimit);

// Everything a national pool needs to decide availability.
struct NationalCriteria {
    QString countryCode;
    int ageLimit = 0;
    FreshnessContext freshness;
};

// Eligible for the national team and not retired — the call-up/suggestion pool.
bool isAvailableForNation(const Player &player, const NationalCriteria &criteria);

// Records that a player left the user's club for `destination` (club name,
// free-agent or retired tag). Clears the transfer/loan listing and the planned
// destination, which no longer apply once he is gone.
void applyDeparture(Player &player, const QString &destination);

} // namespace PlayerStatus

} // namespace fm
