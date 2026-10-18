#pragma once

#include "Player.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

namespace fm {

class AppConfig;
class Definitions;

// Normalized DWRS per role per player uid (numeric 0-100), the C++ mirror of
// legacy master_role_ratings.
using RoleRatings = QHash<QString, QHash<QString, double>>;

// One slot of a computed XI.
struct XiCell {
    QString playerUid; // empty = slot unfilled
    QString name = QStringLiteral("-");
    double rating = 0.0; // whole percent (legacy truncates to int for display)
    QString apt;
    bool isFilled() const { return !playerUid.isEmpty(); }
};

struct DepthOption {
    QString playerUid;
    QString name;
    double rating = 0.0;
    QString apt;
    int age = 0;
};

// Result of calculate_squad_and_surplus.
struct SquadResult {
    QHash<QString, XiCell> startingXi; // slot -> cell (every tactic slot present)
    QHash<QString, XiCell> bTeam;
    QHash<QString, QList<DepthOption>> bestDepthOptions; // role -> best option(s)
    std::vector<const Player *> depthPool; // remaining after XI + B-team, in pool order
    QSet<QString> depthPlayerUids;
    QSet<QString> coreSquadUids; // XI + B-team + depth
};

// Result of calculate_development_squads.
struct DevelopmentSquads {
    QHash<QString, XiCell> youthXi;
    QHash<QString, XiCell> secondTeamXi;
    std::vector<const Player *> loanCandidates; // most promising first
    std::vector<const Player *> sellCandidates; // sorted by last name
};

// Grew out of legacy squad_logic.py. The starting XI and B-team are no longer
// picked greedily ("weakest link first") but jointly and provably optimally as
// an assignment problem (see calculateSquadAndSurplus). Pools are ordered lists
// of players (legacy list order = DB row order); the remaining tie-breaking
// replicates Python's stable sorts and the tactic slot order from
// Definitions::tacticSlotOrder.
class SquadBuilder
{
public:
    SquadBuilder(const Definitions &definitions, const AppConfig &config);

    const Definitions &definitions() const { return m_definitions; }

    // Rebuild cached config values after settings change.
    void reloadConfig();

    // Starting XI and B-team for a tactic, chosen together so that — strictly
    // in this order — the most XI slots are filled, the XI total score is
    // maximal, then the most B-team slots are filled and the B-team total is
    // maximal (score = DWRS x playing-time weight x natural-position bonus).
    // Exact, not heuristic. Plus depth options and the remaining pool.
    SquadResult calculateSquadAndSurplus(const std::vector<const Player *> &pool,
                                         const QHash<QString, QString> &positions,
                                         const QStringList &slotOrder,
                                         const RoleRatings &ratings,
                                         bool applyAptWeight = true) const;

    DevelopmentSquads calculateDevelopmentSquads(
        const std::vector<const Player *> &secondTeamPlayers,
        const std::vector<const Player *> &firstTeamRemnants,
        const QHash<QString, QString> &positions, const QStringList &slotOrder,
        const RoleRatings &ratings, const QSet<QString> &depthPlayerUids) const;

    // Highest rating across the player's assigned roles (legacy
    // best_dwrs_for_player).
    static double bestDwrsForPlayer(const Player &player, const RoleRatings &ratings);

private:
    const Definitions &m_definitions;
    const AppConfig &m_config;

    int m_maxDepthRoles = 2;
    int m_minLoanTalent = 45;
    int m_minLoanAge = 17;
    int m_youthLoanOverAge = 18;
    int m_outfielderCap = 20;
    int m_goalkeeperCap = 25;
    double m_naturalPosMultiplier = 1.0;
    QHash<QString, double> m_aptWeights; // display name -> weight
    QHash<QString, QStringList> m_roleToGamePositions;
};

} // namespace fm
