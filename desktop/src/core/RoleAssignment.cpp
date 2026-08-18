#include "RoleAssignment.h"

#include "Database.h"
#include "Definitions.h"
#include "Utils.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace fm {

namespace RoleAssignment {

QStringList autoAssignMissingRoles(Database &db, std::vector<Player> &players,
                                   const Definitions &definitions, QString *errorOut)
{
    if (errorOut)
        errorOut->clear();

    const QHash<QString, QStringList> posMap = definitions.positionToRoleMapping();

    // Position strings repeat heavily across a big scouting database; parse
    // each distinct string only once (mirrors the legacy optimization).
    QHash<QString, QStringList> rolesForPositionStr;
    const auto defaultRolesFor = [&](const QString &positionRaw) -> const QStringList & {
        auto it = rolesForPositionStr.find(positionRaw);
        if (it == rolesForPositionStr.end()) {
            QSet<QString> roles;
            const QSet<QString> positions = parsePositionString(positionRaw);
            for (const QString &position : positions) {
                for (const QString &role : posMap.value(position))
                    roles.insert(role);
            }
            QStringList sortedRoles(roles.cbegin(), roles.cend());
            std::sort(sortedRoles.begin(), sortedRoles.end());
            it = rolesForPositionStr.insert(positionRaw, sortedRoles);
        }
        return it.value();
    };

    // Remember the pre-change roles so a failed write leaves memory == DB.
    std::vector<std::pair<Player *, QStringList>> changed;
    for (Player &player : players) {
        const QStringList &defaults = defaultRolesFor(player.positionRaw);
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
        changed.push_back({&player, player.assignedRoles});
        player.assignedRoles = std::move(merged);
    }

    if (changed.empty())
        return {};

    std::vector<Player> batch;
    batch.reserve(changed.size());
    for (const auto &[p, oldRoles] : changed)
        batch.push_back(*p);
    if (!db.upsertPlayers(batch)) {
        if (errorOut)
            *errorOut = db.errorString();
        // Roll the in-memory change back so store and DB stay consistent.
        for (auto &[p, oldRoles] : changed)
            p->assignedRoles = oldRoles;
        return {};
    }

    QStringList uids;
    uids.reserve(static_cast<int>(changed.size()));
    for (const auto &[p, oldRoles] : changed)
        uids << p->uid;
    return uids;
}

} // namespace RoleAssignment

} // namespace fm
