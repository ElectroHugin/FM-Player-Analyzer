#pragma once

#include "Player.h"

#include <QString>

namespace fm {

// Data-freshness derivation. Every full HTML import bumps a per-database upload
// counter (settings key "update_counter"); each imported player's
// lastSeenUpdate is stamped with that counter. Freshness is therefore a pure
// function of (current counter - player.lastSeenUpdate) — nothing is stored per
// player beyond the stamp, so changing the thresholds re-evaluates instantly.
//
// Thresholds (configurable in Settings):
//   retirementAge (X, default 35) and staleAfterUploads (Y, default 5).
namespace Freshness {

// Number of uploads that have happened since the player was last present.
// 0 when the player was in the most recent import (fresh). Never negative.
// Returns 0 while no upload has been recorded yet (currentCounter <= 0) or for
// never-stamped legacy rows, so nothing is flagged before real tracking data
// exists.
int uploadsSinceSeen(const Player &player, int currentCounter);

// Data considered stale: missing from the last Y (>=1) uploads.
bool isStale(const Player &player, int currentCounter, int staleAfterUploads);

// Auto-retired: old enough (age >= X), stale (missing >= Y uploads) and not in
// the user's own club. userClub may be empty (then no player is "own club").
bool isRetired(const Player &player, int currentCounter, int retirementAge,
               int staleAfterUploads, const QString &userClub);

} // namespace Freshness

} // namespace fm
