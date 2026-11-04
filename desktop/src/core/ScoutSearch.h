#pragma once

#include "SquadBuilder.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace fm {

class Definitions;
struct Player;

// Scouting search with several requirements at once (2.1.0): every
// criterion must be met (AND). A criterion is a role, a position, a place
// (position and role) or a bridge — the two places a player of a two-phase
// tactic covers, one with ball (IP) and one without (OOP).

// One place a candidate has to fill.
struct ScoutPlace {
    QString positionLabel; // what the user reads: a slot ("AMR") or a game position ("AM (R)")
    QStringList positions; // eligible game positions, one is enough; empty = any position
    QString role;          // role id; empty = any role
    bool operator==(const ScoutPlace &other) const = default;
};

// One chip of the search: met when the player meets all of its places.
struct ScoutCriterion {
    QList<ScoutPlace> places;
    bool operator==(const ScoutCriterion &other) const = default;
};

// A search handed to the scouting page, e.g. from the Best XI's right-click.
struct ScoutSearchRequest {
    QString tactic;               // tactic to show the role columns of ("" = keep)
    QList<ScoutCriterion> criteria;
    QString source;               // e.g. "Bridge von Müller" (shown above the results)
    QString referenceName;        // benchmark for the delta column ("" = none)
    double referenceValue = -1.0; // his combined value over the required roles
};

namespace ScoutSearch {

// Place for a tactic slot: the player needs one of the slot's game positions.
ScoutPlace slotPlace(const QString &fmVersionId, const QString &slot, const QString &role);
// Place for a game position ("AM (R)") and/or a role (either may be empty).
ScoutPlace positionPlace(const QString &gamePosition, const QString &role);

// Single-place criteria.
ScoutCriterion criterion(const ScoutPlace &place);
// Bridge of a two-phase tactic: slot + role with ball and slot + role without.
ScoutCriterion bridge(const QString &fmVersionId, const QString &ipSlot, const QString &ipRole,
                      const QString &oopSlot, const QString &oopRole);

// Every role the criteria ask for, in order of appearance, without duplicates.
QStringList requiredRoles(const QList<ScoutCriterion> &criteria);
// True when some criterion asks for a position (the caller then parses positions).
bool needsPositions(const QList<ScoutCriterion> &criteria);

// True when the player meets every criterion: he has one of the eligible
// positions of each place and a DWRS within [minDwrs, maxDwrs] in each
// required role (an unrated role never passes). `positions` is
// parsePositionString(player.positionRaw); unused without positions.
bool meetsAll(const Player &player, const QSet<QString> &positions,
              const QList<ScoutCriterion> &criteria, const RoleRatings &ratings, double minDwrs,
              double maxDwrs);

// Weight of a role in the combined value: the share with ball for a role
// with ball, the rest for one without, 1 for a role of both phases.
double phaseWeight(const Definitions &definitions, const QString &role, double ipShare);

// Combined value over the required roles: the phase-weighted mean of the
// player's DWRS (a bridge: a * IP + (1 - a) * OOP, like the Best XI).
// -1 when the criteria ask for no role or the player is unrated in one.
double combinedValue(const Player &player, const QList<ScoutCriterion> &criteria,
                     const RoleRatings &ratings, const Definitions &definitions, double ipShare);

} // namespace ScoutSearch

} // namespace fm
