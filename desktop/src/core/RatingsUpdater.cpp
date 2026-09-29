#include "RatingsUpdater.h"

#include "Database.h"

#include <QDateTime>
#include <QSet>

#include <cmath>

namespace fm {
namespace RatingsUpdater {

Result updateDwrsRatings(Database &db, const std::vector<Player> &players,
                         const DwrsEngine &engine, const QStringList &validRoles,
                         const std::vector<int> &playersSubset,
                         std::function<void(int, int)> progress)
{
    Result result;

    // Restrict to the subset if given (legacy player_ids_to_update).
    std::vector<int> rows;
    if (playersSubset.empty()) {
        rows.resize(players.size());
        for (size_t i = 0; i < players.size(); ++i)
            rows[i] = static_cast<int>(i);
    } else {
        rows = playersSubset;
    }
    if (rows.empty()) {
        result.success = true;
        return result;
    }

    // The 1% gate compares against the stored latest rating. For a subset only
    // those players' rows are needed — not the whole table (~400k rows).
    LatestRatings latest;
    if (playersSubset.empty()) {
        latest = db.latestDwrsRatings();
    } else {
        QList<int> ids;
        ids.reserve(static_cast<int>(rows.size()));
        for (const int row : rows)
            ids << players[row].id;
        latest = db.latestDwrsRatings(ids);
    }
    const QString timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    // Group rows by assigned role (only valid roles), then batch per role.
    const QSet<QString> validSet(validRoles.begin(), validRoles.end());
    QHash<QString, std::vector<int>> roleRows;
    for (const int row : rows) {
        for (const QString &role : players[row].assignedRoles) {
            if (validSet.contains(role))
                roleRows[role].push_back(row);
        }
    }

    std::vector<DwrsEntry> toInsert;
    int done = 0;
    const int total = roleRows.size();
    for (auto it = roleRows.constBegin(); it != roleRows.constEnd(); ++it) {
        const DwrsRoleResult batch = engine.calculateRole(players, it.value(), it.key());
        for (size_t j = 0; j < it.value().size(); ++j) {
            const Player &p = players[it.value()[j]];
            const double newValue = batch.normalized[j];
            ++result.computed;

            const auto old = latest.constFind({p.id, it.key()});
            // Insert only if new or changed by at least 1% (legacy gate).
            if (old == latest.constEnd() || std::abs(newValue - old->second) >= 1.0) {
                DwrsEntry entry;
                entry.playerId = p.id;
                entry.role = it.key();
                entry.absolute = batch.absolute[j];
                entry.normalized = newValue;
                entry.timestamp = timestamp;
                toInsert.push_back(std::move(entry));
            }
        }
        if (progress)
            progress(++done, total);
    }

    if (!db.appendDwrsRatings(toInsert)) {
        result.error = db.errorString();
        return result;
    }
    result.inserted = static_cast<int>(toInsert.size());
    result.success = true;
    return result;
}

void patchRoleRatings(RoleRatings &ratings, const Player &player, const QString &previousUid,
                      const LatestRatings &latest)
{
    for (auto it = ratings.begin(); it != ratings.end(); ++it) {
        it->remove(previousUid);
        it->remove(player.uid);
    }
    for (const QString &role : player.assignedRoles) {
        const auto rating = latest.constFind({player.id, role});
        if (rating != latest.constEnd())
            ratings[role].insert(player.uid, rating.value().second);
    }
}

RoleRatings roleRatingsForAssigned(const PlayerStore &store, const LatestRatings &latest)
{
    RoleRatings ratings;
    for (auto it = latest.constBegin(); it != latest.constEnd(); ++it) {
        const int row = store.rowById(it.key().first);
        if (row < 0)
            continue;
        const Player &player = store.at(row);
        if (!player.assignedRoles.contains(it.key().second))
            continue; // role removed since this rating was stored
        ratings[it.key().second].insert(player.uid, it.value().second);
    }
    return ratings;
}

} // namespace RatingsUpdater
} // namespace fm
