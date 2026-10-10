#pragma once

#include "SquadBuilder.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace fm {

// A squad player whose place changes when a signing arrives. Empty slot = not
// in that team (e.g. dropped from the XI to the B-team or out of both).
struct SquadMove {
    const Player *player = nullptr;
    QString fromSlot;
    bool fromBTeam = false;
    QString toSlot;
    bool toBTeam = false;
};

// One simulated signing: the squad plus this market player, solved again
// exactly by SquadBuilder.
struct SigningOption {
    const Player *player = nullptr; // the market player
    QString slot;                   // XI slot he takes
    double rating = 0.0;            // his DWRS in that slot's role
    double xiGain = 0.0;            // XI total DWRS after minus before
    double bTeamGain = 0.0;         // B-team total DWRS after minus before
    std::vector<SquadMove> moves;   // knock-on effects inside the squad
};

// One XI slot: today's player, how he compares with the affordable market and
// the signings that would end up in this slot.
struct SlotNeed {
    QString slot;
    QString role;
    XiCell current;           // empty cell = slot unfilled today
    double currentRating = 0.0;
    int marketCount = 0;      // market players rated in the role who can play the slot
    int marketBetter = 0;     // ... of them rated higher than the current player
    double marketBest = 0.0;  // best market rating in the role (0 = nobody)
    std::vector<SigningOption> options; // biggest XI gain first
    double bestGain() const { return options.empty() ? 0.0 : options.front().xiGain; }
};

struct SigningAnalysis {
    double xiTotal = 0.0;    // today's XI total DWRS
    double bTeamTotal = 0.0;
    int marketSize = 0;      // market players given
    int simulated = 0;       // signings actually simulated
    std::vector<SlotNeed> needs; // biggest best gain first, then tactic order
};

// "Where do I really need to buy?" The answer is measured, not guessed: each
// promising market player is added to the squad and the starting XI and
// B-team are solved again exactly. The gain is the change of the XI's total
// DWRS — so a position only shows a need when the given market (e.g. what the
// budget affords) actually holds someone who makes the team better, and
// knock-on effects count (a signing at DC can free a versatile player for DM).
namespace SigningAdvisor {

// squad: today's pool (as for Best XI). market: candidates to try, none of
// them in the squad. Per slot the candidatesPerSlot best-rated market players
// are simulated (an exact screen would need every player; the best few per
// slot are the only realistic winners). optionsPerSlot caps each slot's list.
// A signing's agreed playing time is unknown, so it is simulated without one.
SigningAnalysis analyze(const SquadBuilder &builder, const std::vector<const Player *> &squad,
                        const std::vector<const Player *> &market,
                        const QHash<QString, QString> &positions, const QStringList &slotOrder,
                        const RoleRatings &ratings, int candidatesPerSlot = 12,
                        int optionsPerSlot = 15);

} // namespace SigningAdvisor

} // namespace fm
