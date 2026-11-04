#pragma once

#include "AppPaths.h"
#include "core/AppConfig.h"
#include "core/Constants.h"
#include "core/Database.h"
#include "core/Definitions.h"
#include "core/DwrsEngine.h"
#include "core/PlayerStatus.h"
#include "core/PlayerStore.h"
#include "core/Registration.h"
#include "core/ScoutSearch.h"
#include "core/SquadBuilder.h"
#include "core/TacticExplorer.h"

#include <QFuture>
#include <QList>
#include <QObject>

#include <memory>

namespace fm {

// Owns all application state: paths, config, definitions, the open database,
// the in-memory player store, the analysis engines and the ratings caches.
// Pages read from here and connect to dataChanged().
class AppContext : public QObject
{
    Q_OBJECT

public:
    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;

    // Call after the data dir is settled (first-run dialog done). Loads
    // config/definitions and, unless openActiveDatabase is false, opens the
    // active database. Without it, nothing database-backed may be used until
    // openDatabase() succeeded (the start screen picks the save first).
    bool initialize(QString *errorOut = nullptr, bool openActiveDatabase = true);

    // True once a database is open.
    bool hasDatabase() const { return m_database != nullptr; }

    AppPaths &paths() { return m_paths; }
    AppConfig &config() { return *m_config; }
    Definitions &definitions() { return *m_definitions; }
    Database &database() { return *m_database; }
    PlayerStore &store() { return m_store; }
    DwrsEngine &dwrsEngine() { return *m_dwrsEngine; }
    SquadBuilder &squadBuilder() { return *m_squadBuilder; }
    TacticExplorer &tacticExplorer() { return *m_tacticExplorer; }

    // uid-keyed latest normalized ratings (master_role_ratings equivalent),
    // limited to each player's currently assigned roles.
    const RoleRatings &ratings() const { return m_ratings; }
    // id-keyed latest (absolute, normalized) — raw, may still contain roles the
    // player no longer has; look it up via assignedRoles.
    const LatestRatings &latestRatings() const { return m_latestRatings; }

    QString currentDbName() const;
    QStringList availableDatabases() const;

    // Switches/creates the active database and reloads everything.
    bool openDatabase(const QString &dbName, QString *errorOut = nullptr);

    // Creates a new, empty save for the given FM version and opens it. Fails
    // if a save with that name already exists. The new save's profile starts
    // from the defaults, or as a copy of the profile of save copyProfileFrom
    // (weights, colors, options, roles/tactics, logo; not club or players).
    bool createDatabase(const QString &dbName, const QString &fmVersionId,
                        QString *errorOut = nullptr);
    bool createDatabase(const QString &dbName, const QString &fmVersionId,
                        const QString &copyProfileFrom, QString *errorOut);

    // --- Profile of the open save (see desktop/PROFILE.md) ---
    // Weights, options and colors: config() (attached to the open save).
    // Roles/tactics: definitions(); edits are stored with saveDefinitions().
    // Club/national settings, favorites, bridges: database().setting().
    // Logo and flag: profileImage()/setProfileImage() (PNG bytes; empty = none).
    enum class ProfileImage { ClubLogo, NationalFlag };
    QByteArray profileImage(ProfileImage image);
    bool setProfileImage(ProfileImage image, const QByteArray &png, QString *errorOut = nullptr);
    // The image the club header shows (club crest, or the flag in national
    // mode). Single place to plug in further sources (e.g. the FM graphics
    // folder): today it is the PNG stored in the profile.
    QByteArray headerCrest();
    // Writes the edited definitions (definitions().setRoot()) into the open
    // save's profile and reloads them with the engines.
    bool saveDefinitions(QString *errorOut = nullptr);

    // Re-reads players + ratings from the database into memory.
    void reloadFromDatabase();

    // Re-reads only the DWRS ratings (players untouched). Cheaper than a full
    // reload after a recalc, and keeps existing Player* references valid.
    void reloadRatings();

    // Targeted refresh after a save that touched a handful of players: re-reads
    // just those rows and patches them into the store IN PLACE (rows and Player*
    // stay valid) plus their rating-cache entries — instead of reloading ~35k
    // players and ~500k ratings on the UI thread. Falls back to a full reload
    // when an id is unknown to the store or gone from the database.
    void refreshPlayers(const QList<int> &playerIds);

    // Same for ratings only (after a partial DWRS recalc of these players).
    void refreshRatings(const QList<int> &playerIds);

    // Replaces the national squad in the database and refreshes exactly the
    // players whose membership can have changed (old + new members).
    // false (DB error, errorString via database()) leaves everything unchanged.
    bool setNationalSquad(const QList<int> &playerIds);

    // Swaps in a complete state that already mirrors the database — the import
    // pipeline builds it on its worker thread (store in loadPlayers() order plus
    // both rating caches) — so the UI thread does no loading at all.
    void adoptState(PlayerStore store, LatestRatings latestRatings, RoleRatings ratings);

    // Re-reads config-dependent engine state (after settings changes).
    void reloadEngines();

    // Lets the settings page announce changed DB settings (club identity,
    // favorites, …) so header and pages refresh without a full reload.
    void notifySettingsChanged() { emit dataChanged(); }

    // Re-reads config.ini and the open save's profile (settings, roles and
    // tactics) and refreshes the engines. Returns false if the definitions
    // failed to reload; the previously loaded definitions are kept intact in
    // that case (errorString via definitions()).
    bool reloadConfigAndDefinitions();

    // Player the profile page should show on next activation (sidebar search,
    // "open profile" actions). Consumed by the Player Profile page (M9).
    QString pendingProfileUid() const { return m_pendingProfileUid; }
    void setPendingProfileUid(const QString &uid) { m_pendingProfileUid = uid; }

    // Same one-shot handoff for the edit page and the comparison page
    // (player context menus).
    QString takePendingEditUid()
    {
        const QString uid = m_pendingEditUid;
        m_pendingEditUid.clear();
        return uid;
    }
    void setPendingEditUid(const QString &uid) { m_pendingEditUid = uid; }
    QStringList takePendingComparisonUids()
    {
        const QStringList uids = m_pendingComparisonUids;
        m_pendingComparisonUids.clear();
        return uids;
    }
    void addPendingComparisonUid(const QString &uid)
    {
        if (!m_pendingComparisonUids.contains(uid))
            m_pendingComparisonUids.append(uid);
    }
    // Scouting search handed over from the Best XI (bridge search by
    // right-click); the scouting page takes it on its next refresh.
    void setPendingScoutSearch(const ScoutSearchRequest &request)
    {
        m_pendingScoutSearch = request;
        m_hasPendingScoutSearch = true;
    }
    bool takePendingScoutSearch(ScoutSearchRequest *request)
    {
        if (!m_hasPendingScoutSearch)
            return false;
        *request = m_pendingScoutSearch;
        m_pendingScoutSearch = ScoutSearchRequest();
        m_hasPendingScoutSearch = false;
        return true;
    }

    // Background work (HTML import, DWRS recalcs) reads the engines and
    // definitions owned here from a worker thread. Every such job registers its
    // future, so nothing that invalidates that state — an engine/definitions
    // reload, a database switch, shutdown — can run underneath a live worker.
    void registerBackgroundTask(const QFuture<void> &future);
    bool hasRunningBackgroundTask() const;
    // Blocks until every registered job has finished (no-op when idle).
    void waitForBackgroundTasks();

    // Pages/menus ask the main window to switch pages through this.
    void requestNavigation(const QString &pageId) { emit navigationRequested(pageId); }

    // Sidebar management mode (club vs. national). Session state like the
    // legacy 'management_mode'; pages shared between both modes scope their
    // player pool with this.
    bool nationalUiMode() const { return m_nationalUiMode; }
    void setNationalUiMode(bool national) { m_nationalUiMode = national; }

    // National team settings (same DB keys as legacy).
    QString nationalTeamName() { return m_database->setting(QStringLiteral("national_team_name")); }
    QString nationalTeamCode()
    {
        return m_database->setting(QStringLiteral("national_team_country_code"));
    }
    int nationalTeamAgeLimit()
    {
        const QString value =
            m_database->setting(QStringLiteral("national_team_age_limit"));
        return value.isEmpty() ? 0 : value.toInt();
    }

    // Per-database upload counter (incremented once per full HTML import).
    // Drives data-freshness together with each player's lastSeenUpdate.
    int updateCounter()
    {
        return m_database->setting(QStringLiteral("update_counter"), QStringLiteral("0")).toInt();
    }

    // Convenience settings accessors (stored in the DB settings table).
    QString userClub() { return m_database->setting(QStringLiteral("user_club")); }
    QString secondTeamClub() { return m_database->setting(QStringLiteral("second_team_club")); }
    // FM release of this database: selects export layout, roles/tactics and
    // attribute weights. Chosen when the save is created.
    QString fmVersionId()
    {
        return m_database->setting(QStringLiteral("fm_version"), defaultFmVersionId());
    }
    // Switches the active database to another FM version and loads its
    // profile. Only allowed while the database holds no players; false (with
    // a message) otherwise or on failure.
    bool setFmVersion(const QString &versionId, QString *errorOut = nullptr);

    // Start folder for the FM export file dialogs.
    QString htmlExportDir() { return m_config->effectiveHtmlExportDir(fmVersionId()); }
    bool nationalModeEnabled()
    {
        return m_database->setting(QStringLiteral("national_mode_enabled")) == QLatin1String("true");
    }

    // Squad-registration rules chosen for this database, limited to what the
    // active FM version models (an unsupported choice reads as None).
    Registration::Settings registrationSettings();

    // Persists one player's registration status (normalized) and patches the
    // store in place. false (DB error, errorString via database()) changes
    // nothing.
    bool setPlayerRegistration(const QString &uid, PlayerRegistration registration);

    // Marks a player as out long-term (or fit again) and patches the store in
    // place. false (DB error, errorString via database()) changes nothing.
    bool setLongTermInjured(const QString &uid, bool injured);

    // Current freshness thresholds + upload counter + user club, for
    // PlayerStatus::isRetired. Build once per pool, not per player.
    PlayerStatus::FreshnessContext freshnessContext();
    // National code, age limit and freshness, for PlayerStatus::isAvailableForNation.
    PlayerStatus::NationalCriteria nationalCriteria();

signals:
    // Player data or ratings changed — pages must refresh.
    void dataChanged();
    // The open save's profile was (re)loaded: colors, day/night mode, logo,
    // weights. Emitted before the players of a newly opened save load.
    void profileChanged();
    // A different database file was opened.
    void databaseChanged(const QString &dbName);
    // Switch to the given page (handled by MainWindow).
    void navigationRequested(const QString &pageId);

private:
    // Loads the roles/tactics of db's profile for this version (seeding them
    // on first use, applying newer shipped data) and rebuilds the engines.
    bool activateFmVersion(Database &db, const QString &versionId, QString *errorOut);
    bool openDatabaseImpl(const QString &dbName, const QString &copyProfileFrom,
                          QString *errorOut);
    bool copyProfile(Database &target, const QString &sourceName, QString *errorOut);
    void migrateProfileImages(Database &db);

    // Rebuilds m_latestRatings/m_ratings from dwrs_latest against the store.
    void rebuildRatingsCache();

    AppPaths m_paths;
    std::unique_ptr<AppConfig> m_config;
    std::unique_ptr<Definitions> m_definitions;
    std::unique_ptr<Database> m_database;
    PlayerStore m_store;
    std::unique_ptr<DwrsEngine> m_dwrsEngine;
    std::unique_ptr<SquadBuilder> m_squadBuilder;
    std::unique_ptr<TacticExplorer> m_tacticExplorer;
    RoleRatings m_ratings;
    LatestRatings m_latestRatings;
    QString m_pendingProfileUid;
    QString m_pendingEditUid;
    QStringList m_pendingComparisonUids;
    ScoutSearchRequest m_pendingScoutSearch;
    bool m_hasPendingScoutSearch = false;
    bool m_nationalUiMode = false;
    QList<QFuture<void>> m_backgroundTasks;
};

} // namespace fm
