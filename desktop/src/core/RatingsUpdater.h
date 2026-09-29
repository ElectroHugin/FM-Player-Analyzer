#pragma once

#include "Database.h"
#include "DwrsEngine.h"
#include "PlayerStore.h"
#include "SquadBuilder.h"

#include <QString>

#include <functional>

namespace fm {

// Port of legacy update_dwrs_ratings: recompute DWRS for players (all, or a
// subset) and append a new historical row only where the normalized value is
// new or changed by >= 1% — the append gate that keeps the history compact.
namespace RatingsUpdater {

struct Result {
    int computed = 0;  // ratings calculated
    int inserted = 0;  // rows actually appended (new or changed >= 1%)
    bool success = false;
    QString error;
};

// playersSubset: row indexes into players to restrict to (empty = all).
// progress(current, total) is invoked per role batch.
Result updateDwrsRatings(Database &db, const std::vector<Player> &players,
                         const DwrsEngine &engine, const QStringList &validRoles,
                         const std::vector<int> &playersSubset = {},
                         std::function<void(int, int)> progress = {});

// Uid-keyed normalized ratings (the squad engines' input) built from the latest
// DWRS rows, restricted to each player's CURRENTLY assigned roles. dwrs_latest
// keeps the last row of a role that was removed since (history is preserved);
// without this filter such a frozen rating would still place the player in that
// role in Best XI, gap analysis, call-ups and transfer suggestions.
RoleRatings roleRatingsForAssigned(const PlayerStore &store, const LatestRatings &latest);

// Incremental form of roleRatingsForAssigned for ONE changed player: drops his
// entries (under previousUid and his current uid) and re-adds those of his
// currently assigned roles from `latest`. Afterwards `ratings` holds the same
// values a full rebuild would (roles may keep an empty inner hash).
void patchRoleRatings(RoleRatings &ratings, const Player &player, const QString &previousUid,
                      const LatestRatings &latest);

} // namespace RatingsUpdater

} // namespace fm
