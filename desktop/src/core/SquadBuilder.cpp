#include "SquadBuilder.h"

#include "AppConfig.h"
#include "Assignment.h"
#include "MinCostFlow.h"
#include "Constants.h"
#include "Definitions.h"
#include "TalentEngine.h"
#include "Utils.h"

#include <algorithm>
#include <functional>
#include <numeric>
#include <memory>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace fm {

namespace {

struct Candidate {
    const Player *player = nullptr;
    double rating = 0.0;
    QString apt;
    double score = 0.0;
};

// A primary role pins the player to that role — but only while it is still
// assigned; a stale one must not lock him out everywhere.
bool pinnedToOtherRole(const Player &player, const QString &role)
{
    return !player.primaryRole.isEmpty() && player.primaryRole != role
           && player.assignedRoles.contains(player.primaryRole);
}

// Two-phase rule (FM26+): the pin only binds within the primary role's own
// phase. In the other phase the player stays free for any role. `phase` is the
// phase the role is played in ("ip", "oop" or "both" for a one-line-up role).
// Starting XI, B-team and depth options all go through this one rule.
bool pinnedOutInPhase(const Player &player, const QString &role, const QString &phase,
                      const Definitions &definitions)
{
    if (!pinnedToOtherRole(player, role))
        return false;
    const QString pinnedPhase = definitions.rolePhase(player.primaryRole);
    return pinnedPhase == phase || pinnedPhase == QLatin1String("both")
           || phase == QLatin1String("both");
}

// Legacy youth checks parse the TEXT age and fall back to 99 when it is not a
// plain digit string; our migrator stores unparseable ages as 0.
inline int effectiveAge(const Player &p)
{
    return p.age > 0 ? p.age : 99;
}

} // namespace

SquadBuilder::SquadBuilder(const Definitions &definitions, const AppConfig &config)
    : m_definitions(definitions)
    , m_config(config)
{
    reloadConfig();
}

void SquadBuilder::reloadConfig()
{
    m_maxDepthRoles = m_config.squadManagementSetting(QStringLiteral("max_roles_per_depth_player"));
    m_minLoanTalent = m_config.squadManagementSetting(QStringLiteral("min_loan_talent_score"));
    m_minLoanAge = m_config.squadManagementSetting(QStringLiteral("min_loan_age"));
    m_youthLoanOverAge = m_config.squadManagementSetting(QStringLiteral("youth_loan_over_age"));
    m_outfielderCap = m_config.ageThreshold(QStringLiteral("outfielder"));
    m_goalkeeperCap = m_config.ageThreshold(QStringLiteral("goalkeeper"));
    m_naturalPosMultiplier = m_config.selectionBonus(QStringLiteral("natural_position"));

    m_aptWeights.clear();
    for (const QString &apt : fieldPlayerAptOptions())
        m_aptWeights.insert(apt, m_config.aptWeight(apt));
    for (const QString &apt : gkAptOptions())
        m_aptWeights.insert(apt, m_config.aptWeight(apt));

    // role -> game positions it can be played from (reverse of
    // position_to_role_mapping), in the mapping's iteration order — only used
    // as a set membership test, so order is irrelevant here.
    m_roleToGamePositions.clear();
    const auto mapping = m_definitions.positionToRoleMapping();
    for (auto it = mapping.constBegin(); it != mapping.constEnd(); ++it) {
        for (const QString &role : it.value())
            m_roleToGamePositions[role].append(it.key());
    }
}

double SquadBuilder::bestDwrsForPlayer(const Player &player, const RoleRatings &ratings)
{
    double best = 0.0;
    for (const QString &role : player.assignedRoles) {
        const double rating = ratings.value(role).value(player.uid, 0.0);
        if (rating > best)
            best = rating;
    }
    return best;
}

SquadResult SquadBuilder::calculateSquadAndSurplus(const std::vector<const Player *> &pool,
                                                   const QHash<QString, QString> &positions,
                                                   const QStringList &slotOrder,
                                                   const RoleRatings &ratings,
                                                   bool applyAptWeight,
                                                   const XiLimit &xiLimit) const
{
    SquadResult result;

    // Parse every player's position string exactly once (legacy optimization,
    // and identical eligibility semantics).
    QHash<QString, QSet<QString>> parsedPositions;
    parsedPositions.reserve(static_cast<int>(pool.size()));
    for (const Player *p : pool)
        parsedPositions.insert(p->uid, parsePositionString(p->positionRaw));

    const auto &slotToGamePositions = tacticalSlotToGamePositions(m_definitions.fmVersionId());

    // All eligible candidates for one tactical slot, best score first.
    // std::stable_sort + pool order replicates Python's stable sorted().
    const auto buildCandidates = [&](const QString &slot, const QString &role,
                                     const std::vector<const Player *> &available) {
        const QStringList allowedList = slotToGamePositions.value(slot);
        const QSet<QString> allowed(allowedList.begin(), allowedList.end());
        const QStringList naturalGamePositions = m_roleToGamePositions.value(role);
        const QHash<QString, double> roleRatings = ratings.value(role);

        std::vector<Candidate> candidates;
        for (const Player *player : available) {
            const QSet<QString> &playerPositions = parsedPositions[player->uid];
            if (!playerPositions.intersects(allowed))
                continue;
            if (pinnedToOtherRole(*player, role))
                continue;
            const double rating = roleRatings.value(player->uid, 0.0);
            if (rating <= 0.0)
                continue;
            const QString apt = player->agreedPlayingTime.isEmpty()
                                    ? QStringLiteral("None")
                                    : player->agreedPlayingTime;
            const double aptWeight =
                applyAptWeight ? m_aptWeights.value(apt, 1.0) : 1.0;
            double naturalBonus = 1.0;
            for (const QString &np : player->naturalPositions) {
                if (naturalGamePositions.contains(np)) {
                    naturalBonus = m_naturalPosMultiplier;
                    break;
                }
            }
            candidates.push_back({player, rating, apt, rating * aptWeight * naturalBonus});
        }
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
        return candidates;
    };

    // --- Starting XI + B-team: one exact optimisation ---
    // Both teams are filled together as an assignment problem (every player at
    // most once, one player per slot) and solved exactly by the Hungarian
    // method (Assignment::solveMinCost) — a proven optimum, not a heuristic.
    // Priorities, strictly in this order:
    //   1. as many starting-XI slots filled as possible,
    //   2. the highest starting-XI total score,
    //   3. as many B-team slots filled as possible,
    //   4. the highest B-team total score.
    // The XI is never weakened for the B-team; among equally strong XIs the one
    // leaving the strongest B-team wins (a versatile player starts where his
    // absence from the B-team costs least).
    const int slotCount = static_cast<int>(slotOrder.size());
    const int rowCount = 2 * slotCount; // XI rows, then B-team rows

    // Exact pruning: in an optimal solution every slot holds one of its best
    // rowCount candidates — with only rowCount - 1 other slots competing, a
    // better unassigned one would always be left over to swap in.
    std::vector<std::vector<Candidate>> slotCandidates;
    slotCandidates.reserve(static_cast<size_t>(slotCount));
    std::vector<const Player *> columnPlayers;
    QHash<const Player *, int> columnOf;
    double maxScore = 0.0;
    for (const QString &slot : slotOrder) {
        std::vector<Candidate> cands = buildCandidates(slot, positions.value(slot), pool);
        if (cands.size() > static_cast<size_t>(rowCount))
            cands.resize(static_cast<size_t>(rowCount));
        for (const Candidate &c : cands) {
            if (!columnOf.contains(c.player)) {
                columnOf.insert(c.player, static_cast<int>(columnPlayers.size()));
                columnPlayers.push_back(c.player);
            }
            maxScore = std::max(maxScore, c.score);
        }
        slotCandidates.push_back(std::move(cands));
    }

    // Integer weights make the priorities exact: no amount of a lower level can
    // outweigh one step of a higher one. Scores are kept to 1e-4 (coarser only
    // if absurd weights would overflow 64 bits).
    double scale = 1e4;
    std::int64_t q = 0, bFill = 0, xiScore = 0, xiFill = 0;
    for (;;) {
        q = std::llround(maxScore * scale) + 1;    // > any single score
        bFill = slotCount * q + 1;                 // > any B-team total
        xiScore = slotCount * (bFill + q) + 1;     // > any B-team fill + total
        if (static_cast<double>(slotCount) * q * xiScore * slotCount < 1e18 || scale <= 1.0)
            break;
        scale /= 10.0;
    }
    xiFill = slotCount * q * xiScore + xiScore; // > any XI total + B-team part

    // Minimisation: cost = -value. Unlisted pairs (ineligible player) and the
    // spare columns cost 0, which is how a slot stays empty. `penalty` is
    // added to a limited player's XI value (see XiLimit); 0 = no limit.
    const int columnCount = static_cast<int>(columnPlayers.size()) + rowCount;
    std::vector<std::vector<std::int64_t>> cost;
    const auto buildCost = [&](std::int64_t penalty) {
        cost.assign(static_cast<size_t>(rowCount),
                    std::vector<std::int64_t>(static_cast<size_t>(columnCount), 0));
        for (int k = 0; k < slotCount; ++k) {
            for (const Candidate &c : slotCandidates[static_cast<size_t>(k)]) {
                const std::int64_t score = std::llround(c.score * scale);
                const size_t column = static_cast<size_t>(columnOf.value(c.player));
                const bool limited = penalty > 0 && xiLimit.uids.contains(c.player->uid);
                // A penalty beyond the fill reward leaves the slot empty.
                const std::int64_t xiValue = xiFill + score * xiScore - (limited ? penalty : 0);
                cost[static_cast<size_t>(k)][column] = xiValue > 0 ? -xiValue : 0;
                cost[static_cast<size_t>(slotCount + k)][column] = -(bFill + score);
            }
        }
    };
    const auto limitedInXi = [&](const std::vector<int> &a) {
        int count = 0;
        for (int row = 0; row < slotCount; ++row) {
            const int column = a[static_cast<size_t>(row)];
            if (column >= 0 && column < static_cast<int>(columnPlayers.size())
                && cost[static_cast<size_t>(row)][static_cast<size_t>(column)] != 0
                && xiLimit.uids.contains(columnPlayers[static_cast<size_t>(column)]->uid))
                ++count;
        }
        return count;
    };
    buildCost(0);
    std::vector<int> assignment = Assignment::solveMinCost(cost);
    if (xiLimit.active() && limitedInXi(assignment) > xiLimit.max) {
        // Smallest penalty that keeps the cap (Lagrangian relaxation).
        std::int64_t lo = 0, hi = xiFill + (q + 1) * xiScore + 1;
        std::vector<int> best;
        while (lo + 1 < hi) {
            const std::int64_t mid = lo + (hi - lo) / 2;
            buildCost(mid);
            std::vector<int> trial = Assignment::solveMinCost(cost);
            if (limitedInXi(trial) <= xiLimit.max) {
                hi = mid;
                best = std::move(trial);
            } else {
                lo = mid;
            }
        }
        buildCost(hi);
        assignment = best.empty() ? Assignment::solveMinCost(cost) : std::move(best);
    }

    QHash<QString, XiCell> xi;
    QHash<QString, XiCell> bTeam;
    QSet<QString> takenUids;
    for (int row = 0; row < rowCount; ++row) {
        const int column = assignment[static_cast<size_t>(row)];
        if (column < 0 || column >= static_cast<int>(columnPlayers.size())
            || cost[static_cast<size_t>(row)][static_cast<size_t>(column)] == 0)
            continue; // slot left empty
        const int k = row % slotCount;
        const Player *player = columnPlayers[static_cast<size_t>(column)];
        for (const Candidate &c : slotCandidates[static_cast<size_t>(k)]) {
            if (c.player != player)
                continue;
            XiCell cell;
            cell.playerUid = player->uid;
            cell.name = player->name;
            cell.rating = std::trunc(c.rating); // legacy: f"{int(rating)}%"
            cell.apt = c.apt;
            (row < slotCount ? xi : bTeam).insert(slotOrder[k], cell);
            takenUids.insert(player->uid);
            break;
        }
    }
    std::vector<const Player *> depthPool;
    for (const Player *p : pool) {
        if (!takenUids.contains(p->uid))
            depthPool.push_back(p);
    }

    // Footedness swaps on symmetrical same-role slot pairs; only EXPLICIT
    // preferred_side counts, swap only on strict improvement.
    const auto applyFootednessSwaps = [&](QHash<QString, XiCell> &team) {
        static const QList<QPair<QString, QString>> symmetricalPairs = {
            {QStringLiteral("DCL"), QStringLiteral("DCR")},
            {QStringLiteral("DMCL"), QStringLiteral("DMCR")},
            {QStringLiteral("MCL"), QStringLiteral("MCR")},
            {QStringLiteral("AMCL"), QStringLiteral("AMCR")},
            {QStringLiteral("STL"), QStringLiteral("STR")},
        };
        QHash<QString, const Player *> playerByUid;
        for (const Player *p : pool)
            playerByUid.insert(p->uid, p);

        const auto explicitSide = [](const Player *p) -> QString {
            if (p && (p->preferredSide == QLatin1String("Left")
                      || p->preferredSide == QLatin1String("Right")))
                return p->preferredSide;
            return QString();
        };

        for (const auto &[slotL, slotR] : symmetricalPairs) {
            if (!team.contains(slotL) || !team.contains(slotR))
                continue;
            if (positions.value(slotL) != positions.value(slotR))
                continue;
            const Player *playerL = playerByUid.value(team[slotL].playerUid);
            const Player *playerR = playerByUid.value(team[slotR].playerUid);
            if (!playerL || !playerR)
                continue;
            const QString prefL = explicitSide(playerL);
            const QString prefR = explicitSide(playerR);
            const int satisfiedNow = int(prefL == QLatin1String("Left"))
                                     + int(prefR == QLatin1String("Right"));
            const int satisfiedAfter = int(prefL == QLatin1String("Right"))
                                       + int(prefR == QLatin1String("Left"));
            if (satisfiedAfter > satisfiedNow)
                std::swap(team[slotL], team[slotR]);
        }
    };
    applyFootednessSwaps(xi);
    applyFootednessSwaps(bTeam);

    // Fill unassigned slots with the default placeholder cell.
    for (const QString &slot : slotOrder) {
        if (!xi.contains(slot))
            xi.insert(slot, XiCell());
        if (!bTeam.contains(slot))
            bTeam.insert(slot, XiCell());
    }

    // --- Smart depth calculation ---
    QSet<QString> xiOrBTeamUids;
    for (const XiCell &cell : std::as_const(xi)) {
        if (cell.isFilled())
            xiOrBTeamUids.insert(cell.playerUid);
    }
    for (const XiCell &cell : std::as_const(bTeam)) {
        if (cell.isFilled())
            xiOrBTeamUids.insert(cell.playerUid);
    }

    QStringList uniqueRoles;
    for (const QString &slot : slotOrder) {
        const QString role = positions.value(slot);
        if (!uniqueRoles.contains(role))
            uniqueRoles.append(role);
    }
    fillDepthOptions(pool, xiOrBTeamUids, uniqueRoles, ratings, result);

    result.startingXi = std::move(xi);
    result.bTeam = std::move(bTeam);
    result.depthPool = std::move(depthPool);
    result.coreSquadUids = xiOrBTeamUids + result.depthPlayerUids;
    return result;
}

void SquadBuilder::fillDepthOptions(const std::vector<const Player *> &pool,
                                    const QSet<QString> &xiOrBTeamUids, const QStringList &roles,
                                    const RoleRatings &ratings, SquadResult &result) const
{
    std::vector<const Player *> availableDepth;
    for (const Player *p : pool) {
        if (!xiOrBTeamUids.contains(p->uid))
            availableDepth.push_back(p);
    }

    QHash<QString, QSet<QString>> rolesCoveredByPlayer;
    if (!availableDepth.empty()) {
        QStringList uniqueRoles = roles;
        std::sort(uniqueRoles.begin(), uniqueRoles.end());

        for (const QString &role : uniqueRoles) {
            const QHash<QString, double> roleRatings = ratings.value(role);
            std::vector<const Player *> sortedCandidates = availableDepth;
            // Same pin as the team selection: no depth option for another role
            // of the primary role's phase (a one-line-up role is "both", so
            // there the pin covers every other role as before).
            const QString phase = m_definitions.rolePhase(role);
            std::erase_if(sortedCandidates, [&](const Player *p) {
                return pinnedOutInPhase(*p, role, phase, m_definitions);
            });
            std::stable_sort(sortedCandidates.begin(), sortedCandidates.end(),
                             [&](const Player *a, const Player *b) {
                                 return roleRatings.value(a->uid, -1.0)
                                        > roleRatings.value(b->uid, -1.0);
                             });

            const Player *bestPick = nullptr;
            for (const Player *candidate : sortedCandidates) {
                const QSet<QString> covered = rolesCoveredByPlayer.value(candidate->uid);
                if (covered.contains(role) || covered.size() < m_maxDepthRoles) {
                    bestPick = candidate;
                    break;
                }
            }
            if (!bestPick && !sortedCandidates.empty())
                bestPick = sortedCandidates.front();

            if (bestPick) {
                const double rating = roleRatings.value(bestPick->uid, 0.0);
                if (rating > 0.0) {
                    DepthOption option;
                    option.playerUid = bestPick->uid;
                    option.name = bestPick->name;
                    option.rating = std::trunc(rating);
                    option.apt = bestPick->agreedPlayingTime;
                    option.age = bestPick->age;
                    result.bestDepthOptions[role] = {option};
                    result.depthPlayerUids.insert(bestPick->uid);
                    rolesCoveredByPlayer[bestPick->uid].insert(role);
                }
            }
        }
    }

}

SquadResult SquadBuilder::calculateSquadForTactic(const std::vector<const Player *> &pool,
                                                  const QString &tactic,
                                                  const RoleRatings &ratings,
                                                  bool applyAptWeight,
                                                  const Bridges &fixedBridges,
                                                  const XiLimit &xiLimit) const
{
    if (!m_definitions.isTwoPhaseTactic(tactic)) {
        return calculateSquadAndSurplus(pool, m_definitions.tacticRoles().value(tactic),
                                        m_definitions.tacticSlotOrder(tactic), ratings,
                                        applyAptWeight, xiLimit);
    }
    const TwoPhaseSquadResult twoPhase =
        calculateTwoPhaseSquad(pool, tactic, ratings, m_config.ipSharePercent() / 100.0,
                               applyAptWeight, fixedBridges, xiLimit);

    SquadResult result;
    result.twoPhase = true;
    result.valueOfUid = twoPhase.valueOfUid;
    result.slotsOfUid = twoPhase.slotsOfUid;
    // Slots of the line-up without ball (the base shape); the rating is the
    // player's value over both line-ups, so totals and averages compare
    // players and tactics fairly.
    QSet<QString> xiOrBTeamUids;
    const auto combine = [&](const QHash<QString, XiCell> &team, QHash<QString, XiCell> &out) {
        for (auto it = team.constBegin(); it != team.constEnd(); ++it) {
            XiCell cell = it.value();
            if (cell.isFilled()) {
                cell.rating = std::trunc(twoPhase.valueOfUid.value(cell.playerUid, cell.rating));
                xiOrBTeamUids.insert(cell.playerUid);
            }
            out.insert(it.key(), cell);
        }
    };
    combine(twoPhase.startingOop, result.startingXi);
    combine(twoPhase.bTeamOop, result.bTeam);

    const Definitions::TwoPhaseTactic &lineUps = m_definitions.twoPhaseTactics().value(tactic);
    QStringList roles;
    for (const QString &slot : lineUps.ipOrder)
        roles.append(lineUps.ip.value(slot));
    for (const QString &slot : lineUps.oopOrder)
        roles.append(lineUps.oop.value(slot));
    roles.removeDuplicates();
    fillDepthOptions(pool, xiOrBTeamUids, roles, ratings, result);

    result.depthPool = twoPhase.depthPool;
    result.coreSquadUids = xiOrBTeamUids + result.depthPlayerUids;
    return result;
}

DevelopmentSquads SquadBuilder::calculateDevelopmentSquads(
    const std::vector<const Player *> &secondTeamPlayers,
    const std::vector<const Player *> &firstTeamRemnants,
    const QHash<QString, QString> &positions, const QStringList &slotOrder,
    const RoleRatings &ratings, const QSet<QString> &depthPlayerUids) const
{
    DevelopmentSquads result;

    // 1. Second team XI (falls back to the remnant pool without a second club).
    const std::vector<const Player *> &secondPool =
        !secondTeamPlayers.empty() ? secondTeamPlayers : firstTeamRemnants;
    const SquadResult secondSquad =
        calculateSquadAndSurplus(secondPool, positions, slotOrder, ratings);
    result.secondTeamXi = secondSquad.startingXi;

    QSet<QString> secondTeamUids;
    for (const XiCell &cell : std::as_const(result.secondTeamXi)) {
        if (cell.isFilled())
            secondTeamUids.insert(cell.playerUid);
    }

    // 2. Youth XI from young remnants not already in the second XI.
    std::vector<const Player *> youthPool;
    for (const Player *p : firstTeamRemnants) {
        if (secondTeamUids.contains(p->uid))
            continue;
        const int age = effectiveAge(*p);
        const int cap = p->isGoalkeeper() ? m_goalkeeperCap : m_outfielderCap;
        if (age <= cap)
            youthPool.push_back(p);
    }
    const SquadResult youthSquad =
        calculateSquadAndSurplus(youthPool, positions, slotOrder, ratings);
    result.youthXi = youthSquad.startingXi;

    QSet<QString> youthUids;
    for (const XiCell &cell : std::as_const(result.youthXi)) {
        if (cell.isFilled())
            youthUids.insert(cell.playerUid);
    }

    // 3. Definitive surplus: in neither XI and not kept as first-team depth.
    std::vector<const Player *> surplus;
    for (const Player *p : firstTeamRemnants) {
        if (!secondTeamUids.contains(p->uid) && !youthUids.contains(p->uid)
            && !depthPlayerUids.contains(p->uid))
            surplus.push_back(p);
    }

    // 4. Categorize surplus: young + promising -> loan, everyone else -> sell.
    //    A hard age floor applies: players below m_minLoanAge are never loaned
    //    out. Promising ones below it are simply kept without a suggestion;
    //    only genuinely unremarkable players are put up for sale.
    std::vector<std::pair<double, const Player *>> loanWithTalent;
    for (const Player *p : surplus) {
        const int age = effectiveAge(*p);
        const bool gk = p->isGoalkeeper();
        const int cap = gk ? m_goalkeeperCap : m_outfielderCap;
        const bool young = age <= cap;
        if (!young) {
            result.sellCandidates.push_back(p);
            continue;
        }
        const double bestDwrs = bestDwrsForPlayer(*p, ratings);
        const double talent = TalentEngine::talentForPlayer(m_definitions, *p, bestDwrs, cap);
        if (talent >= m_minLoanTalent) {
            if (age >= m_minLoanAge)
                loanWithTalent.push_back({talent, p});
            // else: promising but too young to loan -> keep, no suggestion.
        } else {
            result.sellCandidates.push_back(p);
        }
    }

    // 4b. Youth-XI regulars older than m_youthLoanOverAge who are not kept as
    //     first-team depth should not stagnate without minutes: propose the
    //     promising ones for loan too. This extends the loan list — the surplus
    //     loan candidates above stay in place, no one is removed.
    for (const Player *p : firstTeamRemnants) {
        if (!youthUids.contains(p->uid) || depthPlayerUids.contains(p->uid))
            continue;
        const int age = effectiveAge(*p);
        if (age <= m_youthLoanOverAge) // strictly older ("über 18")
            continue;
        const bool gk = p->isGoalkeeper();
        const int cap = gk ? m_goalkeeperCap : m_outfielderCap;
        const double bestDwrs = bestDwrsForPlayer(*p, ratings);
        const double talent = TalentEngine::talentForPlayer(m_definitions, *p, bestDwrs, cap);
        if (talent >= m_minLoanTalent)
            loanWithTalent.push_back({talent, p});
    }

    std::stable_sort(loanWithTalent.begin(), loanWithTalent.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });
    for (const auto &[talent, p] : loanWithTalent)
        result.loanCandidates.push_back(p);

    std::stable_sort(result.sellCandidates.begin(), result.sellCandidates.end(),
                     [](const Player *a, const Player *b) {
                         return getLastName(a->name) < getLastName(b->name);
                     });

    return result;
}

TwoPhaseSquadResult SquadBuilder::calculateTwoPhaseSquad(const std::vector<const Player *> &pool,
                                                         const QString &tactic,
                                                         const RoleRatings &ratings,
                                                         double ipShare,
                                                         bool applyAptWeight,
                                                         const QHash<QString, QPair<QString, QString>> &fixedBridges,
                                                         const XiLimit &xiLimit) const
{
    TwoPhaseSquadResult result;
    const auto tacticIt = m_definitions.twoPhaseTactics().constFind(tactic);
    if (tacticIt == m_definitions.twoPhaseTactics().constEnd()) {
        result.depthPool = pool;
        return result;
    }
    const Definitions::TwoPhaseTactic &lineUps = tacticIt.value();
    ipShare = std::clamp(ipShare, 0.0, 1.0);

    QHash<QString, QSet<QString>> parsedPositions;
    for (const Player *p : pool)
        parsedPositions.insert(p->uid, parsePositionString(p->positionRaw));
    const auto &slotToGamePositions = tacticalSlotToGamePositions(m_definitions.fmVersionId());

    // Pinned to his primary role: in that role's phase the player may only
    // take slots of exactly that role.
    const auto pinnedOut = [&](const Player &player, const QString &role, const QString &phase) {
        return pinnedOutInPhase(player, role, phase, m_definitions);
    };

    struct Option {
        double rating = 0.0; // DWRS of the slot's role
        double score = 0.0;  // rating x playing-time weight x natural bonus
        QString apt;
    };
    // Game positions per slot, built once per call (not per player).
    QHash<QString, QStringList> allowedBySlot;
    for (const QStringList *order : {&lineUps.ipOrder, &lineUps.oopOrder}) {
        for (const QString &slot : *order) {
            if (!allowedBySlot.contains(slot))
                allowedBySlot.insert(slot, slot == QLatin1String("GK")
                                               ? QStringList{QStringLiteral("GK")}
                                               : slotToGamePositions.value(slot));
        }
    }
    // Score of `player` in `slot` (role from `lineUp`), or nothing if ineligible.
    const auto option = [&](const Player &player, const QString &slot,
                            const QHash<QString, QString> &lineUp,
                            const QString &phase) -> std::optional<Option> {
        const QSet<QString> &own = parsedPositions[player.uid];
        const QStringList &allowed = allowedBySlot[slot];
        if (std::none_of(allowed.cbegin(), allowed.cend(),
                         [&](const QString &position) { return own.contains(position); }))
            return std::nullopt;
        const QString role = lineUp.value(slot);
        if (pinnedOut(player, role, phase))
            return std::nullopt;
        const double rating = ratings.value(role).value(player.uid, 0.0);
        if (rating <= 0.0)
            return std::nullopt;
        Option o;
        o.rating = rating;
        o.apt = player.agreedPlayingTime.isEmpty() ? QStringLiteral("None")
                                                   : player.agreedPlayingTime;
        const double aptWeight = applyAptWeight ? m_aptWeights.value(o.apt, 1.0) : 1.0;
        double naturalBonus = 1.0;
        const QStringList naturalGamePositions = m_roleToGamePositions.value(role);
        for (const QString &np : player.naturalPositions) {
            if (naturalGamePositions.contains(np)) {
                naturalBonus = m_naturalPosMultiplier;
                break;
            }
        }
        o.score = rating * aptWeight * naturalBonus;
        return o;
    };

    // One team: min-cost flow source -> ip slot -> player -> oop slot -> sink.
    // Each unit of flow places one player in both line-ups. A fixed reward per
    // placed player (split over his two edges) outweighs any score, so the
    // most players are placed first, then the value is maximal.
    // Manual bridges (starting XI only): a fixed player may take only his
    // fixed slot in that phase, nobody else may take it.
    using Bridges = QHash<QString, QPair<QString, QString>>;
    const auto pickTeam = [&](const std::vector<const Player *> &available,
                              QHash<QString, XiCell> &ipTeam, QHash<QString, XiCell> &oopTeam,
                              const Bridges &fixed, const XiLimit &limit) {
        QSet<QString> fixedIp, fixedOop;
        for (auto it = fixed.cbegin(); it != fixed.cend(); ++it) {
            if (!it->first.isEmpty())
                fixedIp.insert(it->first);
            if (!it->second.isEmpty())
                fixedOop.insert(it->second);
        }
        const auto allowed = [&](const Player &player, const QString &slot, bool ipPhase) {
            const auto own = fixed.constFind(player.uid);
            const QString mine = own == fixed.cend() ? QString()
                                                     : (ipPhase ? own->first : own->second);
            if (!mine.isEmpty())
                return slot == mine;
            return !(ipPhase ? fixedIp : fixedOop).contains(slot);
        };
        const QStringList &ipSlots = lineUps.ipOrder;
        const QStringList &oopSlots = lineUps.oopOrder;
        const int nIp = static_cast<int>(ipSlots.size());
        const int nOop = static_cast<int>(oopSlots.size());
        const int nPlayers = static_cast<int>(available.size());
        // Nodes: 0 source, 1 sink, ip slots, player in, player out, oop slots.
        const int ipBase = 2;
        const int inBase = ipBase + nIp;
        const int outBase = inBase + nPlayers;
        const int oopBase = outBase + nPlayers;

        constexpr double kScale = 1e4;
        // > the largest possible total of scaled scores (11 x 2 x 100 x weights).
        double maxScore = 0.0;
        std::vector<std::vector<std::optional<Option>>> ipOpt(static_cast<size_t>(nPlayers)),
            oopOpt(static_cast<size_t>(nPlayers));
        for (int p = 0; p < nPlayers; ++p) {
            const Player &player = *available[static_cast<size_t>(p)];
            for (const QString &slot : ipSlots) {
                ipOpt[static_cast<size_t>(p)].push_back(
                    allowed(player, slot, true)
                        ? option(player, slot, lineUps.ip, QStringLiteral("ip"))
                        : std::nullopt);
                if (ipOpt[static_cast<size_t>(p)].back())
                    maxScore = std::max(maxScore, ipOpt[static_cast<size_t>(p)].back()->score);
            }
            for (const QString &slot : oopSlots) {
                oopOpt[static_cast<size_t>(p)].push_back(
                    allowed(player, slot, false)
                        ? option(player, slot, lineUps.oop, QStringLiteral("oop"))
                        : std::nullopt);
                if (oopOpt[static_cast<size_t>(p)].back())
                    maxScore = std::max(maxScore, oopOpt[static_cast<size_t>(p)].back()->score);
            }
        }
        const std::int64_t halfReward =
            std::llround(maxScore * kScale) * std::max(nIp, nOop) + 1;

        // Builds and solves the flow; `penalty` is charged per placed player of
        // the limited group (XiLimit), 0 = no limit.
        struct Solved {
            std::unique_ptr<MinCostFlow> flow;
            std::vector<std::vector<int>> ipEdge, oopEdge;
        };
        const auto solveWith = [&](std::int64_t penalty) {
            Solved solved;
            solved.flow = std::make_unique<MinCostFlow>(oopBase + nOop);
            MinCostFlow &flow = *solved.flow;
            std::vector<std::vector<int>> &ipEdge = solved.ipEdge;
            std::vector<std::vector<int>> &oopEdge = solved.oopEdge;
            for (int k = 0; k < nIp; ++k)
                flow.addEdge(0, ipBase + k, 1, 0);
            for (int j = 0; j < nOop; ++j)
                flow.addEdge(oopBase + j, 1, 1, 0);
            ipEdge.assign(static_cast<size_t>(nPlayers), std::vector<int>(static_cast<size_t>(nIp), -1));
            oopEdge.assign(static_cast<size_t>(nPlayers), std::vector<int>(static_cast<size_t>(nOop), -1));
            for (int p = 0; p < nPlayers; ++p) {
                bool anyIp = false;
                bool anyOop = false;
                for (int k = 0; k < nIp; ++k)
                    anyIp |= ipOpt[static_cast<size_t>(p)][static_cast<size_t>(k)].has_value();
                for (int j = 0; j < nOop; ++j)
                    anyOop |= oopOpt[static_cast<size_t>(p)][static_cast<size_t>(j)].has_value();
                if (!anyIp || !anyOop)
                    continue; // must fit both line-ups
                const bool limited =
                    penalty > 0 && limit.uids.contains(available[static_cast<size_t>(p)]->uid);
                flow.addEdge(inBase + p, outBase + p, 1, limited ? penalty : 0);
                for (int k = 0; k < nIp; ++k) {
                    if (const auto &o = ipOpt[static_cast<size_t>(p)][static_cast<size_t>(k)]) {
                        ipEdge[static_cast<size_t>(p)][static_cast<size_t>(k)] = flow.addEdge(
                            ipBase + k, inBase + p, 1,
                            -(halfReward + std::llround(ipShare * o->score * kScale)));
                    }
                }
                for (int j = 0; j < nOop; ++j) {
                    if (const auto &o = oopOpt[static_cast<size_t>(p)][static_cast<size_t>(j)]) {
                        oopEdge[static_cast<size_t>(p)][static_cast<size_t>(j)] = flow.addEdge(
                            outBase + p, oopBase + j, 1,
                            -(halfReward + std::llround((1.0 - ipShare) * o->score * kScale)));
                    }
                }
            }

            flow.solve(0, 1, std::min(nIp, nOop));
            return solved;
        };
        const auto limitedPlaced = [&](const Solved &solved) {
            int count = 0;
            for (int p = 0; p < nPlayers; ++p) {
                if (!limit.uids.contains(available[static_cast<size_t>(p)]->uid))
                    continue;
                for (const int e : solved.ipEdge[static_cast<size_t>(p)])
                    if (e >= 0 && solved.flow->flowOn(e) > 0)
                        ++count;
            }
            return count;
        };
        Solved solved = solveWith(0);
        if (limit.active() && limitedPlaced(solved) > limit.max) {
            // Smallest penalty that keeps the cap (Lagrangian relaxation); at
            // the upper end placing a limited player is never worth it.
            std::int64_t lo = 0;
            std::int64_t hi = 2 * halfReward + 2 * (std::llround(maxScore * kScale) + 1);
            Solved best;
            while (lo + 1 < hi) {
                const std::int64_t mid = lo + (hi - lo) / 2;
                Solved trial = solveWith(mid);
                if (limitedPlaced(trial) <= limit.max) {
                    hi = mid;
                    best = std::move(trial);
                } else {
                    lo = mid;
                }
            }
            solved = best.flow ? std::move(best) : solveWith(hi);
        }
        const MinCostFlow &flow = *solved.flow;
        const std::vector<std::vector<int>> &ipEdge = solved.ipEdge;
        const std::vector<std::vector<int>> &oopEdge = solved.oopEdge;

        QSet<QString> taken;
        for (int p = 0; p < nPlayers; ++p) {
            const Player &player = *available[static_cast<size_t>(p)];
            int ipK = -1;
            int oopJ = -1;
            for (int k = 0; k < nIp; ++k) {
                const int e = ipEdge[static_cast<size_t>(p)][static_cast<size_t>(k)];
                if (e >= 0 && flow.flowOn(e) > 0)
                    ipK = k;
            }
            for (int j = 0; j < nOop; ++j) {
                const int e = oopEdge[static_cast<size_t>(p)][static_cast<size_t>(j)];
                if (e >= 0 && flow.flowOn(e) > 0)
                    oopJ = j;
            }
            if (ipK < 0 || oopJ < 0)
                continue;
            const Option &ipO = *ipOpt[static_cast<size_t>(p)][static_cast<size_t>(ipK)];
            const Option &oopO = *oopOpt[static_cast<size_t>(p)][static_cast<size_t>(oopJ)];
            const auto cell = [&](const Option &o) {
                XiCell c;
                c.playerUid = player.uid;
                c.name = player.name;
                c.rating = std::trunc(o.rating);
                c.apt = o.apt;
                return c;
            };
            ipTeam.insert(ipSlots[ipK], cell(ipO));
            oopTeam.insert(oopSlots[oopJ], cell(oopO));
            result.slotsOfUid.insert(player.uid, {ipSlots[ipK], oopSlots[oopJ]});
            result.valueOfUid.insert(player.uid,
                                     ipShare * ipO.rating + (1.0 - ipShare) * oopO.rating);
            taken.insert(player.uid);
        }
        for (const QString &slot : ipSlots) {
            if (!ipTeam.contains(slot))
                ipTeam.insert(slot, XiCell());
        }
        for (const QString &slot : oopSlots) {
            if (!oopTeam.contains(slot))
                oopTeam.insert(slot, XiCell());
        }
        std::vector<const Player *> rest;
        for (const Player *p : available) {
            if (!taken.contains(p->uid))
                rest.push_back(p);
        }
        return rest;
    };

    const std::vector<const Player *> afterXi =
        pickTeam(pool, result.startingIp, result.startingOop, fixedBridges, xiLimit);
    result.depthPool = pickTeam(afterXi, result.bTeamIp, result.bTeamOop, {}, {});
    return result;
}

double SquadBuilder::startingValue(const TwoPhaseSquadResult &squad)
{
    double total = 0.0;
    for (const XiCell &cell : squad.startingIp) {
        if (cell.isFilled())
            total += squad.valueOfUid.value(cell.playerUid);
    }
    return total;
}

QList<OutageCover> SquadBuilder::replacementPlans(const std::vector<const Player *> &pool,
                                                  const QString &tactic,
                                                  const RoleRatings &ratings, bool applyAptWeight,
                                                  const Bridges &fixedBridges,
                                                  const XiLimit &xiLimit, int maxPlans) const
{
    QList<OutageCover> covers;
    const bool twoPhase = m_definitions.isTwoPhaseTactic(tactic);
    if (!twoPhase && !m_definitions.tacticRoles().contains(tactic))
        return covers;
    const double ipShare = std::clamp(m_config.ipSharePercent() / 100.0, 0.0, 1.0);
    const QHash<QString, QString> positions = m_definitions.tacticRoles().value(tactic);
    const QStringList slotOrder = twoPhase ? m_definitions.twoPhaseTactics().value(tactic).ipOrder
                                           : m_definitions.tacticSlotOrder(tactic);

    // The starting XI of a pool: places, values and the total.
    struct Xi {
        QHash<QString, QPair<QString, QString>> places;
        QHash<QString, double> value;
        double total = 0.0;
    };
    const auto startingXi = [&](const std::vector<const Player *> &players,
                                const Bridges &fixed) {
        Xi xi;
        if (twoPhase) {
            const TwoPhaseSquadResult r = calculateTwoPhaseSquad(
                players, tactic, ratings, ipShare, applyAptWeight, fixed, xiLimit);
            for (const XiCell &cell : r.startingIp) {
                if (!cell.isFilled())
                    continue;
                xi.places.insert(cell.playerUid, r.slotsOfUid.value(cell.playerUid));
                xi.value.insert(cell.playerUid, r.valueOfUid.value(cell.playerUid));
            }
            xi.total = startingValue(r);
        } else {
            const SquadResult r = calculateSquadAndSurplus(players, positions, slotOrder,
                                                           ratings, applyAptWeight, xiLimit);
            for (auto it = r.startingXi.cbegin(); it != r.startingXi.cend(); ++it) {
                if (!it->isFilled())
                    continue;
                xi.places.insert(it->playerUid, {it.key(), it.key()});
                xi.value.insert(it->playerUid, it->rating);
                xi.total += it->rating;
            }
        }
        return xi;
    };
    // Manual bridges only bind players who are still in the pool.
    const auto bridgesFor = [&](const QSet<QString> &uids) {
        Bridges kept;
        for (auto it = fixedBridges.cbegin(); it != fixedBridges.cend(); ++it) {
            if (uids.contains(it.key()))
                kept.insert(it.key(), it.value());
        }
        return kept;
    };

    const Xi current = startingXi(pool, twoPhase ? fixedBridges : Bridges());
    std::vector<const Player *> bench;
    for (const Player *p : pool) {
        if (!current.places.contains(p->uid))
            bench.push_back(p);
    }
    // Upper bound of a player's value in any place of the tactic (his best
    // role DWRS per line-up over the slots his positions allow): no plan
    // can beat the sum.
    QHash<QString, double> upperBound;
    {
        const auto &slotToGamePositions = tacticalSlotToGamePositions(m_definitions.fmVersionId());
        QHash<QString, QSet<QString>> allowedOf;
        const auto bestOf = [&](const QString &uid, const QSet<QString> &own,
                                const QHash<QString, QString> &lineUp) {
            double best = 0.0;
            for (auto it = lineUp.cbegin(); it != lineUp.cend(); ++it) {
                auto allowed = allowedOf.constFind(it.key());
                if (allowed == allowedOf.cend()) {
                    const QStringList list = it.key() == QLatin1String("GK")
                                                 ? QStringList{QStringLiteral("GK")}
                                                 : slotToGamePositions.value(it.key());
                    allowed = allowedOf.insert(it.key(), QSet<QString>(list.begin(), list.end()));
                }
                if (own.intersects(*allowed))
                    best = std::max(best, ratings.value(it.value()).value(uid, 0.0));
            }
            return best;
        };
        const auto &lineUps = m_definitions.twoPhaseTactics().value(tactic);
        for (const Player *p : pool) {
            const QSet<QString> own = parsePositionString(p->positionRaw);
            upperBound.insert(p->uid,
                              twoPhase ? ipShare * bestOf(p->uid, own, lineUps.ip)
                                             + (1.0 - ipShare) * bestOf(p->uid, own, lineUps.oop)
                                       : bestOf(p->uid, own, positions));
        }
    }

    // Starters in pitch order of the line-up with ball.
    QStringList outOrder;
    for (const QString &slot : slotOrder) {
        for (auto it = current.places.cbegin(); it != current.places.cend(); ++it) {
            if (it->first == slot && !outOrder.contains(it.key()))
                outOrder.append(it.key());
        }
    }

    for (const QString &outUid : std::as_const(outOrder)) {
        OutageCover cover;
        cover.outUid = outUid;
        cover.outSlots = current.places.value(outUid);
        cover.outValue = current.value.value(outUid);
        cover.current = current.total;

        QSet<QString> rest;
        for (auto it = current.places.cbegin(); it != current.places.cend(); ++it) {
            if (it.key() != outUid)
                rest.insert(it.key());
        }
        const auto planFrom = [&](const Xi &xi, const QString &inUid) {
            ReplacementPlan plan;
            plan.inUid = inUid;
            plan.total = xi.total;
            if (!inUid.isEmpty()) {
                plan.inSlots = xi.places.value(inUid);
                plan.inValue = xi.value.value(inUid);
            }
            for (const QString &uid : std::as_const(outOrder)) {
                if (!rest.contains(uid))
                    continue;
                const auto from = current.places.value(uid);
                const auto to = xi.places.value(uid);
                if (from != to)
                    plan.moves.append({uid, from, to});
            }
            for (auto it = xi.places.cbegin(); it != xi.places.cend(); ++it) {
                if (it->first == cover.outSlots.first)
                    plan.coverIp = it.key();
                if (it->second == cover.outSlots.second)
                    plan.coverOop = it.key();
            }
            plan.complete = !inUid.isEmpty();
            return plan;
        };

        // Candidates by their upper bound, best first: once maxPlans complete
        // plans are found, nobody whose bound is below the weakest of them
        // can make the list (exact; ties are still evaluated).
        double restBound = 0.0;
        for (const QString &uid : std::as_const(rest))
            restBound += upperBound.value(uid);
        std::vector<int> order(bench.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return upperBound.value(bench[static_cast<size_t>(a)]->uid)
                   > upperBound.value(bench[static_cast<size_t>(b)]->uid);
        });
        struct Ranked {
            ReplacementPlan plan;
            int benchIndex = 0;
        };
        QList<Ranked> ranked;
        std::vector<double> bestTotals; // complete plans found, best first
        const Bridges restBridges = twoPhase ? bridgesFor(rest) : Bridges();
        for (const int index : order) {
            const Player *candidate = bench[static_cast<size_t>(index)];
            if (static_cast<int>(bestTotals.size()) >= maxPlans
                && restBound + upperBound.value(candidate->uid)
                       < bestTotals[static_cast<size_t>(maxPlans - 1)] - 1e-6)
                break;
            QSet<QString> uids = rest;
            uids.insert(candidate->uid);
            std::vector<const Player *> trial;
            for (const Player *p : pool) {
                if (uids.contains(p->uid))
                    trial.push_back(p);
            }
            Xi xi = startingXi(trial, restBridges);
            // Exactly one extra player: he and every remaining starter play.
            if (xi.places.size() != uids.size())
                continue;
            bool anyMove = false;
            for (const QString &uid : std::as_const(rest))
                anyMove |= xi.places.value(uid) != current.places.value(uid);
            if (twoPhase && anyMove) {
                // Among equally good line-ups, the one where nobody else moves
                // (e.g. not a centre-back swapping sides for no gain).
                Bridges stay = restBridges;
                for (const QString &uid : std::as_const(rest))
                    stay.insert(uid, current.places.value(uid));
                const Xi still = startingXi(trial, stay);
                if (still.places.size() == uids.size() && still.total >= xi.total - 1e-6)
                    xi = still;
            }
            ranked.append({planFrom(xi, candidate->uid), index});
            bestTotals.insert(std::upper_bound(bestTotals.begin(), bestTotals.end(), xi.total,
                                               std::greater<double>()),
                              xi.total);
        }
        // Best whole-XI value first; on a tie the plan that moves fewer
        // starters; otherwise the bench order.
        std::sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) {
            if (std::abs(a.plan.total - b.plan.total) > 1e-6)
                return a.plan.total > b.plan.total;
            if (a.plan.moves.size() != b.plan.moves.size())
                return a.plan.moves.size() < b.plan.moves.size();
            return a.benchIndex < b.benchIndex;
        });
        QList<ReplacementPlan> plans;
        for (const Ranked &r : std::as_const(ranked))
            plans.append(r.plan);
        if (plans.size() > maxPlans)
            plans.resize(std::max(1, maxPlans));
        if (plans.isEmpty()) {
            // Nobody fits: the others close ranks, one place stays empty.
            std::vector<const Player *> trial;
            for (const Player *p : pool) {
                if (rest.contains(p->uid))
                    trial.push_back(p);
            }
            plans.append(planFrom(startingXi(trial, twoPhase ? bridgesFor(rest) : Bridges()),
                                  QString()));
        }
        cover.plans = std::move(plans);
        covers.append(std::move(cover));
    }
    return covers;
}

QList<BridgeOption> SquadBuilder::bridgeOptions(const std::vector<const Player *> &pool,
                                                const QString &tactic, const RoleRatings &ratings,
                                                double ipShare, const Bridges &fixed,
                                                const QString &uid, bool ipPhase,
                                                bool applyAptWeight) const
{
    QList<BridgeOption> options;
    const auto tacticIt = m_definitions.twoPhaseTactics().constFind(tactic);
    if (tacticIt == m_definitions.twoPhaseTactics().constEnd())
        return options;
    const TwoPhaseSquadResult current =
        calculateTwoPhaseSquad(pool, tactic, ratings, ipShare, applyAptWeight, fixed);
    const auto mine = current.slotsOfUid.constFind(uid);
    if (mine == current.slotsOfUid.cend())
        return options; // not in the starting XI
    const QString oldSlot = ipPhase ? mine->first : mine->second;
    const QHash<QString, XiCell> &team = ipPhase ? current.startingIp : current.startingOop;
    const QStringList &order = ipPhase ? tacticIt->ipOrder : tacticIt->oopOrder;
    for (const QString &slot : order) {
        if (slot == oldSlot)
            continue;
        Bridges trial = fixed;
        auto own = trial.value(uid, {mine->first, mine->second});
        (ipPhase ? own.first : own.second) = slot;
        trial.insert(uid, own);
        // Whoever holds the slot moves to the player's old one (a swap).
        const QString other = team.value(slot).playerUid;
        if (!other.isEmpty()) {
            auto theirs = current.slotsOfUid.value(other);
            (ipPhase ? theirs.first : theirs.second) = oldSlot;
            trial.insert(other, theirs);
        }
        const TwoPhaseSquadResult result =
            calculateTwoPhaseSquad(pool, tactic, ratings, ipShare, applyAptWeight, trial);
        const auto placed = result.slotsOfUid.constFind(uid);
        if (placed == result.slotsOfUid.cend()
            || (ipPhase ? placed->first : placed->second) != slot)
            continue; // not possible (position missing)
        BridgeOption option;
        option.slot = slot;
        option.swapUid = other;
        option.bridges = trial;
        option.total = startingValue(result);
        options.append(option);
    }
    return options;
}

} // namespace fm
