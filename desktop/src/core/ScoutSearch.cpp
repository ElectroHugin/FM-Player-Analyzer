#include "ScoutSearch.h"

#include "Definitions.h"
#include "PitchGeometry.h"
#include "Player.h"

namespace fm::ScoutSearch {

ScoutPlace slotPlace(const QString &fmVersionId, const QString &slot, const QString &role)
{
    ScoutPlace place;
    place.positionLabel = slot;
    place.positions = slot.isEmpty() ? QStringList()
                                     : tacticalSlotToGamePositions(fmVersionId).value(slot);
    if (!slot.isEmpty() && place.positions.isEmpty())
        place.positions = {slot}; // unknown slot: the key itself (never matches by accident)
    place.role = role;
    return place;
}

ScoutPlace positionPlace(const QString &gamePosition, const QString &role)
{
    ScoutPlace place;
    place.positionLabel = gamePosition;
    if (!gamePosition.isEmpty())
        place.positions = {gamePosition};
    place.role = role;
    return place;
}

ScoutCriterion criterion(const ScoutPlace &place)
{
    return ScoutCriterion{{place}};
}

ScoutCriterion bridge(const QString &fmVersionId, const QString &ipSlot, const QString &ipRole,
                      const QString &oopSlot, const QString &oopRole)
{
    return ScoutCriterion{{slotPlace(fmVersionId, ipSlot, ipRole),
                           slotPlace(fmVersionId, oopSlot, oopRole)}};
}

QStringList requiredRoles(const QList<ScoutCriterion> &criteria)
{
    QStringList roles;
    for (const ScoutCriterion &c : criteria) {
        for (const ScoutPlace &place : c.places) {
            if (!place.role.isEmpty() && !roles.contains(place.role))
                roles.append(place.role);
        }
    }
    return roles;
}

bool needsPositions(const QList<ScoutCriterion> &criteria)
{
    for (const ScoutCriterion &c : criteria) {
        for (const ScoutPlace &place : c.places) {
            if (!place.positions.isEmpty())
                return true;
        }
    }
    return false;
}

bool meetsAll(const Player &player, const QSet<QString> &positions,
              const QList<ScoutCriterion> &criteria, const RoleRatings &ratings, double minDwrs,
              double maxDwrs)
{
    for (const ScoutCriterion &c : criteria) {
        for (const ScoutPlace &place : c.places) {
            if (!place.positions.isEmpty()) {
                bool any = false;
                for (const QString &position : place.positions) {
                    if (positions.contains(position)) {
                        any = true;
                        break;
                    }
                }
                if (!any)
                    return false;
            }
            if (!place.role.isEmpty()) {
                const auto roleIt = ratings.constFind(place.role);
                if (roleIt == ratings.constEnd())
                    return false;
                const auto it = roleIt->constFind(player.uid);
                if (it == roleIt->constEnd() || it.value() < minDwrs || it.value() > maxDwrs)
                    return false;
            }
        }
    }
    return true;
}

double phaseWeight(const Definitions &definitions, const QString &role, double ipShare)
{
    const QString phase = definitions.rolePhase(role);
    if (phase == QLatin1String("ip"))
        return ipShare;
    if (phase == QLatin1String("oop"))
        return 1.0 - ipShare;
    return 1.0;
}

double combinedValue(const Player &player, const QList<ScoutCriterion> &criteria,
                     const RoleRatings &ratings, const Definitions &definitions, double ipShare)
{
    double weighted = 0.0;
    double weights = 0.0;
    double plain = 0.0;
    int count = 0;
    for (const ScoutCriterion &c : criteria) {
        for (const ScoutPlace &place : c.places) {
            if (place.role.isEmpty())
                continue;
            const auto roleIt = ratings.constFind(place.role);
            if (roleIt == ratings.constEnd())
                return -1.0;
            const auto it = roleIt->constFind(player.uid);
            if (it == roleIt->constEnd())
                return -1.0;
            const double w = phaseWeight(definitions, place.role, ipShare);
            weighted += w * it.value();
            weights += w;
            plain += it.value();
            ++count;
        }
    }
    if (count == 0)
        return -1.0;
    // All weights zero (share 0 % or 100 % with only the other phase): plain mean.
    return weights > 1e-9 ? weighted / weights : plain / count;
}

} // namespace fm::ScoutSearch
