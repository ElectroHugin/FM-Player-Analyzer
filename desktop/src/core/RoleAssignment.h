#pragma once

#include "Player.h"

#include <QStringList>

#include <vector>

namespace fm {

class Database;
class Definitions;

// Additive default-role assignment. For every player, the default roles
// derived from his current game positions (position_to_role_mapping) are merged
// into his assigned roles: roles that are missing get added, existing/manual
// roles are never removed. This tops up players who gained positions across
// uploads (e.g. M(R) -> M/AM(R) also earning the AM(R) roles), which the old
// "only players with no roles" behavior never did.
namespace RoleAssignment {

// Mutates players in place and persists the changed ones. Returns the uids
// of the players whose role set grew (empty on no-op); sets errorOut and
// returns an empty list on DB failure with *errorOut non-empty.
QStringList autoAssignMissingRoles(Database &db, std::vector<Player> &players,
                                   const Definitions &definitions,
                                   QString *errorOut = nullptr);

} // namespace RoleAssignment

} // namespace fm
