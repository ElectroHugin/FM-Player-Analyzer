#pragma once

#include "Player.h"

#include <QString>

namespace fm {

// Data-freshness derivation. A per-database upload counter (settings key
// "update_counter") advances on every FULL export import (see isFullUpload);
// each imported player's lastSeenUpdate is stamped with the counter value. A
// partial export (squad, shortlist, scouting) stamps its players with the
// current value without advancing it: it refreshes them but ages nobody.
// Freshness is a pure function of (current counter - player.lastSeenUpdate) —
// nothing is stored per player beyond the stamp, so changing the thresholds
// re-evaluates instantly.
//
// Thresholds (configurable in Settings): retirementAge (X, default 35) and
// staleAfterUploads (Y, default 2), the latter counted in missed full exports.
namespace Freshness {

// Share of the current players an import must contain to count as a full export
// and advance the counter.
constexpr double kFullUploadShare = 0.25;

// An import of importedRows players counts as a full export when it reaches
// kFullUploadShare of currentPlayers — the players seen since the latest full
// export (uploadsSinceSeen 0), i.e. roughly that export's size. Measuring
// against all known players instead would let the never-pruned database outgrow
// a real full export over the seasons. Always true when nobody is current.
bool isFullUpload(int importedRows, int currentPlayers);

// Number of full exports that have happened since the player was last present.
// 0 when the player was in the most recent full export or any import after it
// (fresh). Never negative. Returns 0 while no full export has been recorded yet
// (currentCounter <= 0). Never-stamped legacy rows (lastSeenUpdate 0) count as
// last seen at upload 0, so they age like everyone else once tracking started.
int uploadsSinceSeen(const Player &player, int currentCounter);

// Data considered stale: missing from the last Y (>=1) full exports.
bool isStale(const Player &player, int currentCounter, int staleAfterUploads);

// Auto-retired: old enough (age >= X), stale (missing >= Y exports) and not in
// the user's own club. userClub may be empty (then no player is "own club").
bool isRetired(const Player &player, int currentCounter, int retirementAge,
               int staleAfterUploads, const QString &userClub);

} // namespace Freshness

} // namespace fm
