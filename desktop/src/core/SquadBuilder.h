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
    // Two-phase tactic (calculateSquadForTactic): the slots are those of the
    // line-up without ball and a cell's rating is the player's value over
    // both line-ups (valueOfUid, untruncated).
    bool twoPhase = false;
    QHash<QString, double> valueOfUid;
    QHash<QString, QPair<QString, QString>> slotsOfUid; // uid -> (ip slot, oop slot)
};

// Best XI and B-team of a two-phase tactic (FM26+): every player of a team has
// one slot in the in-possession line-up and one in the out-of-possession
// line-up; which slot goes with which is the result of the optimisation, not
// part of the tactic.
struct TwoPhaseSquadResult {
    QHash<QString, XiCell> startingIp;  // ip slot -> cell (rating = ip role DWRS)
    QHash<QString, XiCell> startingOop; // oop slot -> cell (rating = oop role DWRS)
    QHash<QString, XiCell> bTeamIp;
    QHash<QString, XiCell> bTeamOop;
    QHash<QString, QPair<QString, QString>> slotsOfUid; // uid -> (ip slot, oop slot)
    QHash<QString, double> valueOfUid;                  // uid -> a*ip + (1-a)*oop DWRS
    std::vector<const Player *> depthPool; // not in XI or B-team, pool order
};

// One way to move a starting-XI player to another slot of one line-up
// (SquadBuilder::bridgeOptions).
struct BridgeOption {
    QString slot;    // the new slot
    QString swapUid; // who held it and takes the player's old slot ("" = empty)
    QHash<QString, QPair<QString, QString>> bridges; // fixed bridges to apply
    double total = 0.0; // starting-XI value with it (SquadBuilder::startingValue)
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

    // Two-phase tactic (FM26+, see algorithmus-zwei-aufstellungen.md): picks
    // the 11 players and, for each, one in-possession and one out-of-possession
    // slot so that — strictly in this order — the most players are placed and
    // the summed value is maximal; value = ipShare * score(ip role) +
    // (1 - ipShare) * score(oop role), score = DWRS x playing-time weight x
    // natural-position bonus. A player needs the position for both slots.
    // Exact (minimum-cost flow). The B-team is then chosen the same way from
    // the remaining players.
    // fixedBridges: manual bridges uid -> (ip slot, oop slot) for the
    // starting XI; an empty slot leaves that phase free. A fixed player may
    // only take his slot, nobody else may take it; the rest is optimised.
    using Bridges = QHash<QString, QPair<QString, QString>>;
    TwoPhaseSquadResult calculateTwoPhaseSquad(const std::vector<const Player *> &pool,
                                               const QString &tactic, const RoleRatings &ratings,
                                               double ipShare, bool applyAptWeight = true,
                                               const Bridges &fixedBridges = {}) const;

    // Sum of the starting XI's values over both line-ups.
    static double startingValue(const TwoPhaseSquadResult &squad);

    // The other slots `uid` could take in one line-up (ipPhase) on top of the
    // fixed bridges: the holder of a slot swaps to his old one. Each option
    // carries the bridges to store and the resulting starting-XI value.
    QList<BridgeOption> bridgeOptions(const std::vector<const Player *> &pool,
                                      const QString &tactic, const RoleRatings &ratings,
                                      double ipShare, const Bridges &fixed, const QString &uid,
                                      bool ipPhase, bool applyAptWeight = true) const;

    // Best XI, B-team and depth for a tactic by name: calculateSquadAndSurplus
    // for a one-line-up tactic, calculateTwoPhaseSquad (with the configured
    // share "with ball") folded into a SquadResult for a two-phase tactic,
    // with the user's manual bridges (fixedBridges) if given.
    SquadResult calculateSquadForTactic(const std::vector<const Player *> &pool,
                                        const QString &tactic, const RoleRatings &ratings,
                                        bool applyAptWeight = true,
                                        const Bridges &fixedBridges = {}) const;

    DevelopmentSquads calculateDevelopmentSquads(
        const std::vector<const Player *> &secondTeamPlayers,
        const std::vector<const Player *> &firstTeamRemnants,
        const QHash<QString, QString> &positions, const QStringList &slotOrder,
        const RoleRatings &ratings, const QSet<QString> &depthPlayerUids) const;

    // Highest rating across the player's assigned roles (legacy
    // best_dwrs_for_player).
    static double bestDwrsForPlayer(const Player &player, const RoleRatings &ratings);

private:
    // Best depth option per role among players outside XI and B-team.
    void fillDepthOptions(const std::vector<const Player *> &pool,
                          const QSet<QString> &xiOrBTeamUids, const QStringList &roles,
                          const RoleRatings &ratings, SquadResult &result) const;

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
