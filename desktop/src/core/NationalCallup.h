#pragma once

#include "Player.h"
#include "SquadBuilder.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

namespace fm {

class Definitions;

// National-team call-up assistant. Given the eligible player pool (already
// filtered by nationality/age and with injured players removed) it computes the
// squad of `totalCount` players that most strengthens the national side, using
// the existing SquadBuilder "weakest link first" engine over the WHOLE pool
// (not just the current squad). The recommendation is then diffed against the
// current squad to produce invite/drop lists — the same keep/drop suggestion FM
// shows before an international window.
namespace NationalCallup {

// A player counts as a goalkeeper when his game positions include GK.
bool isGoalkeeper(const Player &player);

struct Recommendation {
    // Best XI (team A) and B-team over the full eligible pool, for display.
    SquadResult squad;

    // Final recommended squad: goalkeepers first, then outfielders by strength.
    std::vector<const Player *> recommended;
    QSet<QString> recommendedUids;

    // Deltas versus the current squad baseline.
    std::vector<const Player *> invites; // recommended, not currently called up
    std::vector<const Player *> drops;   // currently called up, not recommended

    // Tier of each recommended uid: "A" = starting XI, "B" = B-team,
    // "D" = additional depth. Drives the "why" shown next to each name.
    QHash<QString, QString> tierByUid;

    int goalkeepersRecommended = 0; // may be < requested if the pool lacks GKs
};

// pool must already be filtered to eligible, available players. positions and
// slotOrder come from the chosen national tactic; ratings are the normalized
// DWRS map. totalCount (N) and goalkeeperCount (G, an upper bound honored
// exactly when enough GKs exist) shape the squad. currentSquad is the baseline
// the invite/drop diff is measured against — passed as players (not just uids)
// so a currently called-up player who is now injured or ineligible (and thus
// absent from the pool) still surfaces as a drop.
Recommendation recommend(const SquadBuilder &builder,
                         const std::vector<const Player *> &pool,
                         const QHash<QString, QString> &positions,
                         const QStringList &slotOrder, const RoleRatings &ratings,
                         int totalCount, int goalkeeperCount,
                         const std::vector<const Player *> &currentSquad);

} // namespace NationalCallup

} // namespace fm
