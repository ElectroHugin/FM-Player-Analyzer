#include "RoleAssignment.h"

#include "Database.h"
#include "Definitions.h"
#include "Utils.h"

#include <QSet>

#include <algorithm>

namespace fm {

namespace RoleAssignment {

DefaultRoles::DefaultRoles(const Definitions &definitions)
    : m_positionToRoles(definitions.positionToRoleMapping())
{
}

const QStringList &DefaultRoles::forPosition(const QString &positionRaw)
{
    auto it = m_cache.find(positionRaw);
    if (it == m_cache.end()) {
        QSet<QString> roles;
        const QSet<QString> positions = parsePositionString(positionRaw);
        for (const QString &position : positions) {
            for (const QString &role : m_positionToRoles.value(position))
                roles.insert(role);
        }
        QStringList sortedRoles(roles.cbegin(), roles.cend());
        std::sort(sortedRoles.begin(), sortedRoles.end());
        it = m_cache.insert(positionRaw, sortedRoles);
    }
    return it.value();
}

std::vector<std::pair<int, QStringList>> missingRoleAdditions(const std::vector<Player> &players,
                                                              const Definitions &definitions)
{
    DefaultRoles defaultRoles(definitions);
    std::vector<std::pair<int, QStringList>> additions;
    for (size_t row = 0; row < players.size(); ++row) {
        const Player &player = players[row];
        const QStringList &defaults = defaultRoles.forPosition(player.positionRaw);
        if (defaults.isEmpty())
            continue;

        QSet<QString> current(player.assignedRoles.cbegin(), player.assignedRoles.cend());
        bool grew = false;
        for (const QString &role : defaults) {
            if (!current.contains(role)) {
                current.insert(role);
                grew = true;
            }
        }
        if (!grew)
            continue; // every default role already present

        QStringList merged(current.cbegin(), current.cend());
        std::sort(merged.begin(), merged.end());
        additions.push_back({static_cast<int>(row), std::move(merged)});
    }
    return additions;
}

QStringList autoAssignMissingRoles(Database &db, std::vector<Player> &players,
                                   const Definitions &definitions, QString *errorOut)
{
    if (errorOut)
        errorOut->clear();

    const auto additions = missingRoleAdditions(players, definitions);
    if (additions.empty())
        return {};

    // Remember the pre-change roles so a failed write leaves memory == DB.
    std::vector<QStringList> previous;
    previous.reserve(additions.size());
    std::vector<std::pair<int, QStringList>> rolesById;
    rolesById.reserve(additions.size());
    for (const auto &[row, roles] : additions) {
        Player &player = players[static_cast<size_t>(row)];
        previous.push_back(player.assignedRoles);
        player.assignedRoles = roles;
        rolesById.push_back({player.id, roles});
    }

    // Only the roles changed: write just those instead of full player rows.
    if (!db.replacePlayerRoles(rolesById)) {
        if (errorOut)
            *errorOut = db.errorString();
        for (size_t i = 0; i < additions.size(); ++i)
            players[static_cast<size_t>(additions[i].first)].assignedRoles = previous[i];
        return {};
    }

    QStringList uids;
    uids.reserve(static_cast<int>(additions.size()));
    for (const auto &[row, roles] : additions)
        uids << players[static_cast<size_t>(row)].uid;
    return uids;
}

bool clearStalePrimaryRole(Player &player)
{
    if (player.primaryRole.isEmpty() || player.assignedRoles.contains(player.primaryRole))
        return false;
    player.primaryRole.clear();
    return true;
}

} // namespace RoleAssignment

} // namespace fm
