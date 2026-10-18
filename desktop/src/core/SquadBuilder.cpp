#include "SquadBuilder.h"

#include "AppConfig.h"
#include "Assignment.h"
#include "Constants.h"
#include "Definitions.h"
#include "TalentEngine.h"
#include "Utils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

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
                                                   bool applyAptWeight) const
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
    // spare columns cost 0, which is how a slot stays empty.
    const int columnCount = static_cast<int>(columnPlayers.size()) + rowCount;
    std::vector<std::vector<std::int64_t>> cost(
        static_cast<size_t>(rowCount), std::vector<std::int64_t>(static_cast<size_t>(columnCount), 0));
    for (int k = 0; k < slotCount; ++k) {
        for (const Candidate &c : slotCandidates[static_cast<size_t>(k)]) {
            const std::int64_t score = std::llround(c.score * scale);
            const size_t column = static_cast<size_t>(columnOf.value(c.player));
            cost[static_cast<size_t>(k)][column] = -(xiFill + score * xiScore);
            cost[static_cast<size_t>(slotCount + k)][column] = -(bFill + score);
        }
    }
    const std::vector<int> assignment = Assignment::solveMinCost(cost);

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

    std::vector<const Player *> availableDepth;
    for (const Player *p : pool) {
        if (!xiOrBTeamUids.contains(p->uid))
            availableDepth.push_back(p);
    }

    QHash<QString, QSet<QString>> rolesCoveredByPlayer;
    if (!availableDepth.empty()) {
        QStringList uniqueRoles;
        for (const QString &slot : slotOrder) {
            const QString role = positions.value(slot);
            if (!uniqueRoles.contains(role))
                uniqueRoles.append(role);
        }
        std::sort(uniqueRoles.begin(), uniqueRoles.end());

        for (const QString &role : uniqueRoles) {
            const QHash<QString, double> roleRatings = ratings.value(role);
            std::vector<const Player *> sortedCandidates = availableDepth;
            // Same pin as the team selection: no depth option for other roles.
            std::erase_if(sortedCandidates,
                          [&role](const Player *p) { return pinnedToOtherRole(*p, role); });
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

    result.startingXi = std::move(xi);
    result.bTeam = std::move(bTeam);
    result.depthPool = std::move(depthPool);
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

} // namespace fm
