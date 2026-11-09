#include "TalentEngine.h"

#include "Definitions.h"

namespace fm {
namespace TalentEngine {

double personalityBonus(const Definitions &definitions, const QString &personality)
{
    const QString category = definitions.personalityCategory(personality);
    if (category == QLatin1String("good"))
        return kGoodPersonalityBonus;
    if (category == QLatin1String("bad"))
        return kBadPersonalityBonus;
    return 0.0;
}

double calculateTalentScore(const Definitions &definitions, double bestDwrs, double age,
                            double determination, double workRate, const QString &personality,
                            double ageCap)
{
    return bestDwrs + 2.0 * (ageCap - age) + (determination + workRate - 20.0) / 4.0
           + personalityBonus(definitions, personality);
}

double talentForPlayer(const Definitions &definitions, const Player &player, double bestDwrs,
                       double ageCap)
{
    // Development proxy, not hidden PA: unknown attributes are neutral,
    // observed ranges use their midpoint instead of penalising lack of scouting.
    const double age = player.age > 0 ? player.age : ageCap;
    const auto estimate = [&](Attr attr) {
        const int lo = player.attrLo[idx(attr)], hi = player.attrHi[idx(attr)];
        return lo >= 1 && hi >= lo && hi <= 20 ? (lo + hi) / 2.0 : 10.0;
    };
    return calculateTalentScore(definitions, bestDwrs, age, estimate(Attr::Determination),
                                estimate(Attr::WorkRate), player.personality, ageCap);
}

double ageCapForPlayer(const Player &player, int outfielderCap, int goalkeeperCap)
{
    return player.isGoalkeeper() ? goalkeeperCap : outfielderCap;
}

} // namespace TalentEngine
} // namespace fm
