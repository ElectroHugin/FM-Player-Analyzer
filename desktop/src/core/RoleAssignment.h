#pragma once

#include "Player.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <utility>
#include <vector>

namespace fm {

class Database;
class Definitions;

// Additive default-role assignment. For every player, the default roles
// derived from his current game positions (position_to_role_mapping) are merged
// into his assigned roles: roles that are missing get added, existing/manual
// roles are never removed. This tops up players who gained positions across
// uploads (e.g. M(R) -> M/AM(R) also earning the AM(R) roles), which the old
// "only players with no roles" behavior never did. Used by the HTML import and
// by the "add missing default roles" action on the Assign Roles page.
namespace RoleAssignment {

// Default roles (sorted) for an FM position string, parsed once per distinct
// string — position strings repeat heavily across a big scouting database.
class DefaultRoles
{
public:
    explicit DefaultRoles(const Definitions &definitions);
    const QStringList &forPosition(const QString &positionRaw);

private:
    const QHash<QString, QStringList> &m_positionToRoles;
    QHash<QString, QStringList> m_cache;
};

// Row index -> merged (sorted) role list for every player that is missing at
// least one default role of his positions. Pure: nothing is changed.
std::vector<std::pair<int, QStringList>> missingRoleAdditions(const std::vector<Player> &players,
                                                              const Definitions &definitions);

// Applies missingRoleAdditions to players in place and persists the changed
// ones (roles only). Returns the uids of the players whose role set grew (empty
// on no-op); on DB failure sets errorOut, restores the players and returns an
// empty list with *errorOut non-empty.
QStringList autoAssignMissingRoles(Database &db, std::vector<Player> &players,
                                   const Definitions &definitions,
                                   QString *errorOut = nullptr);

// Clears the player's primary role if it is no longer among his assigned roles
// (call after changing assignedRoles). Returns true if it was cleared.
bool clearStalePrimaryRole(Player &player);

} // namespace RoleAssignment

} // namespace fm
