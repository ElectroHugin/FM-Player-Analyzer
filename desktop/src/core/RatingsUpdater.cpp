#include "RatingsUpdater.h"

#include "Database.h"

#include <QDateTime>
#include <QSet>

#include <cmath>

namespace fm {
namespace RatingsUpdater {

namespace {

struct RatedValue {
    bool valid = false;
    double absolute = 0;
    double normalized = 0;
};
using RoleRater = std::function<std::vector<RatedValue>(const std::vector<int> &rows, const QString &role)>;

Result updateWith(Database &db, const std::vector<Player> &players, const RoleRater &rate,
                  double gate, const QStringList &validRoles, const std::vector<int> &playersSubset,
                  const std::function<void(int, int)> &progress, RatingsOrigin origin,
                  const std::function<void(DwrsEntry &, const Player &)> &annotate)
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

    // The gate compares against the stored latest rating. For a subset only
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
    // Three units per role batch, then one quarter of the scale for writing.
    const int batches = roleRows.size();
    const int total = batches * 4;
    for (auto it = roleRows.constBegin(); it != roleRows.constEnd(); ++it) {
        const std::vector<RatedValue> batch = rate(it.value(), it.key());
        for (size_t j = 0; j < it.value().size(); ++j) {
            if (!batch[j].valid)
                continue;
            const Player &p = players[it.value()[j]];
            const double newValue = batch[j].normalized;
            ++result.computed;

            const auto old = latest.constFind({p.id, it.key()});
            // Insert only if new or changed by at least the gate.
            if (old == latest.constEnd() || std::abs(newValue - old->second) >= gate - 1e-9) {
                DwrsEntry entry;
                entry.playerId = p.id;
                entry.role = it.key();
                entry.absolute = batch[j].absolute;
                entry.normalized = newValue;
                entry.timestamp = timestamp;
                if (annotate)
                    annotate(entry, p);
                toInsert.push_back(std::move(entry));
            }
        }
        done += 3;
        if (progress)
            progress(done, total);
    }

    const auto written = [&progress, batches, total](int rows, int of) {
        if (progress && of > 0)
            progress(batches * 3 + static_cast<int>(qint64(rows) * batches / of), total);
    };
    if (!db.appendDwrsRatings(toInsert, origin, true, written)) {
        result.error = db.errorString();
        return result;
    }
    result.inserted = static_cast<int>(toInsert.size());
    result.success = true;
    return result;
}

} // namespace

QByteArray encodeAttributes(const Player &player)
{
    QByteArray bytes(2 * kAttrCount, 0);
    for (int a = 0; a < kAttrCount; ++a) {
        int low = player.attrLo[a], high = player.attrHi[a];
        if (low < 1 || high < low || high > 20) low = high = 0; // unknown or invalid
        bytes[a] = static_cast<char>(low);
        bytes[a + kAttrCount] = static_cast<char>(high);
    }
    return bytes;
}

Result updateDwrsRatings(Database &db, const std::vector<Player> &players,
                         const DwrsEngine &engine, const QStringList &validRoles,
                         const std::vector<int> &playersSubset,
                         std::function<void(int, int)> progress, RatingsOrigin origin)
{
    const RoleRater rate = [&](const std::vector<int> &rows, const QString &role) {
        const DwrsRoleResult batch = engine.calculateRole(players, rows, role);
        std::vector<RatedValue> values(rows.size());
        for (size_t j = 0; j < rows.size(); ++j)
            values[j] = {true, batch.absolute[j], batch.normalized[j]};
        return values;
    };
    return updateWith(db, players, rate, 1.0, validRoles, playersSubset, progress, origin, {});
}

Result updateDwrsRatings(Database &db, const std::vector<Player> &players,
                         const RatingSnapshot &snapshot, const QStringList &validRoles,
                         const std::vector<int> &playersSubset,
                         std::function<void(int, int)> progress, RatingsOrigin origin,
                         const QDate &gameDate)
{
    const RoleRater rate = [&](const std::vector<int> &rows, const QString &role) {
        std::vector<RatedValue> values(rows.size());
        for (size_t j = 0; j < rows.size(); ++j) {
            const CurveScore score = snapshot.evaluate(players[rows[j]], role);
            if (score.normalized)
                values[j] = {true, score.absolute, *score.normalized};
        }
        return values;
    };
    const auto annotate = [&](DwrsEntry &entry, const Player &player) {
        entry.basisKey = snapshot.cacheKey();
        entry.profileId = snapshot.profileId();
        entry.profileRevision = snapshot.profileRevision();
        entry.calculationVersion = snapshot.calculationVersion();
        entry.gameDate = gameDate;
        entry.attributes = encodeAttributes(player);
    };
    return updateWith(db, players, rate, snapshot.isClassic() ? 1.0 : 0.1, validRoles,
                      playersSubset, progress, origin, annotate);
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

bool loadRatingCaches(Database &db, const PlayerStore &store, LatestRatings &latest,
                      RoleRatings &ratings)
{
    latest.clear();
    ratings.clear();

    // Role names repeat ~store.size() times each: keep one shared copy per role
    // (implicit sharing) together with that role's uid -> rating hash. The
    // per-role hashes live here by value (QHash may move its values when it
    // grows, so no pointers into `ratings`) and are moved over at the end.
    struct RoleSlot {
        QString name;
        QHash<QString, double> assigned;
    };
    QHash<QString, RoleSlot> roles;

    int currentId = -1;
    const Player *player = nullptr;
    const bool ok = db.forEachLatestDwrsRating(
        [&](int playerId, const QString &role, double absolute, double normalized) {
            if (playerId != currentId) {
                currentId = playerId;
                const int row = store.rowById(playerId);
                player = row < 0 ? nullptr : &store.at(row);
            }
            auto slot = roles.find(role);
            if (slot == roles.end())
                slot = roles.insert(role, RoleSlot{role, {}});
            latest.insert({playerId, slot->name}, {absolute, normalized});
            if (player && player->assignedRoles.contains(slot->name))
                slot->assigned.insert(player->uid, normalized);
            // else: unknown player, or role removed since this rating was stored
        });
    if (!ok) {
        latest.clear();
        return false;
    }
    for (auto it = roles.begin(); it != roles.end(); ++it) {
        if (!it->assigned.isEmpty())
            ratings.insert(it->name, std::move(it->assigned));
    }
    return true;
}

} // namespace RatingsUpdater
} // namespace fm
