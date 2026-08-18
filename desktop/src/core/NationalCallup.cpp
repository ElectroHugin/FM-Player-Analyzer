#include "NationalCallup.h"

#include "Utils.h"

#include <algorithm>

namespace fm {

namespace NationalCallup {

bool isGoalkeeper(const Player &player)
{
    return parsePositionString(player.positionRaw).contains(QStringLiteral("GK"));
}

namespace {

// Tier rank for ordering: starting XI beats B-team beats everything else.
int tierRank(const QString &uid, const QSet<QString> &xiUids, const QSet<QString> &bUids)
{
    if (xiUids.contains(uid))
        return 0;
    if (bUids.contains(uid))
        return 1;
    return 2;
}

} // namespace

Recommendation recommend(const SquadBuilder &builder,
                         const std::vector<const Player *> &pool,
                         const QHash<QString, QString> &positions,
                         const QStringList &slotOrder, const RoleRatings &ratings,
                         int totalCount, int goalkeeperCount,
                         const std::vector<const Player *> &currentSquad)
{
    Recommendation result;
    if (totalCount < 1)
        return result;
    goalkeeperCount = std::clamp(goalkeeperCount, 0, totalCount);

    // One squad-builder pass over the whole eligible pool yields the best XI
    // (team A), the B-team and the remaining depth — the strength backbone the
    // recommendation is drawn from. National selection ignores club playing
    // time (apply_apt_weight = false), matching the national Best XI page.
    result.squad = builder.calculateSquadAndSurplus(pool, positions, slotOrder, ratings, false);

    QSet<QString> xiUids, bUids;
    for (const XiCell &cell : result.squad.startingXi)
        if (cell.isFilled())
            xiUids.insert(cell.playerUid);
    for (const XiCell &cell : result.squad.bTeam)
        if (cell.isFilled())
            bUids.insert(cell.playerUid);

    // Split the pool into goalkeepers and outfielders, each ranked by tier
    // (XI > B-team > depth) then by best DWRS across their roles. Ties fall
    // back to pool order via stable_sort.
    std::vector<const Player *> keepers, outfield;
    for (const Player *p : pool)
        (isGoalkeeper(*p) ? keepers : outfield).push_back(p);

    const auto rank = [&](const Player *p) {
        return tierRank(p->uid, xiUids, bUids);
    };
    const auto byTierThenRating = [&](const Player *a, const Player *b) {
        const int ra = rank(a), rb = rank(b);
        if (ra != rb)
            return ra < rb;
        return SquadBuilder::bestDwrsForPlayer(*a, ratings)
               > SquadBuilder::bestDwrsForPlayer(*b, ratings);
    };
    std::stable_sort(keepers.begin(), keepers.end(), byTierThenRating);
    std::stable_sort(outfield.begin(), outfield.end(), byTierThenRating);

    // Take up to G keepers; the rest of the N slots go to outfielders. When the
    // pool has fewer than G keepers, the freed slots are handed to outfielders
    // so the squad still reaches N.
    const int gkTake = std::min<int>(goalkeeperCount, static_cast<int>(keepers.size()));
    const int outfieldTake = std::min<int>(totalCount - gkTake,
                                           static_cast<int>(outfield.size()));
    result.goalkeepersRecommended = gkTake;

    result.recommended.reserve(static_cast<size_t>(gkTake + outfieldTake));
    for (int i = 0; i < gkTake; ++i)
        result.recommended.push_back(keepers[static_cast<size_t>(i)]);
    for (int i = 0; i < outfieldTake; ++i)
        result.recommended.push_back(outfield[static_cast<size_t>(i)]);

    for (const Player *p : result.recommended) {
        result.recommendedUids.insert(p->uid);
        const int r = rank(p);
        result.tierByUid.insert(p->uid, r == 0 ? QStringLiteral("A")
                                                : r == 1 ? QStringLiteral("B")
                                                         : QStringLiteral("D"));
    }

    // Diff against the current squad. Invites follow recommended order (best
    // first); drops follow the given current-squad order for a stable list and
    // include current players who fell out of the eligible pool entirely
    // (injured, aged out, nationality changed).
    QSet<QString> currentUids;
    for (const Player *p : currentSquad)
        currentUids.insert(p->uid);
    for (const Player *p : result.recommended) {
        if (!currentUids.contains(p->uid))
            result.invites.push_back(p);
    }
    for (const Player *p : currentSquad) {
        if (!result.recommendedUids.contains(p->uid))
            result.drops.push_back(p);
    }

    return result;
}

} // namespace NationalCallup

} // namespace fm
