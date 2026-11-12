#include "Database.h"
#include <cmath>

#include "Constants.h"
#include "Freshness.h"
#include "Registration.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <optional>
#include <atomic>

namespace fm {

namespace {

constexpr int kSchemaVersion = 18;

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

// Reads home_grown, club_trained, u21, league_listed, uefa_listed, non_eu,
// league_blocked, uefa_blocked starting at column `first`. The fields added in
// 2.2.3 live in player_registration_extra (see createPlayerRegistrationExtraSql).
PlayerRegistration registrationFromRow(const QSqlQuery &q, int first)
{
    PlayerRegistration r;
    r.homeGrown = q.value(first).toInt() != 0;
    r.clubTrained = q.value(first + 1).toInt() != 0;
    const int u21 = q.value(first + 2).toInt();
    r.u21 = u21 == 1   ? PlayerRegistration::U21::Yes
            : u21 == 0 ? PlayerRegistration::U21::No
                       : PlayerRegistration::U21::Auto;
    r.leagueListed = q.value(first + 3).toInt() != 0;
    r.uefaListed = q.value(first + 4).toInt() != 0;
    r.nonEu = q.value(first + 5).toInt() != 0;
    r.leagueBlocked = q.value(first + 6).toInt() != 0;
    r.uefaBlocked = q.value(first + 7).toInt() != 0;
    return r;
}

// Reads youth, min_wage_exempt starting at column `first` into r.
void registrationExtraFromRow(const QSqlQuery &q, int first, PlayerRegistration &r)
{
    const int youth = q.value(first).toInt();
    r.youth = youth == 1   ? PlayerRegistration::U21::Yes
              : youth == 0 ? PlayerRegistration::U21::No
                           : PlayerRegistration::U21::Auto;
    r.minWageExempt = q.value(first + 1).toInt() != 0;
}

// The registration without the 2.2.3 fields (what player_registration holds).
PlayerRegistration withoutExtra(PlayerRegistration r)
{
    r.youth = PlayerRegistration::U21::Auto;
    r.minWageExempt = false;
    return r;
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
    // Parked 2.2.3 fields of duplicates an older version merged away.
    return initSchema() && reconcileRegistrationStash();
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

    if (version > kSchemaVersion) {
        // Written by a newer version of the app: its new fields would be lost
        // or overwritten by this one's writes.
        m_error = QStringLiteral("This database was created by a newer version of FM Player "
                                 "Analyzer (schema %1, this version knows %2). Please update "
                                 "the app.")
                      .arg(version)
                      .arg(kSchemaVersion);
        return false;
    }
    if (version == kSchemaVersion)
        return true;
    versionQuery.finish();

    // Preserve a dedicated pre-2.3.0 snapshot, including committed WAL data.
    // It is never rotated with routine backups or overwritten on a retry.
    if (version > 0 && version < 18 && m_filePath != QStringLiteral(":memory:")) {
        const QString backup = m_filePath + QStringLiteral(".pre-2.3.0-%1.db")
            .arg(QUuid::createUuid().toString(QUuid::Id128));
        QString error;
        if (!snapshotTo(m_filePath, backup, &error)) {
            m_error = QStringLiteral("Pre-migration backup failed: %1").arg(error);
            return false;
        }
    }

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
        if (ok && version < 5)
            ok = migrateV4ToV5();
        if (ok && version < 6)
            ok = migrateV5ToV6();
        if (ok && version < 7)
            ok = migrateV6ToV7();
        if (ok && version < 8)
            ok = migrateV7ToV8();
        if (ok && version < 9)
            ok = migrateV8ToV9();
        if (ok && version < 10)
            ok = migrateV9ToV10();
        if (ok && version < 11)
            ok = migrateV10ToV11();
        if (ok && version < 12)
            ok = migrateV11ToV12();
        if (ok && version < 14)
            ok = migrateToV14();
        if (ok && version < 15)
            ok = migrateToV15();
        if (ok && version < 16)
            ok = migrateToV16();
        if (ok && version < 17)
            ok = migrateToV17();
    }
    if (ok && version < 18) ok = migrateToV18();
    if (!ok || !exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion))) {
        rollbackTransaction();
        return false;
    }
    return commitTransaction();
}

bool Database::migrateToV18()
{
    QSqlQuery columns(m_db);
    if (!columns.exec(QStringLiteral("PRAGMA table_info(dwrs_latest)"))) { m_error = columns.lastError().text(); return false; }
    bool hasBasis = false;
    while (columns.next()) if (columns.value(1).toString() == QStringLiteral("basis_key")) hasBasis = true;
    columns.finish();
    if (!hasBasis && !exec(QStringLiteral("ALTER TABLE dwrs_latest ADD COLUMN basis_key TEXT NOT NULL DEFAULT ''"))) return false;
    return exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS dwrs_provenance ("
        "player_id INTEGER NOT NULL, role TEXT NOT NULL, ts TEXT NOT NULL,"
        "basis_key TEXT NOT NULL, profile_id TEXT NOT NULL, profile_revision TEXT NOT NULL,"
        "calculation_version TEXT NOT NULL, game_date TEXT, attributes BLOB NOT NULL,"
        "attribute_format TEXT NOT NULL DEFAULT 'visible-lo-hi-v1',"
        "PRIMARY KEY(player_id, role, ts),"
        "FOREIGN KEY(player_id, role, ts) REFERENCES dwrs_history(player_id, role, ts) ON DELETE CASCADE ON UPDATE CASCADE) WITHOUT ROWID"));
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
        "  save_natural_positions TEXT,\n"
        "  natural_positions_manual INTEGER NOT NULL DEFAULT 0,\n"
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
        createPlayerRegistrationSql(),
        createPlayerRegistrationExtraSql(),
        createLongTermInjuriesSql(),
        createDwrsRecalcsSql(),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_dwrs_history_role_ts ON dwrs_history(role, ts)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_player_roles_role ON player_roles(role)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_players_club ON players(club)"),
    };
    for (const QString &sql : statements + createImportHistorySql() + createV15Sql()) {
        if (!exec(sql))
            return false;
    }
    return true;
}

QStringList Database::createImportHistorySql()
{
    // imports: one row per import (the in-game date is NULL when unknown).
    // player_snapshots: per player and period ("yyyy-MM" or "S<season>"), the
    // visible data at that time; attrs = kAttrCount lo bytes, then hi bytes.
    return {
        QStringLiteral("CREATE TABLE IF NOT EXISTS imports ("
                       " id INTEGER PRIMARY KEY,"
                       " imported_at TEXT NOT NULL,"
                       " game_date TEXT,"
                       " source TEXT NOT NULL DEFAULT '',"
                       " format TEXT NOT NULL DEFAULT '',"
                       " players INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS player_snapshots ("
                       " player_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,"
                       " period TEXT NOT NULL,"
                       " game_date TEXT NOT NULL,"
                       " condensed INTEGER NOT NULL DEFAULT 0,"
                       " attrs BLOB NOT NULL,"
                       " average_rating REAL,"
                       " transfer_value REAL,"
                       " transfer_value_text TEXT,"
                       " wage TEXT,"
                       " PRIMARY KEY (player_id, period)) WITHOUT ROWID"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_player_snapshots_date"
                       " ON player_snapshots(game_date)"),
    };
}

bool Database::migrateV10ToV11()
{
    for (const QString &sql : createImportHistorySql()) {
        if (!exec(sql))
            return false;
    }
    return true;
}

bool Database::migrateToV16()
{
    QSet<QString> columns;
    QSqlQuery info(m_db);
    if (!info.exec(QStringLiteral("PRAGMA table_info(players)"))) {
        m_error = info.lastError().text();
        return false;
    }
    while (info.next())
        columns.insert(info.value(1).toString());
    if (!columns.contains(QStringLiteral("save_natural_positions"))
        && !exec(QStringLiteral("ALTER TABLE players ADD COLUMN save_natural_positions TEXT")))
        return false;
    if (!columns.contains(QStringLiteral("natural_positions_manual"))
        && !exec(QStringLiteral("ALTER TABLE players ADD COLUMN natural_positions_manual "
                                "INTEGER NOT NULL DEFAULT 0")))
        return false;
    return !columns.contains(QStringLiteral("natural_positions"))
        || exec(QStringLiteral("UPDATE players SET natural_positions_manual = 1 "
                               "WHERE COALESCE(natural_positions, '') <> ''"));
}

bool Database::migrateV11ToV12()
{
    // Non-EU flag and "cannot be registered" per list. A database that went
    // through v5 after this change already has the columns.
    QSet<QString> existing;
    QSqlQuery info(m_db);
    if (!info.exec(QStringLiteral("PRAGMA table_info(player_registration)"))) {
        m_error = info.lastError().text();
        return false;
    }
    while (info.next())
        existing.insert(info.value(1).toString());
    for (const char *column : {"non_eu", "league_blocked", "uefa_blocked"}) {
        const QString name = QString::fromLatin1(column);
        if (existing.contains(name))
            continue;
        if (!exec(QStringLiteral("ALTER TABLE player_registration ADD COLUMN %1 INTEGER NOT NULL "
                                 "DEFAULT 0")
                      .arg(name)))
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

QString Database::createPlayerRegistrationSql()
{
    // Hand-maintained squad-registration status (home-grown, club-trained, U21
    // override, saved squad lists). Only non-default players have a row.
    return QStringLiteral("CREATE TABLE IF NOT EXISTS player_registration ("
                          " player_id INTEGER PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,"
                          " home_grown INTEGER NOT NULL DEFAULT 0,"
                          " club_trained INTEGER NOT NULL DEFAULT 0,"
                          " u21 INTEGER NOT NULL DEFAULT -1,"
                          " league_listed INTEGER NOT NULL DEFAULT 0,"
                          " uefa_listed INTEGER NOT NULL DEFAULT 0,"
                          " non_eu INTEGER NOT NULL DEFAULT 0,"
                          " league_blocked INTEGER NOT NULL DEFAULT 0,"
                          " uefa_blocked INTEGER NOT NULL DEFAULT 0)");
}

QString Database::createPlayerRegistrationExtraSql()
{
    // Registration fields added in 2.2.3 (LaLiga youth override, minimum wage
    // does not apply). A table of their own: 2.2.2 and older rewrite whole
    // player_registration rows and would reset columns they do not know.
    return QStringLiteral("CREATE TABLE IF NOT EXISTS player_registration_extra ("
                          " player_id INTEGER PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,"
                          " youth INTEGER NOT NULL DEFAULT -1,"
                          " min_wage_exempt INTEGER NOT NULL DEFAULT 0)");
}

bool Database::migrateToV14()
{
    if (!exec(createPlayerRegistrationExtraSql()))
        return false;
    // 2.2.3 testbuilds (schema 13) kept these fields as columns of
    // player_registration: move them over. The columns stay, unused.
    QSet<QString> existing;
    QSqlQuery info(m_db);
    if (!info.exec(QStringLiteral("PRAGMA table_info(player_registration)"))) {
        m_error = info.lastError().text();
        return false;
    }
    while (info.next())
        existing.insert(info.value(1).toString());
    info.finish();
    if (!existing.contains(QStringLiteral("youth"))
        || !existing.contains(QStringLiteral("min_wage_exempt")))
        return true;
    return exec(QStringLiteral("INSERT OR REPLACE INTO player_registration_extra "
                               "(player_id, youth, min_wage_exempt) "
                               "SELECT player_id, youth, min_wage_exempt FROM player_registration "
                               "WHERE youth != -1 OR min_wage_exempt != 0"));
}

QStringList Database::createV15Sql()
{
    return {
        // A player deleted by any app version (2.2.2 merges duplicates by
        // deleting one) parks his 2.2.3 registration fields here first; 2.2.3
        // clears what its own deletions park and hands the rest to the
        // surviving duplicate on the next open (reconcileRegistrationStash).
        QStringLiteral("CREATE TABLE IF NOT EXISTS player_registration_stash ("
                       " player_id INTEGER PRIMARY KEY,"
                       " uid TEXT NOT NULL,"
                       " name TEXT NOT NULL,"
                       " youth INTEGER NOT NULL,"
                       " min_wage_exempt INTEGER NOT NULL)"),
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS stash_registration_extra"
                       " BEFORE DELETE ON players"
                       " WHEN EXISTS (SELECT 1 FROM player_registration_extra"
                       " WHERE player_id = OLD.id)"
                       " BEGIN"
                       " INSERT OR REPLACE INTO player_registration_stash"
                       " (player_id, uid, name, youth, min_wage_exempt)"
                       " SELECT OLD.id, OLD.uid, OLD.name, youth, min_wage_exempt"
                       " FROM player_registration_extra WHERE player_id = OLD.id;"
                       " END"),
        // Newest in-game date imported per player and month, also when his
        // data did not change (no snapshot row): an older export imported
        // later must not become that month's state.
        QStringLiteral("CREATE TABLE IF NOT EXISTS snapshot_seen ("
                       " player_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,"
                       " period TEXT NOT NULL,"
                       " game_date TEXT NOT NULL,"
                       " PRIMARY KEY (player_id, period)) WITHOUT ROWID"),
    };
}

bool Database::migrateToV15()
{
    for (const QString &sql : createV15Sql()) {
        if (!exec(sql))
            return false;
    }
    return true;
}

bool Database::reconcileRegistrationStash()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT player_id, uid, name, youth, min_wage_exempt "
                               "FROM player_registration_stash")))
        return true; // nothing parked (or an older schema): nothing to do
    struct Parked {
        int id;
        QString uid, name;
        int youth, minWageExempt;
    };
    std::vector<Parked> parked;
    while (q.next())
        parked.push_back({q.value(0).toInt(), q.value(1).toString(), q.value(2).toString(),
                          q.value(3).toInt(), q.value(4).toInt()});
    q.finish();
    if (parked.empty())
        return true;
    if (!beginTransaction())
        return false;
    for (const Parked &p : parked) {
        // The surviving duplicate by the known uid relation only: the same
        // uid (re-imported) or its newgen twin (12345 <-> r-12345). A name
        // alone is not enough: two players may share one.
        const QString bare = p.uid.startsWith(QLatin1String("r-")) ? p.uid.mid(2) : p.uid;
        QSqlQuery find(m_db);
        find.prepare(QStringLiteral(
            "SELECT id FROM players WHERE uid IN (?, ?, ?) AND id NOT IN "
            "(SELECT player_id FROM player_registration_extra)"));
        find.bindValue(0, p.uid);
        find.bindValue(1, bare);
        find.bindValue(2, QStringLiteral("r-") + bare);
        QList<int> matches;
        if (find.exec())
            while (find.next())
                matches << find.value(0).toInt();
        if (matches.size() == 1) {
            QSqlQuery put(m_db);
            put.prepare(QStringLiteral("INSERT OR IGNORE INTO player_registration_extra "
                                       "(player_id, youth, min_wage_exempt) VALUES (?, ?, ?)"));
            put.bindValue(0, matches.first());
            put.bindValue(1, p.youth);
            put.bindValue(2, p.minWageExempt);
            if (!put.exec()) {
                m_error = put.lastError().text();
                rollbackTransaction();
                return false;
            }
        }
    }
    if (!exec(QStringLiteral("DELETE FROM player_registration_stash"))) {
        rollbackTransaction();
        return false;
    }
    return commitTransaction();
}

bool Database::clearStash(const QList<int> &playerIds)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM player_registration_stash WHERE player_id = ?"));
    for (const int id : playerIds) {
        query.bindValue(0, id);
        if (!query.exec()) {
            m_error = query.lastError().text();
            return false;
        }
    }
    return true;
}

bool Database::migrateV4ToV5()
{
    return exec(createPlayerRegistrationSql());
}

bool Database::migrateV5ToV6()
{
    // The upload counter used to advance on every import, so a few small
    // exports (squad, shortlist, scouting) made almost the whole database look
    // stale. It now advances only on full exports (Freshness::isFullUpload).
    // Re-base the existing stamps onto that clock: an earlier upload counts as
    // full when it is still the last-seen stamp of a full-export share of all
    // players; each stamp becomes the number of such uploads at or before it.
    const int counter =
        setting(QStringLiteral("update_counter"), QStringLiteral("0")).toInt();
    if (counter <= 0)
        return true;

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM players")) || !query.next()) {
        m_error = query.lastError().text();
        return false;
    }
    const int total = query.value(0).toInt();
    if (!query.exec(QStringLiteral("SELECT last_seen_update, COUNT(*) FROM players "
                                   "WHERE last_seen_update > 0 "
                                   "GROUP BY last_seen_update ORDER BY last_seen_update"))) {
        m_error = query.lastError().text();
        return false;
    }
    QList<int> fullUploads; // ascending
    while (query.next()) {
        if (Freshness::isFullUpload(query.value(1).toInt(), total))
            fullUploads << query.value(0).toInt();
    }
    query.finish();

    // CASE WHEN last_seen_update >= newest THEN n ... WHEN >= oldest THEN 1 ELSE 0
    // (plain 0 when no full upload is left: the clock starts over).
    QString rebased = QStringLiteral("0");
    if (!fullUploads.isEmpty()) {
        rebased = QStringLiteral("CASE");
        for (qsizetype i = fullUploads.size() - 1; i >= 0; --i) {
            rebased += QStringLiteral(" WHEN last_seen_update >= %1 THEN %2")
                           .arg(fullUploads.at(i))
                           .arg(i + 1);
        }
        rebased += QStringLiteral(" ELSE 0 END");
    }
    if (!exec(QStringLiteral("UPDATE players SET last_seen_update = ") + rebased
              + QStringLiteral(" WHERE last_seen_update > 0")))
        return false;
    return setSetting(QStringLiteral("update_counter"), QString::number(fullUploads.size()));
}

bool Database::migrateV6ToV7()
{
    // Manual U21 decisions now expire when the player turns 22 (see
    // Registration::expireU21Override). Apply that to the ones set before: reset
    // overrides of players who are 22+ by now and queue them for the
    // registration page's one-time report.
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM player_registration WHERE u21 != -1"))
        || !query.next()) {
        m_error = query.lastError().text();
        return false;
    }
    if (query.value(0).toInt() == 0)
        return true; // no manual decision at all
    if (!query.exec(QStringLiteral("SELECT p.uid FROM player_registration r "
                                   "JOIN players p ON p.id = r.player_id "
                                   "WHERE r.u21 != -1 AND p.age >= 22"))) {
        m_error = query.lastError().text();
        return false;
    }
    QStringList uids;
    while (query.next())
        uids << query.value(0).toString();
    query.finish();
    if (uids.isEmpty())
        return true;
    return exec(QStringLiteral("UPDATE player_registration SET u21 = -1 WHERE u21 != -1 "
                               "AND player_id IN (SELECT id FROM players WHERE age >= 22)"))
           // Rows left at all defaults are not kept (see setRegistrations).
           && exec(QStringLiteral("DELETE FROM player_registration WHERE home_grown = 0 "
                                  "AND club_trained = 0 AND u21 = -1 AND league_listed = 0 "
                                  "AND uefa_listed = 0"))
           && setSetting(Registration::expiredU21SettingKey(), uids.join(QLatin1Char(',')));
}

QString Database::createLongTermInjuriesSql()
{
    // Players the user marked as out for a long time; the squad pages plan
    // without them.
    return QStringLiteral("CREATE TABLE IF NOT EXISTS long_term_injuries ("
                          " player_id INTEGER PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE)");
}

bool Database::migrateV7ToV8()
{
    return exec(createLongTermInjuriesSql());
}

QString Database::createDwrsRecalcsSql()
{
    // History timestamps written by a recalculation instead of an import: the
    // development view re-bases ratings there instead of counting a change.
    return QStringLiteral("CREATE TABLE IF NOT EXISTS dwrs_recalcs (ts TEXT PRIMARY KEY)");
}

bool Database::migrateV8ToV9()
{
    // Earlier recalculations left no trace; they cannot be told apart anymore.
    return exec(createDwrsRecalcsSql());
}

bool Database::migrateV9ToV10()
{
    // FM27 goalkeeper attributes; existing rows stay NULL (= not scouted).
    // Skips columns that already exist (a database rewound to v9 by hand).
    QSet<QString> existing;
    QSqlQuery info(m_db);
    if (!info.exec(QStringLiteral("PRAGMA table_info(players)"))) {
        m_error = info.lastError().text();
        return false;
    }
    while (info.next())
        existing.insert(info.value(1).toString());
    for (const QString &name : {QStringLiteral("Communication"), QStringLiteral("Eccentricity")}) {
        for (const QString &suffix : {QStringLiteral("_lo"), QStringLiteral("_hi")}) {
            const QString column = attrColumnName(name) + suffix;
            if (!existing.contains(column)
                && !exec(QStringLiteral("ALTER TABLE players ADD COLUMN %1 INTEGER").arg(column)))
                return false;
        }
    }
    return true;
}

bool Database::migrateToV17()
{
    QSet<QString> existing;
    QSqlQuery info(m_db);
    if (!info.exec(QStringLiteral("PRAGMA table_info(players)"))) {
        m_error = info.lastError().text();
        return false;
    }
    while (info.next()) existing.insert(info.value(1).toString());
    info.finish();
    for (const auto &name : {QStringLiteral("Natural Fitness"), QStringLiteral("Free Kick Taking"),
                             QStringLiteral("Long Throws"), QStringLiteral("Penalty Taking"),
                             QStringLiteral("Punching")}) {
        for (const auto &suffix : {QStringLiteral("_lo"), QStringLiteral("_hi")}) {
            const auto column = attrColumnName(name) + suffix;
            if (!existing.contains(column)
                && !exec(QStringLiteral("ALTER TABLE players ADD COLUMN %1 INTEGER").arg(column)))
                return false;
        }
    }
    return true;
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
    QSet<int> nationalIds, shortlistIdSet, injuredIdSet;
    QHash<int, QString> trainingRoleById;
    forRows(QStringLiteral("SELECT player_id FROM national_squad"),
            QStringLiteral("SELECT player_id FROM national_squad WHERE player_id IN (%1)"),
            [&](const QSqlQuery &q) { nationalIds.insert(q.value(0).toInt()); });
    forRows(QStringLiteral("SELECT player_id FROM shortlist"),
            QStringLiteral("SELECT player_id FROM shortlist WHERE player_id IN (%1)"),
            [&](const QSqlQuery &q) { shortlistIdSet.insert(q.value(0).toInt()); });
    forRows(QStringLiteral("SELECT player_id FROM long_term_injuries"),
            QStringLiteral("SELECT player_id FROM long_term_injuries WHERE player_id IN (%1)"),
            [&](const QSqlQuery &q) { injuredIdSet.insert(q.value(0).toInt()); });
    forRows(QStringLiteral("SELECT player_id, role FROM training_roles"),
            QStringLiteral("SELECT player_id, role FROM training_roles WHERE player_id IN (%1)"),
            [&](const QSqlQuery &q) {
                trainingRoleById.insert(q.value(0).toInt(), q.value(1).toString());
            });
    QHash<int, PlayerRegistration> registrationById;
    forRows(QStringLiteral("SELECT player_id, home_grown, club_trained, u21, league_listed, "
                           "uefa_listed, non_eu, league_blocked, uefa_blocked "
                           "FROM player_registration"),
            QStringLiteral("SELECT player_id, home_grown, club_trained, u21, league_listed, "
                           "uefa_listed, non_eu, league_blocked, uefa_blocked "
                           "FROM player_registration WHERE player_id IN (%1)"),
            [&](const QSqlQuery &q) {
                registrationById.insert(q.value(0).toInt(), registrationFromRow(q, 1));
            });
    forRows(QStringLiteral("SELECT player_id, youth, min_wage_exempt "
                           "FROM player_registration_extra"),
            QStringLiteral("SELECT player_id, youth, min_wage_exempt "
                           "FROM player_registration_extra WHERE player_id IN (%1)"),
            [&](const QSqlQuery &q) {
                registrationExtraFromRow(q, 1, registrationById[q.value(0).toInt()]);
            });

    // Column indexes, resolved once from the first result row's record.
    struct Columns {
        int id, uid, name, age, club, nat, nat2, pos, pers, media, apt, wage, tvRaw, tv, avr,
            heightRaw, height, lf, rf, pf, side, prim, natPos, saveNatPos, manualNatPos,
            lastSeen, ts, ls, newClub;
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
        c.saveNatPos = col("save_natural_positions");
        c.manualNatPos = col("natural_positions_manual");
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
            p.saveNaturalPositions = splitRolesFromDb(query.value(c.saveNatPos).toString());
            p.naturalPositionsManual = query.value(c.manualNatPos).toBool();
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
            p.longTermInjured = injuredIdSet.contains(p.id);
            p.trainingRole = trainingRoleById.value(p.id);
            p.registration = registrationById.value(p.id);
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
        QStringLiteral("natural_positions"), QStringLiteral("save_natural_positions"),
        QStringLiteral("natural_positions_manual"), QStringLiteral("last_seen_update"),
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
        query.bindValue(i++, joinRolesForDb(p.saveNaturalPositions));
        query.bindValue(i++, p.naturalPositionsManual ? 1 : 0);
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
    // Deleted on purpose here: nothing to hand over (see createV15Sql).
    if (!clearStash(playerIds)) {
        rollbackTransaction();
        return false;
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
    const auto players = loadPlayers({badPlayerId, goodPlayerId});
    if (players.size() != 2) {
        m_error = QStringLiteral("Both players must exist when merging position information.");
        rollbackTransaction();
        return false;
    }
    Player good = players[players[0].id == goodPlayerId ? 0 : 1];
    good.mergeNaturalPositionsFrom(players[players[0].id == badPlayerId ? 0 : 1]);
    QSqlQuery positionQuery(m_db);
    positionQuery.prepare(QStringLiteral("UPDATE players SET natural_positions = ?, "
                                        "save_natural_positions = ?, natural_positions_manual = ? "
                                        "WHERE id = ?"));
    positionQuery.addBindValue(joinRolesForDb(good.naturalPositions));
    positionQuery.addBindValue(joinRolesForDb(good.saveNaturalPositions));
    positionQuery.addBindValue(good.naturalPositionsManual ? 1 : 0);
    positionQuery.addBindValue(goodPlayerId);
    if (!positionQuery.exec()) {
        m_error = positionQuery.lastError().text();
        rollbackTransaction();
        return false;
    }
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
    // Snapshots follow the same way; per month the newer in-game state wins.
    query.prepare(QStringLiteral(
        "DELETE FROM player_snapshots WHERE player_id = ? AND EXISTS ("
        " SELECT 1 FROM player_snapshots b WHERE b.player_id = ?"
        " AND b.period = player_snapshots.period"
        " AND b.game_date > player_snapshots.game_date)"));
    query.bindValue(0, goodPlayerId);
    query.bindValue(1, badPlayerId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        rollbackTransaction();
        return false;
    }
    query.prepare(QStringLiteral(
        "UPDATE OR IGNORE player_snapshots SET player_id = ? WHERE player_id = ?"));
    query.bindValue(0, goodPlayerId);
    query.bindValue(1, badPlayerId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        rollbackTransaction();
        return false;
    }
    // The newest in-game date seen per month moves over too (the newer wins),
    // so an older export cannot slip into the merged player's history.
    query.prepare(QStringLiteral(
        "INSERT INTO snapshot_seen (player_id, period, game_date) "
        "SELECT ?, period, game_date FROM snapshot_seen WHERE player_id = ? "
        "ON CONFLICT(player_id, period) DO UPDATE SET "
        "game_date = MAX(game_date, excluded.game_date)"));
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

    // Registration flags move over only when the good player has none (the
    // importer's fill-empty merge does the same in memory).
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO player_registration "
        "(player_id, home_grown, club_trained, u21, league_listed, uefa_listed, non_eu, "
        "league_blocked, uefa_blocked) "
        "SELECT ?, home_grown, club_trained, u21, league_listed, uefa_listed, non_eu, "
        "league_blocked, uefa_blocked "
        "FROM player_registration WHERE player_id = ?"));
    query.bindValue(0, goodPlayerId);
    query.bindValue(1, badPlayerId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        rollbackTransaction();
        return false;
    }
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO player_registration_extra (player_id, youth, min_wage_exempt) "
        "SELECT ?, youth, min_wage_exempt FROM player_registration_extra WHERE player_id = ?"));
    query.bindValue(0, goodPlayerId);
    query.bindValue(1, badPlayerId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        rollbackTransaction();
        return false;
    }

    // The user's own decisions on the duplicate survive the merge instead of
    // vanishing with its row (CASCADE): national squad, shortlist and
    // long-term injury are kept if either record had them; a training role
    // moves over only when the surviving player has none (it wins, like the
    // fill-empty merge above). HtmlImporter's mergeAppManaged mirrors this in
    // memory.
    const QStringList carryOver = {
        QStringLiteral("INSERT OR IGNORE INTO national_squad (player_id) "
                       "SELECT ? FROM national_squad WHERE player_id = ?"),
        QStringLiteral("INSERT OR IGNORE INTO shortlist (player_id) "
                       "SELECT ? FROM shortlist WHERE player_id = ?"),
        QStringLiteral("INSERT OR IGNORE INTO long_term_injuries (player_id) "
                       "SELECT ? FROM long_term_injuries WHERE player_id = ?"),
        QStringLiteral("INSERT OR IGNORE INTO training_roles (player_id, role) "
                       "SELECT ?, role FROM training_roles WHERE player_id = ?"),
    };
    for (const QString &sql : carryOver) {
        query.prepare(sql);
        query.bindValue(0, goodPlayerId);
        query.bindValue(1, badPlayerId);
        if (!query.exec()) {
            m_error = query.lastError().text();
            rollbackTransaction();
            return false;
        }
    }

    query.prepare(QStringLiteral("DELETE FROM players WHERE id = ?"));
    query.bindValue(0, badPlayerId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        rollbackTransaction();
        return false;
    }
    if (!clearStash({badPlayerId})) {
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

bool Database::appendDwrsRatings(const std::vector<DwrsEntry> &entries, RatingsOrigin origin, bool recordHistory)
{
    if (entries.empty())
        return true;
    for (const auto &entry : entries) {
        if (!std::isfinite(entry.absolute) || !std::isfinite(entry.normalized)) {
            m_error = QStringLiteral("Invalid non-finite rating"); return false;
        }
        if (entry.basisKey.isEmpty()) continue;
        if (entry.profileId.isEmpty() || entry.profileRevision.isEmpty() || entry.calculationVersion.isEmpty()
            || entry.attributes.size() != 2 * kAttrCount) {
            m_error = QStringLiteral("Incomplete rating provenance"); return false;
        }
        for (int a = 0; a < kAttrCount; ++a) {
            const auto low = static_cast<unsigned char>(entry.attributes[a]);
            const auto high = static_cast<unsigned char>(entry.attributes[a + kAttrCount]);
            if (low > high || high > 20 || (low == 0 && high != 0)) {
                m_error = QStringLiteral("Invalid historical attribute range"); return false;
            }
        }
    }
    if (!beginTransaction())
        return false;

    if (recordHistory && origin == RatingsOrigin::Recalc) {
        QSet<QString> stamps;
        for (const DwrsEntry &entry : entries)
            stamps.insert(entry.timestamp);
        QSqlQuery mark(m_db);
        mark.prepare(QStringLiteral("INSERT OR IGNORE INTO dwrs_recalcs (ts) VALUES (?)"));
        for (const QString &stamp : stamps) {
            mark.bindValue(0, stamp);
            if (!mark.exec()) {
                m_error = mark.lastError().text();
                rollbackTransaction();
                return false;
            }
        }
    }

    // Multi-row statements: one exec per chunk instead of two per rating. The
    // per-exec overhead of QSqlQuery dominated a big recalc (~160k ratings took
    // >10 s); the SQL work itself is unchanged. 5 binds per row keeps a chunk
    // far below SQLite's bound-variable limit.
    constexpr int kChunkRows = 200;
    const auto rowsSql = [](int rows, int columns = 5) {
        QStringList marks;
        for (int c = 0; c < columns; ++c)
            marks << QStringLiteral("?");
        const QString row = QLatin1Char('(') + marks.join(QStringLiteral(", ")) + QLatin1Char(')');
        QStringList values;
        values.reserve(rows);
        for (int i = 0; i < rows; ++i)
            values << row;
        return values.join(QStringLiteral(", "));
    };
    // A repeated (player, role, timestamp) replaces the earlier row, as before;
    // its provenance row is replaced together with it below.
    const auto historySql = [&](int rows) {
        return QStringLiteral("INSERT OR REPLACE INTO dwrs_history ")
               + QStringLiteral(
                              "(player_id, role, absolute, normalized, ts) VALUES ")
               + rowsSql(rows);
    };
    // Keep the materialized latest table in sync: overwrite the (player, role)
    // row only when the entry is at least as new as the stored one, so an
    // out-of-order append can never make dwrs_latest go backwards. Rows of one
    // statement are applied in order, exactly like the former per-row upsert.
    const auto latestSql = [&](int rows) {
        return QStringLiteral("INSERT INTO dwrs_latest "
                              "(player_id, role, absolute, normalized, ts, basis_key) VALUES ")
               + rowsSql(rows, 6)
               + QStringLiteral(" ON CONFLICT(player_id, role) DO UPDATE SET "
                                "  absolute = excluded.absolute, "
                                "  normalized = excluded.normalized, ts = excluded.ts, "
                                "  basis_key = excluded.basis_key "
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
            if (statement == &history && !recordHistory) continue;
            int bind = 0;
            for (int r = start; r < start + rows; ++r) {
                const DwrsEntry &entry = entries[static_cast<size_t>(r)];
                statement->bindValue(bind++, entry.playerId);
                statement->bindValue(bind++, entry.role);
                statement->bindValue(bind++, entry.absolute);
                statement->bindValue(bind++, entry.normalized);
                statement->bindValue(bind++, entry.timestamp);
                if (statement == &latest)
                    statement->bindValue(bind++, entry.basisKey.isNull() ? QStringLiteral("") : entry.basisKey);
            }
            if (!statement->exec()) {
                m_error = statement->lastError().text();
                rollbackTransaction();
                return false;
            }
        }
    }
    // Provenance in multi-row chunks too: one statement per rating made a
    // recalc of a big save sit at 100 % for about a minute.
    if (recordHistory) {
        std::vector<const DwrsEntry *> annotated;
        for (const auto &entry : entries)
            if (!entry.basisKey.isEmpty())
                annotated.push_back(&entry);
        constexpr int kProvenanceRows = 100; // 9 binds per row
        const auto provenanceSql = [&](int rows) {
            return QStringLiteral("INSERT OR REPLACE INTO dwrs_provenance "
                                  "(player_id,role,ts,basis_key,profile_id,profile_revision,"
                                  "calculation_version,game_date,attributes) VALUES ")
                   + rowsSql(rows, 9);
        };
        QSqlQuery fullProvenance(m_db);
        fullProvenance.prepare(provenanceSql(kProvenanceRows));
        const int count = static_cast<int>(annotated.size());
        for (int start = 0; start < count; start += kProvenanceRows) {
            const int rows = std::min(kProvenanceRows, count - start);
            QSqlQuery partial(m_db);
            if (rows != kProvenanceRows)
                partial.prepare(provenanceSql(rows));
            QSqlQuery &provenance = rows == kProvenanceRows ? fullProvenance : partial;
            int bind = 0;
            for (int r = start; r < start + rows; ++r) {
                const DwrsEntry &entry = *annotated[static_cast<size_t>(r)];
                provenance.bindValue(bind++, entry.playerId);
                provenance.bindValue(bind++, entry.role);
                provenance.bindValue(bind++, entry.timestamp);
                provenance.bindValue(bind++, entry.basisKey);
                provenance.bindValue(bind++, entry.profileId);
                provenance.bindValue(bind++, entry.profileRevision);
                provenance.bindValue(bind++, entry.calculationVersion);
                provenance.bindValue(bind++, entry.gameDate.isValid()
                                                 ? QVariant(entry.gameDate.toString(Qt::ISODate)) : QVariant());
                provenance.bindValue(bind++, entry.attributes);
            }
            if (!provenance.exec()) {
                m_error = provenance.lastError().text();
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

bool Database::forEachLatestDwrsRating(
    const std::function<void(int, const QString &, double, double)> &visit)
{
    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral("SELECT player_id, role, absolute, normalized FROM dwrs_latest "
                                   "ORDER BY player_id, role"))) {
        m_error = query.lastError().text();
        return false;
    }
    while (query.next()) {
        visit(query.value(0).toInt(), query.value(1).toString(), query.value(2).toDouble(),
              query.value(3).toDouble());
    }
    if (query.lastError().isValid()) {
        m_error = query.lastError().text();
        return false;
    }
    return true;
}

std::vector<DwrsEntry> Database::dwrsHistory(const QList<int> &playerIds, const QString &role)
{
    std::vector<DwrsEntry> result;
    if (playerIds.isEmpty())
        return result;

    // Chunked IN (…) lists: an unbounded list could exceed SQLite's
    // bound-variable limit for very large id sets.
    const bool oneRole = !role.isEmpty() && role != QLatin1String("All Roles");
    QString sql = QStringLiteral("SELECT h.player_id, h.role, h.absolute, h.normalized, h.ts, "
                                 "p.basis_key,p.profile_id,p.profile_revision,p.calculation_version,p.game_date,p.attributes "
                                 "FROM dwrs_history h LEFT JOIN dwrs_provenance p "
                                 "ON p.player_id=h.player_id AND p.role=h.role AND p.ts=h.ts WHERE h.player_id IN (%1)");
    if (oneRole)
        sql += QStringLiteral(" AND h.role = ?");
    sql += QStringLiteral(" ORDER BY h.player_id, h.role, h.ts");

    forEachIdChunk(playerIds, sql, oneRole ? QVariantList{role} : QVariantList{},
                   [&result](const QSqlQuery &query) {
                       DwrsEntry entry;
                       entry.playerId = query.value(0).toInt();
                       entry.role = query.value(1).toString();
                       entry.absolute = query.value(2).toDouble();
                       entry.normalized = query.value(3).toDouble();
                       entry.timestamp = query.value(4).toString();
                       entry.basisKey = query.value(5).toString(); entry.profileId = query.value(6).toString();
                       entry.profileRevision = query.value(7).toString(); entry.calculationVersion = query.value(8).toString();
                       entry.gameDate = QDate::fromString(query.value(9).toString(), Qt::ISODate);
                       entry.attributes = query.value(10).toByteArray();
                       result.push_back(std::move(entry));
                   });
    return result;
}

QSet<QString> Database::dwrsRecalcTimestamps()
{
    QSet<QString> stamps;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT ts FROM dwrs_recalcs"))) {
        m_error = query.lastError().text();
        return stamps;
    }
    while (query.next())
        stamps.insert(query.value(0).toString());
    return stamps;
}

// --- Import log and player snapshots -------------------------------------------

namespace {

constexpr const char *kDateFormat = "yyyy-MM-dd";

QByteArray packAttrs(const ImportHistory::Snapshot &s)
{
    QByteArray bytes(2 * kAttrCount, '\0');
    for (int i = 0; i < kAttrCount; ++i) {
        bytes[i] = char(s.attrLo[size_t(i)]);
        bytes[kAttrCount + i] = char(s.attrHi[size_t(i)]);
    }
    return bytes;
}

// Snapshot from a row of kSnapshotColumns. A shorter blob (fewer attributes in an older version)
// leaves the rest at 0 = missing.
ImportHistory::Snapshot snapshotFromQuery(const QSqlQuery &q)
{
    ImportHistory::Snapshot s;
    s.playerId = q.value(0).toInt();
    s.gameDate = QDate::fromString(q.value(1).toString(), QLatin1String(kDateFormat));
    s.condensed = q.value(2).toInt() != 0;
    const QByteArray bytes = q.value(3).toByteArray();
    const int count = int(bytes.size() / 2);
    for (int i = 0; i < count && i < kAttrCount; ++i) {
        s.attrLo[size_t(i)] = uint8_t(bytes.at(i));
        s.attrHi[size_t(i)] = uint8_t(bytes.at(count + i));
    }
    s.averageRating = q.value(4).toDouble();
    s.transferValue = q.value(5).toDouble();
    s.wage = q.value(6).toString();
    s.transferValueText = q.value(7).toString();
    s.period = q.value(8).toString();
    return s;
}

const QString kSnapshotColumns = QStringLiteral(
    "player_id, game_date, condensed, attrs, average_rating, transfer_value, wage,"
    " transfer_value_text, period");

} // namespace

int Database::addImport(const ImportHistory::ImportRecord &record)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO imports (imported_at, game_date, source, format, players)"
                             " VALUES (?, ?, ?, ?, ?)"));
    q.bindValue(0, record.importedAt);
    q.bindValue(1, record.gameDate.isValid()
                       ? QVariant(record.gameDate.toString(QLatin1String(kDateFormat)))
                       : QVariant(QMetaType::fromType<QString>()));
    // Never NULL (a default QString binds as NULL).
    q.bindValue(2, record.source.isEmpty() ? QStringLiteral("") : record.source);
    q.bindValue(3, record.format.isEmpty() ? QStringLiteral("") : record.format);
    q.bindValue(4, record.players);
    if (!q.exec()) {
        m_error = q.lastError().text();
        return 0;
    }
    return q.lastInsertId().toInt();
}

std::vector<ImportHistory::ImportRecord> Database::imports()
{
    std::vector<ImportHistory::ImportRecord> out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id, imported_at, game_date, source, format, players"
                               " FROM imports ORDER BY id"))) {
        m_error = q.lastError().text();
        return out;
    }
    while (q.next()) {
        ImportHistory::ImportRecord r;
        r.id = q.value(0).toInt();
        r.importedAt = q.value(1).toString();
        r.gameDate = QDate::fromString(q.value(2).toString(), QLatin1String(kDateFormat));
        r.source = q.value(3).toString();
        r.format = q.value(4).toString();
        r.players = q.value(5).toInt();
        out.push_back(std::move(r));
    }
    return out;
}

QDate Database::lastGameDate()
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT game_date FROM imports WHERE game_date IS NOT NULL"
                              " ORDER BY id DESC LIMIT 1"))
        && q.next())
        return QDate::fromString(q.value(0).toString(), QLatin1String(kDateFormat));
    return {};
}

QHash<int, ImportHistory::Snapshot> Database::latestSnapshotsBefore(const QDate &before)
{
    QHash<int, ImportHistory::Snapshot> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM (SELECT *, ROW_NUMBER() OVER ("
                             "PARTITION BY player_id ORDER BY game_date DESC) AS n"
                             " FROM player_snapshots WHERE game_date < ?) WHERE n = 1")
                  .arg(kSnapshotColumns));
    q.bindValue(0, before.toString(QLatin1String(kDateFormat)));
    if (!q.exec()) {
        m_error = q.lastError().text();
        return out;
    }
    while (q.next()) {
        ImportHistory::Snapshot s = snapshotFromQuery(q);
        out.insert(s.playerId, std::move(s));
    }
    return out;
}

bool Database::upsertSnapshots(const std::vector<ImportHistory::Snapshot> &snapshots)
{
    if (snapshots.empty())
        return true;
    if (!beginTransaction())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO player_snapshots (%1)"
                             " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)")
                  .arg(kSnapshotColumns));
    for (const ImportHistory::Snapshot &s : snapshots) {
        q.bindValue(0, s.playerId);
        q.bindValue(1, s.gameDate.toString(QLatin1String(kDateFormat)));
        q.bindValue(2, s.condensed ? 1 : 0);
        q.bindValue(3, packAttrs(s));
        q.bindValue(4, s.averageRating > 0 ? QVariant(s.averageRating)
                                           : QVariant(QMetaType::fromType<double>()));
        q.bindValue(5, s.transferValue);
        q.bindValue(6, s.wage);
        q.bindValue(7, s.transferValueText);
        q.bindValue(8, s.period.isEmpty() ? ImportHistory::periodKey(s.gameDate, s.condensed)
                                          : s.period);
        if (!q.exec()) {
            m_error = q.lastError().text();
            rollbackTransaction();
            return false;
        }
    }
    return commitTransaction();
}

bool Database::deleteMonthSnapshots(const QList<int> &playerIds, const QDate &date)
{
    if (playerIds.isEmpty())
        return true;
    const QString period = ImportHistory::periodKey(date, false);
    return forEachIdChunk(playerIds,
                          QStringLiteral("DELETE FROM player_snapshots WHERE player_id IN (%1)"
                                         " AND period = ?"),
                          {period}, [](const QSqlQuery &) {});
}

QHash<int, QDate> Database::monthSnapshotDates(const QDate &date)
{
    // Newest of the month's snapshot row and the newest import that saw the
    // player (snapshot_seen; also when nothing changed).
    QHash<int, QDate> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT player_id, MAX(game_date) FROM ("
                             " SELECT player_id, game_date FROM player_snapshots"
                             "  WHERE condensed = 0 AND period = ?"
                             " UNION ALL"
                             " SELECT player_id, game_date FROM snapshot_seen WHERE period = ?)"
                             " GROUP BY player_id"));
    const QString period = ImportHistory::periodKey(date, false);
    q.bindValue(0, period);
    q.bindValue(1, period);
    if (!q.exec()) {
        m_error = q.lastError().text();
        return out;
    }
    while (q.next())
        out.insert(q.value(0).toInt(),
                   QDate::fromString(q.value(1).toString(), QLatin1String(kDateFormat)));
    return out;
}

bool Database::markMonthSeen(const QList<int> &playerIds, const QDate &date)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO snapshot_seen (player_id, period, game_date)"
                             " VALUES (?, ?, ?) ON CONFLICT(player_id, period)"
                             " DO UPDATE SET game_date = MAX(game_date, excluded.game_date)"));
    const QString period = ImportHistory::periodKey(date, false);
    const QString day = date.toString(QLatin1String(kDateFormat));
    for (const int id : playerIds) {
        q.bindValue(0, id);
        q.bindValue(1, period);
        q.bindValue(2, day);
        if (!q.exec()) {
            m_error = q.lastError().text();
            return false;
        }
    }
    return true;
}

bool Database::pruneMonthSeenBefore(const QDate &before)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM snapshot_seen WHERE period < ?"));
    q.bindValue(0, ImportHistory::periodKey(before, false));
    if (!q.exec()) {
        m_error = q.lastError().text();
        return false;
    }
    return true;
}

std::vector<ImportHistory::Snapshot> Database::monthlySnapshotsBefore(const QDate &before)
{
    std::vector<ImportHistory::Snapshot> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM player_snapshots WHERE condensed = 0"
                             " AND game_date < ? ORDER BY player_id, game_date")
                  .arg(kSnapshotColumns));
    q.bindValue(0, before.toString(QLatin1String(kDateFormat)));
    if (!q.exec()) {
        m_error = q.lastError().text();
        return out;
    }
    while (q.next())
        out.push_back(snapshotFromQuery(q));
    return out;
}

bool Database::deleteSnapshots(const std::vector<std::pair<int, QString>> &keys)
{
    if (keys.empty())
        return true;
    if (!beginTransaction())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM player_snapshots WHERE player_id = ? AND period = ?"));
    for (const auto &[playerId, period] : keys) {
        q.bindValue(0, playerId);
        q.bindValue(1, period);
        if (!q.exec()) {
            m_error = q.lastError().text();
            rollbackTransaction();
            return false;
        }
    }
    return commitTransaction();
}

std::vector<ImportHistory::Snapshot> Database::snapshots(int playerId)
{
    std::vector<ImportHistory::Snapshot> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM player_snapshots WHERE player_id = ?"
                             " ORDER BY game_date")
                  .arg(kSnapshotColumns));
    q.bindValue(0, playerId);
    if (!q.exec()) {
        m_error = q.lastError().text();
        return out;
    }
    while (q.next())
        out.push_back(snapshotFromQuery(q));
    return out;
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

bool Database::setLongTermInjured(int playerId, bool injured)
{
    QSqlQuery query(m_db);
    query.prepare(injured
                      ? QStringLiteral("INSERT OR IGNORE INTO long_term_injuries (player_id) VALUES (?)")
                      : QStringLiteral("DELETE FROM long_term_injuries WHERE player_id = ?"));
    query.bindValue(0, playerId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return false;
    }
    return true;
}

bool Database::setRegistrations(const std::vector<std::pair<int, PlayerRegistration>> &byId)
{
    if (byId.empty())
        return true;
    if (!beginTransaction())
        return false;
    QSqlQuery remove(m_db);
    remove.prepare(QStringLiteral("DELETE FROM player_registration WHERE player_id = ?"));
    QSqlQuery upsert(m_db);
    upsert.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO player_registration "
        "(player_id, home_grown, club_trained, u21, league_listed, uefa_listed, non_eu, "
        "league_blocked, uefa_blocked) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    QSqlQuery removeExtra(m_db);
    removeExtra.prepare(
        QStringLiteral("DELETE FROM player_registration_extra WHERE player_id = ?"));
    QSqlQuery upsertExtra(m_db);
    upsertExtra.prepare(QStringLiteral("INSERT OR REPLACE INTO player_registration_extra "
                                       "(player_id, youth, min_wage_exempt) VALUES (?, ?, ?)"));
    for (const auto &[playerId, r] : byId) {
        // Only non-default players have a row, in each table.
        const PlayerRegistration base = withoutExtra(r);
        QSqlQuery &query = base.isDefault() ? remove : upsert;
        query.bindValue(0, playerId);
        if (!base.isDefault()) {
            query.bindValue(1, r.homeGrown ? 1 : 0);
            query.bindValue(2, r.clubTrained ? 1 : 0);
            query.bindValue(3, static_cast<int>(r.u21));
            query.bindValue(4, r.leagueListed ? 1 : 0);
            query.bindValue(5, r.uefaListed ? 1 : 0);
            query.bindValue(6, r.nonEu ? 1 : 0);
            query.bindValue(7, r.leagueBlocked ? 1 : 0);
            query.bindValue(8, r.uefaBlocked ? 1 : 0);
        }
        const bool extraDefault = r.youth == PlayerRegistration::U21::Auto && !r.minWageExempt;
        QSqlQuery &extra = extraDefault ? removeExtra : upsertExtra;
        extra.bindValue(0, playerId);
        if (!extraDefault) {
            extra.bindValue(1, static_cast<int>(r.youth));
            extra.bindValue(2, r.minWageExempt ? 1 : 0);
        }
        if (!query.exec() || !extra.exec()) {
            m_error = (query.lastError().isValid() ? query : extra).lastError().text();
            rollbackTransaction();
            return false;
        }
    }
    return commitTransaction();
}

bool Database::createBackup(const QString &dbFilePath, const QString &backupsDir, QString *errorOut)
{
    if (!QFile::exists(dbFilePath))
        return true; // nothing to back up

    QDir().mkpath(backupsDir);
    const QDir dir(backupsDir);
    const QString baseName = QFileInfo(dbFilePath).completeBaseName();
    const QString timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    // Two backups within one second (e.g. delete right after an import) get a
    // counter instead of failing on the existing file.
    QString backupName = QStringLiteral("%1_backup_%2.db").arg(baseName, timestamp);
    for (int n = 2; dir.exists(backupName); ++n)
        backupName = QStringLiteral("%1_backup_%2_%3.db").arg(baseName, timestamp).arg(n);
    const QString backupPath = dir.filePath(backupName);

    // VACUUM INTO writes a transactionally consistent snapshot that already
    // folds in any committed WAL frames. A plain file copy would miss data
    // still living in the -wal sidecar (the DB runs in WAL mode), so a
    // pre-import backup could silently lose the latest changes.
    QString error;
    if (!snapshotTo(dbFilePath, backupPath, &error)) {
        if (errorOut)
            *errorOut = QStringLiteral("Backup (VACUUM INTO) failed: %1").arg(error);
        return false;
    }

    // Rotation: keep the 3 newest backups of exactly this save. The name must
    // match <save>_backup_<timestamp>[_<n>].db in full: a plain prefix glob
    // would also catch the backups of a save called "<save>_backup_x" and
    // could rotate away the one just written.
    const QRegularExpression ownBackup(
        QStringLiteral("^%1_backup_(\\d{4}-\\d{2}-\\d{2}_\\d{2}-\\d{2}-\\d{2})(?:_(\\d+))?\\.db$")
            .arg(QRegularExpression::escape(baseName)));
    struct Entry {
        QString file;
        QString stamp;
        int counter = 1;
    };
    QList<Entry> backups;
    for (const QString &file : dir.entryList({QStringLiteral("*.db")}, QDir::Files)) {
        const QRegularExpressionMatch match = ownBackup.match(file);
        if (match.hasMatch())
            backups.append({file, match.captured(1),
                            match.captured(2).isEmpty() ? 1 : match.captured(2).toInt()});
    }
    std::sort(backups.begin(), backups.end(), [](const Entry &a, const Entry &b) {
        return a.stamp != b.stamp ? a.stamp < b.stamp : a.counter < b.counter;
    });
    while (backups.size() > 3) {
        const Entry oldest = backups.takeFirst();
        if (oldest.file != backupName) // never the one just written
            QFile::remove(dir.filePath(oldest.file));
    }
    if (!QFile::exists(backupPath)) {
        if (errorOut)
            *errorOut = QStringLiteral("Backup %1 is missing after rotation.").arg(backupPath);
        return false;
    }
    return true;
}

bool Database::snapshotTo(const QString &sourcePath, const QString &targetPath, QString *errorOut)
{
    // Read-only: the source may be another installation's file; nothing of it
    // is changed. SQLite still reads committed frames from its -wal sidecar.
    if (QFileInfo::exists(targetPath)) {
        // Never touch an existing file (VACUUM INTO would refuse anyway, and
        // the cleanup below must only remove what this call wrote).
        if (errorOut)
            *errorOut = QStringLiteral("Target already exists: %1").arg(targetPath);
        return false;
    }
    bool ok = false;
    QString error;
    const QString connName =
        QStringLiteral("snapshot_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    {
        QSqlDatabase src = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        src.setDatabaseName(sourcePath);
        src.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2000"));
        if (!src.open()) {
            error = src.lastError().text();
        } else {
            QSqlQuery query(src);
            query.prepare(QStringLiteral("VACUUM INTO ?"));
            query.bindValue(0, targetPath);
            ok = query.exec();
            if (!ok)
                error = query.lastError().text();
            query.finish();
            src.close();
        }
    }
    QSqlDatabase::removeDatabase(connName);
    if (!ok) {
        // No half-written file under the target name.
        if (QFile::exists(targetPath))
            QFile::remove(targetPath);
        if (errorOut)
            *errorOut = error;
    }
    return ok;
}

} // namespace fm
