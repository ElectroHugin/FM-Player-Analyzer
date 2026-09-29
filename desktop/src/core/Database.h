#pragma once

#include "Player.h"

#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>
#include <utility>
#include <vector>

class QSqlQuery;

namespace fm {

// One historical DWRS rating row.
struct DwrsEntry {
    int playerId = 0;
    QString role;
    double absolute = 0.0;
    double normalized = 0.0; // 0-100, numeric (legacy stored "68%")
    QString timestamp;       // "yyyy-MM-dd HH:mm:ss"
};

// Latest rating per (playerId, role).
using LatestRatings = QHash<QPair<int, QString>, QPair<double, double>>; // -> (absolute, normalized)

// The new, typed SQLite schema and all persistence operations.
// Each instance owns one named connection; create one per thread.
class Database
{
public:
    // connectionName must be unique per open database+thread.
    explicit Database(const QString &connectionName);

    // "<prefix>_<n>" with a process-wide counter (thread-safe). Worker threads
    // use this so two overlapping jobs never share — and tear down — a
    // connection name.
    static QString uniqueConnectionName(const QString &prefix);
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // Opens (and creates/migrates if needed) the database file.
    bool open(const QString &filePath);
    void close();
    bool isOpen() const;
    QString errorString() const { return m_error; }
    QString filePath() const { return m_filePath; }

    // --- Transactions ---
    // Nestable: the outermost level is a real transaction, inner levels are
    // SAVEPOINTs. Every write method below manages its own level, so when a
    // caller opens an outer transaction (e.g. a whole HTML import) all of them
    // compose into one atomic unit: a failure anywhere rolls everything back.
    bool beginTransaction();
    bool commitTransaction();
    void rollbackTransaction();

    // Snake_case DB column base name for an attribute ("Off the Ball" -> "off_the_ball").
    static QString attrColumnName(const QString &fullAttrName);

    // --- Players ---
    std::vector<Player> loadPlayers();

    // What upsertPlayers does with the player_roles junction table.
    enum class RoleWrite {
        Replace, // rewrite each player's roles from Player::assignedRoles
        Keep,    // leave player_roles untouched (caller guarantees roles are unchanged,
                 // e.g. an FM import, which never alters assigned roles)
    };

    // Inserts new players (id == 0) and updates existing ones (id != 0), in
    // one transaction. Assigns fresh ids to inserted players.
    bool upsertPlayers(std::vector<Player> &players, RoleWrite roles = RoleWrite::Replace);

    // Rewrites only the assigned roles of the given players (id -> roles), in one
    // transaction — far cheaper than a full upsert when nothing else changed.
    bool replacePlayerRoles(const std::vector<std::pair<int, QStringList>> &rolesById);

    bool deletePlayers(const QList<int> &playerIds);

    // Gives an existing player a new uid (ID unification: the DB row keeps
    // its id, history and app-managed columns).
    bool renamePlayerUid(int playerId, const QString &newUid);

    // Merges a corrupted duplicate into the real record: moves the DWRS
    // history (skipping rows that would collide) and deletes the bad player.
    // App-managed field merging happens at the Player level in the importer.
    bool mergePlayerInto(int badPlayerId, int goodPlayerId);

    // --- DWRS history ---
    bool appendDwrsRatings(const std::vector<DwrsEntry> &entries);
    LatestRatings latestDwrsRatings();
    // Latest ratings of just these players (queried in id chunks).
    LatestRatings latestDwrsRatings(const QList<int> &playerIds);
    // Full history for a set of players (optionally one role), ordered by
    // player, role, timestamp.
    std::vector<DwrsEntry> dwrsHistory(const QList<int> &playerIds,
                                       const QString &role = QString());

    // --- Settings (key/value; same keys as legacy) ---
    QString setting(const QString &key, const QString &defaultValue = QString());
    bool setSetting(const QString &key, const QString &value);
    bool removeSetting(const QString &key);

    // --- National squad / shortlist (sets of player ids) ---
    QList<int> nationalSquadIds();
    bool setNationalSquadIds(const QList<int> &ids);
    QList<int> shortlistIds();
    bool setShortlistIds(const QList<int> &ids);

    // --- Training roles (player id -> chosen training-advice role) ---
    QHash<int, QString> trainingRoles();
    // Empty role removes the row (revert to auto-pick).
    bool setTrainingRole(int playerId, const QString &role);

    // --- Maintenance ---
    // Copies the db file to <backupsDir>/<name>_backup_<ts>.db, keeps newest 3.
    static bool createBackup(const QString &dbFilePath, const QString &backupsDir,
                             QString *errorOut = nullptr);

    QSqlDatabase &handle() { return m_db; }

private:
    bool initSchema();
    bool createInitialSchema();          // fresh DB -> current shape
    bool migrateV1ToV2();                 // add dwrs_latest, drop dead columns
    bool migrateV2ToV3();                 // add last_seen_update (freshness tracking)
    bool migrateV3ToV4();                 // add training_roles table
    static QString createDwrsLatestSql(); // shared by create + migrate paths
    bool exec(const QString &sql);
    // Runs `sqlTemplate` (with one "%1" for a "?,?,…" id placeholder list) for
    // the ids in chunks, keeping every statement below SQLite's bound-variable
    // limit; onRow is called for each result row.
    bool forEachIdChunk(const QList<int> &ids, const QString &sqlTemplate,
                        const QVariantList &trailingBinds,
                        const std::function<void(const QSqlQuery &)> &onRow);

    QSqlDatabase m_db;
    QString m_connectionName;
    QString m_filePath;
    QString m_error;
    int m_transactionDepth = 0; // 0 = none, 1 = outer, >1 = savepoints

    // In-memory cache of the settings table (queried several times per page
    // refresh). m_settingsLoaded marks keys whose DB state is known; a loaded
    // key absent from m_settingsCache means "no row" (returns the caller's
    // default). Kept coherent through setSetting()/removeSetting(). Per-instance
    // and therefore per-connection/thread, so no locking is needed.
    QHash<QString, QString> m_settingsCache;
    QSet<QString> m_settingsLoaded;
};

// RAII outer transaction: rolls back on scope exit unless commit() succeeded.
class ScopedTransaction
{
public:
    explicit ScopedTransaction(Database &db)
        : m_db(db)
        , m_active(db.beginTransaction())
    {
    }
    ~ScopedTransaction()
    {
        if (m_active)
            m_db.rollbackTransaction();
    }
    ScopedTransaction(const ScopedTransaction &) = delete;
    ScopedTransaction &operator=(const ScopedTransaction &) = delete;

    bool isActive() const { return m_active; }
    bool commit()
    {
        if (!m_active)
            return false;
        m_active = false;
        return m_db.commitTransaction();
    }

private:
    Database &m_db;
    bool m_active;
};

} // namespace fm
