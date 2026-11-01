#include "SigningAdvisor.h"

#include "Constants.h"
#include "Definitions.h"
#include "Utils.h"

#include <algorithm>

namespace fm {
namespace SigningAdvisor {

namespace {

// Gains below this are rounding noise, not an improvement.
constexpr double kMinGain = 0.01;

// Sum of the team's ratings; for a two-phase squad the players' values over
// both line-ups (SquadResult::valueOfUid).
double teamTotal(const QHash<QString, XiCell> &team, const QHash<QString, QString> &positions,
                 const RoleRatings &ratings, const SquadResult &squad)
{
    double total = 0.0;
    for (auto it = team.constBegin(); it != team.constEnd(); ++it) {
        if (!it->isFilled())
            continue;
        total += squad.twoPhase
                     ? squad.valueOfUid.value(it->playerUid, 0.0)
                     : ratings.value(positions.value(it.key())).value(it->playerUid, 0.0);
    }
    return total;
}

struct Place {
    QString slot; // empty = in neither team
    bool bTeam = false;
    bool inXi() const { return !slot.isEmpty() && !bTeam; }
};

QHash<QString, Place> placesOf(const SquadResult &squad)
{
    QHash<QString, Place> places;
    for (auto it = squad.startingXi.constBegin(); it != squad.startingXi.constEnd(); ++it) {
        if (it->isFilled())
            places.insert(it->playerUid, {it.key(), false});
    }
    for (auto it = squad.bTeam.constBegin(); it != squad.bTeam.constEnd(); ++it) {
        if (it->isFilled())
            places.insert(it->playerUid, {it.key(), true});
    }
    return places;
}

} // namespace

SigningAnalysis analyze(const SquadBuilder &builder, const std::vector<const Player *> &squad,
                        const std::vector<const Player *> &market,
                        const QHash<QString, QString> &positions, const QStringList &slotOrder,
                        const RoleRatings &ratings, int candidatesPerSlot, int optionsPerSlot,
                        const QString &tactic)
{
    SigningAnalysis result;
    result.marketSize = static_cast<int>(market.size());

    const SquadResult base = tactic.isEmpty()
        ? builder.calculateSquadAndSurplus(squad, positions, slotOrder, ratings)
        : builder.calculateSquadForTactic(squad, tactic, ratings);
    result.xiTotal = teamTotal(base.startingXi, positions, ratings, base);
    result.bTeamTotal = teamTotal(base.bTeam, positions, ratings, base);
    const QHash<QString, Place> basePlaces = placesOf(base);

    // --- Market per slot: comparison figures + the players worth simulating ---
    std::vector<QSet<QString>> marketPositions;
    marketPositions.reserve(market.size());
    for (const Player *player : market)
        marketPositions.push_back(parsePositionString(player->positionRaw));

    const auto &slotToGamePositions =
        tacticalSlotToGamePositions(builder.definitions().fmVersionId());
    std::vector<const Player *> candidates;
    QSet<const Player *> candidateSet;
    QHash<QString, size_t> slotIndex;
    for (const QString &slot : slotOrder) {
        SlotNeed need;
        need.slot = slot;
        need.role = positions.value(slot);
        need.current = base.startingXi.value(slot);
        const QHash<QString, double> roleRatings = ratings.value(need.role);
        if (need.current.isFilled())
            need.currentRating = roleRatings.value(need.current.playerUid, 0.0);

        const QStringList allowedList = slotToGamePositions.value(slot);
        const QSet<QString> allowed(allowedList.begin(), allowedList.end());
        std::vector<std::pair<double, const Player *>> rated;
        for (size_t i = 0; i < market.size(); ++i) {
            if (!marketPositions[i].intersects(allowed))
                continue;
            const auto it = roleRatings.constFind(market[i]->uid);
            if (it == roleRatings.constEnd() || it.value() <= 0.0)
                continue;
            rated.emplace_back(it.value(), market[i]);
            ++need.marketCount;
            need.marketBest = std::max(need.marketBest, it.value());
            if (!need.current.isFilled() || it.value() > need.currentRating)
                ++need.marketBetter;
        }
        // Stable: equal ratings keep the market order.
        std::stable_sort(rated.begin(), rated.end(),
                         [](const auto &a, const auto &b) { return a.first > b.first; });
        if (rated.size() > static_cast<size_t>(candidatesPerSlot))
            rated.resize(static_cast<size_t>(candidatesPerSlot));
        for (const auto &[rating, player] : rated) {
            if (!candidateSet.contains(player)) {
                candidateSet.insert(player);
                candidates.push_back(player);
            }
        }
        slotIndex.insert(slot, result.needs.size());
        result.needs.push_back(std::move(need));
    }

    // --- Simulate each signing ---
    std::vector<const Player *> pool = squad;
    pool.push_back(nullptr);
    for (const Player *candidate : candidates) {
        Player signing = *candidate;
        signing.agreedPlayingTime.clear(); // his new role in the squad is not agreed yet
        pool.back() = &signing;
        const SquadResult after =
            tactic.isEmpty()
                ? builder.calculateSquadAndSurplus(pool, positions, slotOrder, ratings)
                : builder.calculateSquadForTactic(pool, tactic, ratings);
        ++result.simulated;

        QString slot;
        for (auto it = after.startingXi.constBegin(); it != after.startingXi.constEnd(); ++it) {
            if (it->playerUid == signing.uid) {
                slot = it.key();
                break;
            }
        }
        if (slot.isEmpty())
            continue; // would not start
        SigningOption option;
        option.player = candidate;
        option.slot = slot;
        option.rating = after.twoPhase
                            ? after.valueOfUid.value(signing.uid, 0.0)
                            : ratings.value(positions.value(slot)).value(signing.uid, 0.0);
        option.xiGain = teamTotal(after.startingXi, positions, ratings, after) - result.xiTotal;
        option.bTeamGain = teamTotal(after.bTeam, positions, ratings, after) - result.bTeamTotal;
        if (option.xiGain < kMinGain)
            continue;

        // Knock-on effects for the XI; a switch between two slots of the same
        // role (DMCL <-> DMCR) is no real change.
        const QHash<QString, Place> afterPlaces = placesOf(after);
        for (const Player *player : squad) {
            const Place before = basePlaces.value(player->uid);
            const Place now = afterPlaces.value(player->uid);
            if (!before.inXi() && !now.inXi())
                continue;
            if (before.slot == now.slot && before.bTeam == now.bTeam)
                continue;
            if (before.inXi() && now.inXi()
                && positions.value(before.slot) == positions.value(now.slot))
                continue;
            option.moves.push_back({player, before.slot, before.bTeam, now.slot, now.bTeam});
        }
        result.needs[slotIndex.value(slot)].options.push_back(std::move(option));
    }

    for (SlotNeed &need : result.needs) {
        std::stable_sort(need.options.begin(), need.options.end(),
                         [](const SigningOption &a, const SigningOption &b) {
                             if (a.xiGain != b.xiGain)
                                 return a.xiGain > b.xiGain;
                             if (a.bTeamGain != b.bTeamGain)
                                 return a.bTeamGain > b.bTeamGain;
                             return a.player->transferValue < b.player->transferValue;
                         });
        if (need.options.size() > static_cast<size_t>(optionsPerSlot))
            need.options.resize(static_cast<size_t>(optionsPerSlot));
    }
    std::stable_sort(result.needs.begin(), result.needs.end(),
                     [](const SlotNeed &a, const SlotNeed &b) {
                         return a.bestGain() > b.bestGain();
                     });
    return result;
}

} // namespace SigningAdvisor
} // namespace fm
