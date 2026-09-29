#include "Database.h"

#include "Constants.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>

#include <algorithm>
#include <optional>
#include <atomic>

namespace fm {

namespace {

constexpr int kSchemaVersion = 4;

QString joinRolesForDb(const QStringList &roles)
{
    // player_roles junction handles roles; this helper is for natural_positions.
    return roles.join(QLatin1Char('\x1f')); // unit separator: never occurs in FM data
}

QStringList splitRolesFromDb(const QString &value)
{
    if (value.isEmpty())
        return {};
    return value.split(QLatin1Char('\x1f'), Qt::SkipEmptyParts);
}

} // namespace

QString Database::attrColumnName(const QString &fullAttrName)
{
    QString s = fullAttrName.toLower();
    s.remove(QLatin1Char('('));
    s.remove(QLatin1Char(')'));
    s.replace(QLatin1Char(' '), QLatin1Char('_'));
    // "rushing_out_(tendency)" collapses to "rushing_out_tendency" via the
    // parenthesis removal; double underscores cannot occur in our names.
    return s;
}

Database::Database(const QString &connectionName)
    : m_connectionName(connectionName)
{
}

QString Database::uniqueConnectionName(const QString &prefix)
{
    static std::atomic<int> counter{0};
    return QStringLiteral("%1_%2").arg(prefix).arg(++counter);
}

Database::~Database()
{
    close();
}

bool Database::open(const QString &filePath)
{
    m_error.clear();
    m_filePath = filePath;

    QDir().mkpath(QFileInfo(filePath).absolutePath());

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(filePath);
    if (!m_db.open()) {
        m_error = m_db.lastError().text();
        return false;
    }
    return initSchema();
}

void Database::close()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
}

bool Database::isOpen() const
{
    return m_db.isOpen();
}

bool Database::beginTransaction()
{
    if (m_transactionDepth == 0) {
        if (!m_db.transaction()) {
            m_error = m_db.lastError().text();
            return false;
        }
    } else if (!exec(QStringLiteral("SAVEPOINT sp_%1").arg(m_transactionDepth))) {
        return false;
    }
    ++m_transactionDepth;
    return true;
}

bool Database::commitTransaction()
{
    if (m_transactionDepth <= 0)
        return false;
    --m_transactionDepth;
    if (m_transactionDepth == 0) {
        if (!m_db.commit()) {
            m_error = m_db.lastError().text();
            // A failed COMMIT leaves the transaction open; undo it so the
            // connection is usable and memory matches the DB again.
            m_db.rollback();
            m_settingsCache.clear();
            m_settingsLoaded.clear();
            return false;
        }
        return true;
    }
    return exec(QStringLiteral("RELEASE SAVEPOINT sp_%1").arg(m_transactionDepth));
}

void Database::rollbackTransaction()
{
    if (m_transactionDepth <= 0)
        return;
    --m_transactionDepth;
    if (m_transactionDepth == 0) {
        m_db.rollback();
        // setSetting() inside the rolled-back transaction updated the cache;
        // drop it so the next read comes from the (restored) table.
        m_settingsCache.clear();
        m_settingsLoaded.clear();
        return;
    }
    // Keep m_error from the failure that caused the rollback.
    const QString error = m_error;
    exec(QStringLiteral("ROLLBACK TO SAVEPOINT sp_%1").arg(m_transactionDepth));
    exec(QStringLiteral("RELEASE SAVEPOINT sp_%1").arg(m_transactionDepth));
    m_error = error;
}

bool Database::exec(const QString &sql)
{
    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        m_error = query.lastError().text() + QStringLiteral(" [SQL: ") + sql
                  + QLatin1Char(']');
        return false;
    }
    return true;
}

bool Database::initSchema()
{
    exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
    exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    // 64 MB page cache per connection (SQLite default: ~2 MB). Large imports
    // and recalcs insert 100k+ rows into indexes of a 100+ MB database; with
    // the default cache most of those b-tree pages are re-read from disk.
    exec(QStringLiteral("PRAGMA cache_size = -65536"));

    QSqlQuery versionQuery(m_db);
    versionQuery.exec(QStringLiteral("PRAGMA user_version"));
    int version = 0;
    if (versionQuery.next())
        version = versionQuery.value(0).toInt();

    if (version >= kSchemaVersion)
        return true;

    if (!beginTransaction())
        return false;

    // Fresh database (version 0): create everything at the current shape.
    // Existing database: apply the pending migration steps in order. A fresh DB
    // is created directly at the current shape, so its create path already
    // includes everything the incremental migrations would add.
    bool ok = true;
    if (version < 1) {
        ok = createInitialSchema();
    } else {
        if (ok && version < 2)
            ok = migrateV1ToV2();
        if (ok && version < 3)
            ok = migrateV2ToV3();
        if (ok && version < 4)
            ok = migrateV3ToV4();
    }
    if (!ok || !exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion))) {
        rollbackTransaction();
        return false;
    }
    return commitTransaction();
}

bool Database::createInitialSchema()
{
    QString attrColumns;
    for (const QString &name : attrNames()) {
        const QString base = attrColumnName(name);
        attrColumns += QStringLiteral("  %1_lo INTEGER, %1_hi INTEGER,\n").arg(base);
    }

    // Trailing attribute columns leave no dangling comma, so the last fixed
    // column carries the comma and the attribute block closes the row.
    const QString createPlayers = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS players (\n"
        "  id INTEGER PRIMARY KEY,\n"
        "  uid TEXT NOT NULL UNIQUE,\n"
        "  name TEXT NOT NULL DEFAULT '',\n"
        "  age INTEGER,\n"
        "  club TEXT,\n"
        "  nationality TEXT,\n"
        "  second_nationality TEXT,\n"
        "  position_raw TEXT,\n"
        "  personality TEXT,\n"
        "  media_handling TEXT,\n"
        "  agreed_playing_time TEXT,\n"
        "  wage_raw TEXT,\n"
        "  transfer_value_raw TEXT,\n"
        "  transfer_value REAL,\n"
        "  av_rating REAL,\n"
        "  height_raw TEXT,\n"
        "  height_cm INTEGER,\n"
        "  left_foot TEXT,\n"
        "  right_foot TEXT,\n"
        "  preferred_foot TEXT,\n"
        "  preferred_side TEXT,\n"
        "  primary_role TEXT,\n"
        "  natural_positions TEXT,\n"
        "  last_seen_update INTEGER NOT NULL DEFAULT 0,\n"
        "  transfer_status INTEGER NOT NULL DEFAULT 0,\n"
        "  loan_status INTEGER NOT NULL DEFAULT 0,\n"
        "  new_club TEXT%1\n"
        ")").arg(attrColumns.isEmpty()
                     ? QString()
                     : QStringLiteral(",\n") + attrColumns.chopped(2)); // strip trailing ",\n"

    const QStringList statements = {
        createPlayers,
        QStringLiteral("CREATE TABLE IF NOT EXISTS player_roles ("
                       " player_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,"
                       " role TEXT NOT NULL,"
                       " PRIMARY KEY (player_id, role)) WITHOUT ROWID"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS dwrs_history ("
                       " player_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,"
                       " role TEXT NOT NULL,"
                       " absolute REAL NOT NULL,"
                       " normalized REAL NOT NULL,"
                       " ts TEXT NOT NULL,"
                       " PRIMARY KEY (player_id, role, ts))"),
        createDwrsLatestSql(),
        QStringLiteral("CREATE TABLE IF NOT EXISTS settings ("
                       " key TEXT PRIMARY KEY, value TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS national_squad ("
                       " player_id INTEGER PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS shortlist ("
                       " player_id INTEGER PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS training_roles ("
                       " player_id INTEGER PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,"
                       " role TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_dwrs_history_role_ts ON dwrs_history(role, ts)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_player_roles_role ON player_roles(role)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_players_club ON players(club)"),
    };
    for (const QString &sql : statements) {
        if (!exec(sql))
            return false;
    }
    return true;
}

QString Database::createDwrsLatestSql()
{
    // Materialized "latest rating per (player, role)": maintained on every
    // append so latestDwrsRatings() is a plain scan instead of a MAX(ts)
    // self-join over the whole history (v1.2.6 perf fix).
    return QStringLiteral("CREATE TABLE IF NOT EXISTS dwrs_latest ("
                          " player_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,"
                          " role TEXT NOT NULL,"
                          " absolute REAL NOT NULL,"
                          " normalized REAL NOT NULL,"
                          " ts TEXT NOT NULL,"
                          " PRIMARY KEY (player_id, role)) WITHOUT ROWID");
}

bool Database::migrateV1ToV2()
{
    // 1) Materialize the latest-ratings table and seed it from the existing
    //    history (the one-time MAX(ts) self-join we are eliminating).
    if (!exec(createDwrsLatestSql()))
        return false;
    if (!exec(QStringLiteral(
            "INSERT INTO dwrs_latest (player_id, role, absolute, normalized, ts) "
            "SELECT h.player_id, h.role, h.absolute, h.normalized, h.ts "
            "FROM dwrs_history h "
            "JOIN (SELECT player_id, role, MAX(ts) AS mt FROM dwrs_history "
            "      GROUP BY player_id, role) m "
            "ON h.player_id = m.player_id AND h.role = m.role AND h.ts = m.mt")))
        return false;

    // 2) Drop the never-read legacy columns (dead since the initial port).
    if (!exec(QStringLiteral("ALTER TABLE players DROP COLUMN registration")))
        return false;
    if (!exec(QStringLiteral("ALTER TABLE players DROP COLUMN information")))
        return false;
    return true;
}

bool Database::migrateV2ToV3()
{
    // Data-freshness tracking: stamp of the upload counter at which each player
    // was last present. Existing rows default to 0 ("never stamped"), which
    // Freshness treats as last seen at upload 0 — they age from the first
    // tracked upload on.
    return exec(QStringLiteral(
        "ALTER TABLE players ADD COLUMN last_seen_update INTEGER NOT NULL DEFAULT 0"));
}

bool Database::migrateV3ToV4()
{
    // Per-player training-advice role (id -> role), managed by the training
    // planner, independent of the FM-import upsert.
    return exec(QStringLiteral("CREATE TABLE IF NOT EXISTS training_roles ("
                               " player_id INTEGER PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,"
                               " role TEXT NOT NULL)"));
}

std::vector<Player> Database::loadPlayers()
{
    return loadPlayersImpl(nullptr);
}

std::vector<Player> Database::loadPlayers(const QList<int> &playerIds)
{
    if (playerIds.isEmpty())
        return {};
    return loadPlayersImpl(&playerIds);
}

std::vector<Player> Database::loadPlayersImpl(const QList<int> *ids)
{
    std::vector<Player> players;

    // Runs `sql` over everything (ids == nullptr) or over the id subset; the
    // subset form must contain "%1" for the chunked "IN (…)" placeholder list.
    const auto forRows = [this, ids](const QString &allSql, const QString &subsetSql,
                                     const std::function<void(const QSqlQuery &)> &onRow) {
        if (ids)
            return forEachIdChunk(*ids, subsetSql, {}, onRow);
        QSqlQuery query(m_db);
        query.setForwardOnly(true);
        if (!query.exec(allSql)) {
            m_error = query.lastError().text();
            return false;
        }
        while (query.next())
            onRow(query);
        return true;
    };

    // App-managed side tables first: player_id -> roles / flags / training role.
    QHash<int, QStringList> rolesByPlayer;
    forRows(QStringLiteral("SELECT player_id, role FROM player_roles ORDER BY player_id, role"),
            QStringLiteral("SELECT player_id, role FROM player_roles WHERE player_id IN (%1) "
                           "ORDER BY player_id, role"),
            [&](const QSqlQuery &q) {
                rolesByPlayer[q.value(0).toInt()].append(q.value(1).toString());
            });
    QSet<int> nationalIds, shortlistIdSet;
    QHash<int, QString> trainingRoleById;
    forRows(QStringLiteral("SELECT player_id FROM national_squad"),
            QStringLiteral("SELECT player_id FROM national_squad WHERE player_id IN (%1)"),
            [&](const QSqlQuery &q) { nationalIds.insert(q.value(0).toInt()); });
    forRows(QStringLiteral("SELECT player_id FROM shortlist"),
            QStringLiteral("SELECT player_id FROM shortlist WHERE player_id IN (%1)"),
            [&](const QSqlQuery &q) { shortlistIdSet.insert(q.value(0).toInt()); });
    forRows(QStringLiteral("SELECT player_id, role FROM training_roles"),
            QStringLiteral("SELECT player_id, role FROM training_roles WHERE player_id IN (%1)"),
            [&](const QSqlQuery &q) {
                trainingRoleById.insert(q.value(0).toInt(), q.value(1).toString());
            });

    // Column indexes, resolved once from the first result row's record.
    struct Columns {
        int id, uid, name, age, club, nat, nat2, pos, pers, media, apt, wage, tvRaw, tv, avr,
            heightRaw, height, lf, rf, pf, side, prim, natPos, lastSeen, ts, ls, newClub;
        std::array<int, kAttrCount> lo, hi;
    };
    std::optional<Columns> columns;
    const auto resolve = [](const QSqlRecord &record) {
        const auto col = [&](const char *name) { return record.indexOf(QLatin1String(name)); };
        Columns c{};
        c.id = col("id");
        c.uid = col("uid");
        c.name = col("name");
        c.age = col("age");
        c.club = col("club");
        c.nat = col("nationality");
        c.nat2 = col("second_nationality");
        c.pos = col("position_raw");
        c.pers = col("personality");
        c.media = col("media_handling");
        c.apt = col("agreed_playing_time");
        c.wage = col("wage_raw");
        c.tvRaw = col("transfer_value_raw");
        c.tv = col("transfer_value");
        c.avr = col("av_rating");
        c.heightRaw = col("height_raw");
        c.height = col("height_cm");
        c.lf = col("left_foot");
        c.rf = col("right_foot");
        c.pf = col("preferred_foot");
        c.side = col("preferred_side");
        c.prim = col("primary_role");
        c.natPos = col("natural_positions");
        c.lastSeen = col("last_seen_update");
        c.ts = col("transfer_status");
        c.ls = col("loan_status");
        c.newClub = col("new_club");
        for (int i = 0; i < kAttrCount; ++i) {
            const QString base = attrColumnName(attrNames()[i]);
            c.lo[i] = record.indexOf(base + QStringLiteral("_lo"));
            c.hi[i] = record.indexOf(base + QStringLiteral("_hi"));
        }
        return c;
    };

    const bool ok = forRows(
        QStringLiteral("SELECT * FROM players"),
        QStringLiteral("SELECT * FROM players WHERE id IN (%1) ORDER BY id"),
        [&](const QSqlQuery &query) {
            if (!columns)
                columns = resolve(query.record());
            const Columns &c = *columns;
            Player p;
            p.id = query.value(c.id).toInt();
            p.uid = query.value(c.uid).toString();
            p.name = query.value(c.name).toString();
            p.age = query.value(c.age).toInt();
            p.club = query.value(c.club).toString();
            p.nationality = query.value(c.nat).toString();
            p.secondNationality = query.value(c.nat2).toString();
            p.positionRaw = query.value(c.pos).toString();
            p.personality = query.value(c.pers).toString();
            p.mediaHandling = query.value(c.media).toString();
            p.agreedPlayingTime = query.value(c.apt).toString();
            p.wageRaw = query.value(c.wage).toString();
            p.transferValueRaw = query.value(c.tvRaw).toString();
            p.transferValue = query.value(c.tv).toDouble();
            p.averageRating = query.value(c.avr).toDouble();
            p.heightRaw = query.value(c.heightRaw).toString();
            p.heightCm = query.value(c.height).toInt();
            p.leftFoot = query.value(c.lf).toString();
            p.rightFoot = query.value(c.rf).toString();
            p.preferredFoot = query.value(c.pf).toString();
            p.preferredSide = query.value(c.side).toString();
            p.primaryRole = query.value(c.prim).toString();
            p.naturalPositions = splitRolesFromDb(query.value(c.natPos).toString());
            p.lastSeenUpdate = query.value(c.lastSeen).toInt();
            p.transferStatus = query.value(c.ts).toInt() != 0;
            p.loanStatus = query.value(c.ls).toInt() != 0;
            p.newClub = query.value(c.newClub).toString();
            for (int i = 0; i < kAttrCount; ++i) {
                p.attrLo[i] = static_cast<uint8_t>(query.value(c.lo[i]).toInt());
                p.attrHi[i] = static_cast<uint8_t>(query.value(c.hi[i]).toInt());
            }
            p.assignedRoles = rolesByPlayer.value(p.id);
            p.inNationalSquad = nationalIds.contains(p.id);
            p.onShortlist = shortlistIdSet.contains(p.id);
            p.trainingRole = trainingRoleById.value(p.id);
            players.push_back(std::move(p));
        });
    if (!ok)
        players.clear();
    return players;
}

bool Database::upsertPlayers(std::vector<Player> &players, RoleWrite roles)
{
    if (players.empty())
        return true;

    if (!beginTransaction())
        return false;

    QStringList baseColumns = {
        QStringLiteral("uid"), QStringLiteral("name"), QStringLiteral("age"),
        QStringLiteral("club"), QStringLiteral("nationality"), QStringLiteral("second_nationality"),
        QStringLiteral("position_raw"), QStringLiteral("personality"), QStringLiteral("media_handling"),
        QStringLiteral("agreed_playing_time"), QStringLiteral("wage_raw"),
        QStringLiteral("transfer_value_raw"), QStringLiteral("transfer_value"),
        QStringLiteral("av_rating"), QStringLiteral("height_raw"), QStringLiteral("height_cm"),
        QStringLiteral("left_foot"), QStringLiteral("right_foot"), QStringLiteral("preferred_foot"),
        QStringLiteral("preferred_side"), QStringLiteral("primary_role"),
        QStringLiteral("natural_positions"), QStringLiteral("last_seen_update"),
        QStringLiteral("transfer_status"), QStringLiteral("loan_status"),
        QStringLiteral("new_club"),
    };
    QStringList allColumns = baseColumns;
    for (const QString &name : attrNames()) {
        const QString base = attrColumnName(name);
        allColumns << base + QStringLiteral("_lo") << base + QStringLiteral("_hi");
    }

    QStringList placeholders;
    for (int i = 0; i < allColumns.size(); ++i)
        placeholders << QStringLiteral("?");

    // Upsert on the uid unique constraint keeps ids stable across re-imports.
    QStringList updateClauses;
    for (const QString &column : allColumns) {
        if (column != QLatin1String("uid"))
            updateClauses << QStringLiteral("%1=excluded.%1").arg(column);
    }

    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral("INSERT INTO players (%1) VALUES (%2) "
                                      "ON CONFLICT(uid) DO UPDATE SET %3")
                           .arg(allColumns.join(QStringLiteral(", ")),
                                placeholders.join(QStringLiteral(", ")),
                                updateClauses.join(QStringLiteral(", "))))) {
        m_error = query.lastError().text();
        rollbackTransaction();
        return false;
    }

    QSqlQuery idQuery(m_db);
    idQuery.prepare(QStringLiteral("SELECT id FROM players WHERE uid = ?"));

    QSqlQuery deleteRoles(m_db);
    deleteRoles.prepare(QStringLiteral("DELETE FROM player_roles WHERE player_id = ?"));
    QSqlQuery insertRole(m_db);
    insertRole.prepare(
        QStringLiteral("INSERT OR IGNORE INTO player_roles (player_id, role) VALUES (?, ?)"));

    for (Player &p : players) {
        int i = 0;
        query.bindValue(i++, p.uid);
        query.bindValue(i++, p.name);
        query.bindValue(i++, p.age);
        query.bindValue(i++, p.club);
        query.bindValue(i++, p.nationality);
        query.bindValue(i++, p.secondNationality);
        query.bindValue(i++, p.positionRaw);
        query.bindValue(i++, p.personality);
        query.bindValue(i++, p.mediaHandling);
        query.bindValue(i++, p.agreedPlayingTime);
        query.bindValue(i++, p.wageRaw);
        query.bindValue(i++, p.transferValueRaw);
        query.bindValue(i++, p.transferValue);
        query.bindValue(i++, p.averageRating);
        query.bindValue(i++, p.heightRaw);
        query.bindValue(i++, p.heightCm);
        query.bindValue(i++, p.leftFoot);
        query.bindValue(i++, p.rightFoot);
        query.bindValue(i++, p.preferredFoot);
        query.bindValue(i++, p.preferredSide);
        query.bindValue(i++, p.primaryRole);
        query.bindValue(i++, joinRolesForDb(p.naturalPositions));
        query.bindValue(i++, p.lastSeenUpdate);
        query.bindValue(i++, p.transferStatus ? 1 : 0);
        query.bindValue(i++, p.loanStatus ? 1 : 0);
        query.bindValue(i++, p.newClub);
        for (int a = 0; a < kAttrCount; ++a) {
            query.bindValue(i++, static_cast<int>(p.attrLo[a]));
            query.bindValue(i++, static_cast<int>(p.attrHi[a]));
        }
        if (!query.exec()) {
            m_error = query.lastError().text();
            rollbackTransaction();
            return false;
        }

        if (p.id == 0) {
            idQuery.bindValue(0, p.uid);
            if (!idQuery.exec() || !idQuery.next()) {
                m_error = idQuery.lastError().text();
                rollbackTransaction();
                return false;
            }
            p.id = idQuery.value(0).toInt();
        }

        if (roles == RoleWrite::Keep)
            continue;
        deleteRoles.bindValue(0, p.id);
        if (!deleteRoles.exec()) {
            m_error = deleteRoles.lastError().text();
            rollbackTransaction();
            return false;
        }
        for (const QString &role : p.assignedRoles) {
            insertRole.bindValue(0, p.id);
            insertRole.bindValue(1, role);
            if (!insertRole.exec()) {
                m_error = insertRole.lastError().text();
                rollbackTransaction();
                return false;
            }
        }
    }

    return commitTransaction();
}

bool Database::deletePlayers(const QList<int> &playerIds)
{
    if (playerIds.isEmpty())
        return true;
    if (!beginTransaction())
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM players WHERE id = ?"));
    for (const int id : playerIds) {
        query.bindValue(0, id);
        if (!query.exec()) {
            m_error = query.lastError().text();
            rollbackTransaction();
            return false;
        }
    }
    return commitTransaction();
}

bool Database::renamePlayerUid(int playerId, const QString &newUid)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE players SET uid = ? WHERE id = ?"));
    query.bindValue(0, newUid);
    query.bindValue(1, playerId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return false;
    }
    return true;
}

bool Database::mergePlayerInto(int badPlayerId, int goodPlayerId)
{
    if (badPlayerId == goodPlayerId)
        return true;
    if (!beginTransaction())
        return false;
    // OR IGNORE skips history rows that would collide on (player, role, ts);
    // leftovers under the bad id are removed with the player row (CASCADE).
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE OR IGNORE dwrs_history SET player_id = ? WHERE player_id = ?"));
    query.bindValue(0, goodPlayerId);
    query.bindValue(1, badPlayerId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        rollbackTransaction();
        return false;
    }

    // The good player's history just grew, so recompute its dwrs_latest rows
    // from the merged history. The bad id's dwrs_latest is removed with its
    // player row below (CASCADE).
    query.prepare(QStringLiteral("DELETE FROM dwrs_latest WHERE player_id = ?"));
    query.bindValue(0, goodPlayerId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        rollbackTransaction();
        return false;
    }
    query.prepare(QStringLiteral(
        "INSERT INTO dwrs_latest (player_id, role, absolute, normalized, ts) "
        "SELECT h.player_id, h.role, h.absolute, h.normalized, h.ts "
        "FROM dwrs_history h "
        "JOIN (SELECT role, MAX(ts) AS mt FROM dwrs_history WHERE player_id = ? "
        "      GROUP BY role) m ON h.role = m.role AND h.ts = m.mt "
        "WHERE h.player_id = ?"));
    query.bindValue(0, goodPlayerId);
    query.bindValue(1, goodPlayerId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        rollbackTransaction();
        return false;
    }

    query.prepare(QStringLiteral("DELETE FROM players WHERE id = ?"));
    query.bindValue(0, badPlayerId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        rollbackTransaction();
        return false;
    }
    return commitTransaction();
}

bool Database::replacePlayerRoles(const std::vector<std::pair<int, QStringList>> &rolesById)
{
    if (rolesById.empty())
        return true;
    if (!beginTransaction())
        return false;
    QSqlQuery deleteRoles(m_db);
    deleteRoles.prepare(QStringLiteral("DELETE FROM player_roles WHERE player_id = ?"));
    QSqlQuery insertRole(m_db);
    insertRole.prepare(
        QStringLiteral("INSERT OR IGNORE INTO player_roles (player_id, role) VALUES (?, ?)"));
    for (const auto &[playerId, roles] : rolesById) {
        deleteRoles.bindValue(0, playerId);
        if (!deleteRoles.exec()) {
            m_error = deleteRoles.lastError().text();
            rollbackTransaction();
            return false;
        }
        for (const QString &role : roles) {
            insertRole.bindValue(0, playerId);
            insertRole.bindValue(1, role);
            if (!insertRole.exec()) {
                m_error = insertRole.lastError().text();
                rollbackTransaction();
                return false;
            }
        }
    }
    return commitTransaction();
}

bool Database::appendDwrsRatings(const std::vector<DwrsEntry> &entries)
{
    if (entries.empty())
        return true;
    if (!beginTransaction())
        return false;

    // Multi-row statements: one exec per chunk instead of two per rating. The
    // per-exec overhead of QSqlQuery dominated a big recalc (~160k ratings took
    // >10 s); the SQL work itself is unchanged. 5 binds per row keeps a chunk
    // far below SQLite's bound-variable limit.
    constexpr int kChunkRows = 200;
    const auto rowsSql = [](int rows) {
        QStringList values;
        values.reserve(rows);
        for (int i = 0; i < rows; ++i)
            values << QStringLiteral("(?, ?, ?, ?, ?)");
        return values.join(QStringLiteral(", "));
    };
    const auto historySql = [&](int rows) {
        return QStringLiteral("INSERT OR REPLACE INTO dwrs_history "
                              "(player_id, role, absolute, normalized, ts) VALUES ")
               + rowsSql(rows);
    };
    // Keep the materialized latest table in sync: overwrite the (player, role)
    // row only when the entry is at least as new as the stored one, so an
    // out-of-order append can never make dwrs_latest go backwards. Rows of one
    // statement are applied in order, exactly like the former per-row upsert.
    const auto latestSql = [&](int rows) {
        return QStringLiteral("INSERT INTO dwrs_latest "
                              "(player_id, role, absolute, normalized, ts) VALUES ")
               + rowsSql(rows)
               + QStringLiteral(" ON CONFLICT(player_id, role) DO UPDATE SET "
                                "  absolute = excluded.absolute, "
                                "  normalized = excluded.normalized, ts = excluded.ts "
                                "WHERE excluded.ts >= dwrs_latest.ts");
    };

    QSqlQuery fullHistory(m_db), fullLatest(m_db);
    fullHistory.prepare(historySql(kChunkRows));
    fullLatest.prepare(latestSql(kChunkRows));

    const int total = static_cast<int>(entries.size());
    for (int start = 0; start < total; start += kChunkRows) {
        const int rows = std::min(kChunkRows, total - start);
        QSqlQuery partialHistory(m_db), partialLatest(m_db);
        if (rows != kChunkRows) {
            partialHistory.prepare(historySql(rows));
            partialLatest.prepare(latestSql(rows));
        }
        QSqlQuery &history = rows == kChunkRows ? fullHistory : partialHistory;
        QSqlQuery &latest = rows == kChunkRows ? fullLatest : partialLatest;
        for (QSqlQuery *statement : {&history, &latest}) {
            int bind = 0;
            for (int r = start; r < start + rows; ++r) {
                const DwrsEntry &entry = entries[static_cast<size_t>(r)];
                statement->bindValue(bind++, entry.playerId);
                statement->bindValue(bind++, entry.role);
                statement->bindValue(bind++, entry.absolute);
                statement->bindValue(bind++, entry.normalized);
                statement->bindValue(bind++, entry.timestamp);
            }
            if (!statement->exec()) {
                m_error = statement->lastError().text();
                rollbackTransaction();
                return false;
            }
        }
    }
    return commitTransaction();
}

bool Database::forEachIdChunk(const QList<int> &ids, const QString &sqlTemplate,
                              const QVariantList &trailingBinds,
                              const std::function<void(const QSqlQuery &)> &onRow)
{
    // Sorted + unique, so per-chunk "ORDER BY player_id, …" results concatenate
    // into one globally ordered result.
    QList<int> sorted = ids;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    constexpr int kChunkIds = 500;
    for (int start = 0; start < sorted.size(); start += kChunkIds) {
        const int count = std::min(kChunkIds, static_cast<int>(sorted.size()) - start);
        QStringList placeholders;
        placeholders.reserve(count);
        for (int i = 0; i < count; ++i)
            placeholders << QStringLiteral("?");
        QSqlQuery query(m_db);
        query.setForwardOnly(true);
        if (!query.prepare(sqlTemplate.arg(placeholders.join(QLatin1Char(','))))) {
            m_error = query.lastError().text();
            return false;
        }
        int bind = 0;
        for (int i = start; i < start + count; ++i)
            query.bindValue(bind++, sorted.at(i));
        for (const QVariant &value : trailingBinds)
            query.bindValue(bind++, value);
        if (!query.exec()) {
            m_error = query.lastError().text();
            return false;
        }
        while (query.next())
            onRow(query);
    }
    return true;
}

LatestRatings Database::latestDwrsRatings(const QList<int> &playerIds)
{
    LatestRatings result;
    forEachIdChunk(playerIds,
                   QStringLiteral("SELECT player_id, role, absolute, normalized "
                                  "FROM dwrs_latest WHERE player_id IN (%1)"),
                   {}, [&result](const QSqlQuery &query) {
                       result.insert({query.value(0).toInt(), query.value(1).toString()},
                                     {query.value(2).toDouble(), query.value(3).toDouble()});
                   });
    return result;
}

LatestRatings Database::latestDwrsRatings()
{
    LatestRatings result;
    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    // dwrs_latest already holds the latest row per (player, role), so this is a
    // plain scan — the MAX(ts) self-join over the whole history is gone.
    query.exec(QStringLiteral("SELECT player_id, role, absolute, normalized FROM dwrs_latest"));
    while (query.next()) {
        result.insert({query.value(0).toInt(), query.value(1).toString()},
                      {query.value(2).toDouble(), query.value(3).toDouble()});
    }
    return result;
}

std::vector<DwrsEntry> Database::dwrsHistory(const QList<int> &playerIds, const QString &role)
{
    std::vector<DwrsEntry> result;
    if (playerIds.isEmpty())
        return result;

    // Chunked IN (…) lists: an unbounded list could exceed SQLite's
    // bound-variable limit for very large id sets.
    const bool oneRole = !role.isEmpty() && role != QLatin1String("All Roles");
    QString sql = QStringLiteral("SELECT player_id, role, absolute, normalized, ts "
                                 "FROM dwrs_history WHERE player_id IN (%1)");
    if (oneRole)
        sql += QStringLiteral(" AND role = ?");
    sql += QStringLiteral(" ORDER BY player_id, role, ts");

    forEachIdChunk(playerIds, sql, oneRole ? QVariantList{role} : QVariantList{},
                   [&result](const QSqlQuery &query) {
                       DwrsEntry entry;
                       entry.playerId = query.value(0).toInt();
                       entry.role = query.value(1).toString();
                       entry.absolute = query.value(2).toDouble();
                       entry.normalized = query.value(3).toDouble();
                       entry.timestamp = query.value(4).toString();
                       result.push_back(std::move(entry));
                   });
    return result;
}

QString Database::setting(const QString &key, const QString &defaultValue)
{
    if (m_settingsLoaded.contains(key))
        return m_settingsCache.value(key, defaultValue);

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT value FROM settings WHERE key = ?"));
    query.bindValue(0, key);
    query.exec();
    m_settingsLoaded.insert(key);
    if (query.next()) {
        const QString value = query.value(0).toString();
        m_settingsCache.insert(key, value);
        return value;
    }
    return defaultValue;
}

bool Database::setSetting(const QString &key, const QString &value)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)"));
    query.bindValue(0, key);
    query.bindValue(1, value);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return false;
    }
    m_settingsCache.insert(key, value);
    m_settingsLoaded.insert(key);
    return true;
}

bool Database::removeSetting(const QString &key)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM settings WHERE key = ?"));
    query.bindValue(0, key);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return false;
    }
    m_settingsCache.remove(key);
    m_settingsLoaded.insert(key); // known-absent: subsequent reads skip the DB
    return true;
}

QList<int> Database::nationalSquadIds()
{
    QList<int> ids;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT player_id FROM national_squad"));
    while (query.next())
        ids.append(query.value(0).toInt());
    return ids;
}

bool Database::setNationalSquadIds(const QList<int> &ids)
{
    if (!beginTransaction())
        return false;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM national_squad"))) {
        m_error = query.lastError().text();
        rollbackTransaction();
        return false;
    }
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO national_squad (player_id) VALUES (?)"));
    for (const int id : ids) {
        query.bindValue(0, id);
        if (!query.exec()) {
            m_error = query.lastError().text();
            rollbackTransaction();
            return false;
        }
    }
    return commitTransaction();
}

QList<int> Database::shortlistIds()
{
    QList<int> ids;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT player_id FROM shortlist"));
    while (query.next())
        ids.append(query.value(0).toInt());
    return ids;
}

bool Database::setShortlistIds(const QList<int> &ids)
{
    if (!beginTransaction())
        return false;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM shortlist"))) {
        m_error = query.lastError().text();
        rollbackTransaction();
        return false;
    }
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO shortlist (player_id) VALUES (?)"));
    for (const int id : ids) {
        query.bindValue(0, id);
        if (!query.exec()) {
            m_error = query.lastError().text();
            rollbackTransaction();
            return false;
        }
    }
    return commitTransaction();
}

QHash<int, QString> Database::trainingRoles()
{
    QHash<int, QString> result;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT player_id, role FROM training_roles"));
    while (query.next())
        result.insert(query.value(0).toInt(), query.value(1).toString());
    return result;
}

bool Database::setTrainingRole(int playerId, const QString &role)
{
    QSqlQuery query(m_db);
    if (role.isEmpty()) {
        query.prepare(QStringLiteral("DELETE FROM training_roles WHERE player_id = ?"));
        query.bindValue(0, playerId);
    } else {
        query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO training_roles (player_id, role) VALUES (?, ?)"));
        query.bindValue(0, playerId);
        query.bindValue(1, role);
    }
    if (!query.exec()) {
        m_error = query.lastError().text();
        return false;
    }
    return true;
}

bool Database::createBackup(const QString &dbFilePath, const QString &backupsDir, QString *errorOut)
{
    if (!QFile::exists(dbFilePath))
        return true; // nothing to back up

    QDir().mkpath(backupsDir);
    const QString baseName = QFileInfo(dbFilePath).completeBaseName();
    const QString timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    const QString backupName =
        QStringLiteral("%1_backup_%2.db").arg(baseName, timestamp);
    const QString backupPath = QDir(backupsDir).filePath(backupName);

    // VACUUM INTO writes a transactionally consistent snapshot that already
    // folds in any committed WAL frames. A plain file copy would miss data
    // still living in the -wal sidecar (the DB runs in WAL mode), so a
    // pre-import backup could silently lose the latest changes.
    bool ok = false;
    QString error;
    const QString connName = QStringLiteral("backup_%1").arg(timestamp);
    {
        QSqlDatabase src = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        src.setDatabaseName(dbFilePath);
        if (!src.open()) {
            error = src.lastError().text();
        } else {
            QSqlQuery query(src);
            query.prepare(QStringLiteral("VACUUM INTO ?"));
            query.bindValue(0, backupPath);
            ok = query.exec();
            if (!ok)
                error = query.lastError().text();
            src.close();
        }
    }
    QSqlDatabase::removeDatabase(connName);

    if (!ok) {
        if (errorOut)
            *errorOut = QStringLiteral("Backup (VACUUM INTO) failed: %1").arg(error);
        return false;
    }

    // Rotation: keep the 3 newest backups of this database.
    QStringList backups = QDir(backupsDir)
                              .entryList({QStringLiteral("%1_backup_*.db").arg(baseName)},
                                         QDir::Files, QDir::Name);
    while (backups.size() > 3) {
        QFile::remove(QDir(backupsDir).filePath(backups.takeFirst()));
    }
    return true;
}

} // namespace fm
