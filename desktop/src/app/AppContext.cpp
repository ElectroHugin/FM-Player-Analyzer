#include "AppContext.h"

#include "core/ProfileStore.h"
#include "core/RatingsUpdater.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace fm {

AppContext::AppContext(QObject *parent)
    : QObject(parent)
    , m_graphics(std::make_unique<GraphicsLibrary>())
{
    // Connected first, so the copy is current before pages redraw.
    connect(m_graphics.get(), &GraphicsLibrary::stateChanged, this,
            &AppContext::refreshCachedCrest);
    connect(this, &AppContext::dataChanged, this, &AppContext::refreshCachedCrest);
    connect(this, &AppContext::databaseChanged, this, &AppContext::refreshCachedCrest);
}

AppContext::~AppContext()
{
    // Last line of defense: a worker must never outlive the engines and
    // definitions it reads (destroyed right after this body).
    waitForBackgroundTasks();
}

void AppContext::registerBackgroundTask(const QFuture<void> &future)
{
    m_backgroundTasks.removeIf([](const QFuture<void> &f) { return f.isFinished(); });
    m_backgroundTasks.append(future);
}

bool AppContext::hasRunningBackgroundTask() const
{
    for (const QFuture<void> &future : m_backgroundTasks) {
        if (!future.isFinished())
            return true;
    }
    return false;
}

void AppContext::waitForBackgroundTasks()
{
    // Workers never wait on the UI thread (progress is posted, not blocking),
    // so waiting here cannot deadlock.
    for (QFuture<void> &future : m_backgroundTasks)
        future.waitForFinished();
    m_backgroundTasks.clear();
}

bool AppContext::initialize(QString *errorOut, bool openActiveDatabase)
{
    if (!m_paths.ensureDataDirInitialized()) {
        if (errorOut)
            *errorOut = tr("Datenordner %1 konnte nicht angelegt werden.").arg(m_paths.dataDir());
        return false;
    }

    m_config = std::make_unique<AppConfig>(m_paths.configFile());

    // Stays empty until openDatabase() loads the profile of the database's
    // FM version.
    m_definitions = std::make_unique<Definitions>();

    m_dwrsEngine = std::make_unique<DwrsEngine>(*m_definitions, *m_config);
    m_squadBuilder = std::make_unique<SquadBuilder>(*m_definitions, *m_config);
    m_tacticExplorer = std::make_unique<TacticExplorer>(*m_definitions, *m_squadBuilder);

    if (!openActiveDatabase)
        return true;
    return openDatabase(m_config->dbName(), errorOut);
}

QString AppContext::currentDbName() const
{
    return m_config->dbName();
}

QStringList AppContext::availableDatabases() const
{
    QDir dir(m_paths.databasesDir());
    QStringList names;
    const QStringList files = dir.entryList({QStringLiteral("*.db")}, QDir::Files, QDir::Name);
    for (const QString &file : files)
        names.append(file.chopped(3)); // strip ".db"
    return names;
}

bool AppContext::openDatabase(const QString &dbName, QString *errorOut)
{
    return openDatabaseImpl(dbName, QString(), errorOut);
}

bool AppContext::openDatabaseImpl(const QString &dbName, const QString &copyProfileFrom,
                                  QString *errorOut)
{
    // Reopening the already-active database is a no-op: it would otherwise clash
    // on the (name-derived) connection string with the live connection and cost
    // a needless full reload.
    if (m_database && m_database->isOpen() && dbName == m_config->dbName())
        return true;

    // A worker may still hold the current file open (import, recalc): let it
    // finish before the database it writes to is swapped out.
    waitForBackgroundTasks();

    // A unique name keeps tearing down the previous Database from removing the
    // connection the new one just opened.
    const QString path = m_paths.databaseFile(dbName);
    const bool isNew = !QFile::exists(path);
    auto database = std::make_unique<Database>(
        Database::uniqueConnectionName(QStringLiteral("main_%1").arg(dbName)));
    if (!database->open(path)) {
        if (errorOut)
            *errorOut = database->errorString();
        return false;
    }
    // New saves are marked explicitly; unmarked ones predate the setting and
    // are FM24 (defaultFmVersionId).
    if (isNew) {
        database->setSetting(QStringLiteral("fm_version"), newestFmVersionId());
        // Every rating this save will ever hold uses the current formula (the
        // new-save wizard may import before the main window first checks).
        database->setSetting(QStringLiteral("dwrs_scheme"), QString::number(kDwrsSchemeVersion));
    }

    // The save's profile (weights, colors, options, roles and tactics) is
    // loaded before anything else. A save without one was last used by 2.0.x
    // and takes over the settings all saves shared until then; a new save
    // starts from the defaults (or a copy of another save's profile). If
    // anything fails, the previous save stays active with its own profile.
    if (!m_config->attachProfile(*database,
                                 isNew ? AppConfig::ProfileSeed::Defaults
                                       : AppConfig::ProfileSeed::SharedConfig,
                                 errorOut)) {
        return false;
    }
    const auto restorePrevious = [this] {
        if (m_database)
            m_config->attachProfile(*m_database, AppConfig::ProfileSeed::SharedConfig);
        else
            m_config->detachProfile();
    };
    if (isNew && !copyProfileFrom.isEmpty() && !copyProfile(*database, copyProfileFrom, errorOut)) {
        restorePrevious();
        database->close();
        QFile::remove(path);
        return false;
    }
    migrateProfileImages(*database);

    if (!activateFmVersion(*database,
                           database->setting(QStringLiteral("fm_version"), defaultFmVersionId()),
                           errorOut)) {
        restorePrevious();
        return false;
    }
    m_database = std::move(database);
    m_config->setDbName(dbName);
    m_config->touchDatabaseLastOpened(dbName);
    // Colors and header first (they are ready in milliseconds), then the
    // players, which take most of the time on large saves.
    emit profileChanged();
    applyGraphicsSettings();

    reloadFromDatabase();
    emit databaseChanged(dbName);
    return true;
}

bool AppContext::createDatabase(const QString &dbName, const QString &fmVersionId,
                                QString *errorOut)
{
    return createDatabase(dbName, fmVersionId, QString(), errorOut);
}

bool AppContext::createDatabase(const QString &dbName, const QString &fmVersionId,
                                const QString &copyProfileFrom, QString *errorOut)
{
    if (QFile::exists(m_paths.databaseFile(dbName))) {
        if (errorOut)
            *errorOut = tr("Ein Spielstand mit dem Namen „%1“ existiert bereits.").arg(dbName);
        return false;
    }
    return openDatabaseImpl(dbName, copyProfileFrom, errorOut)
           && setFmVersion(fmVersionId, errorOut);
}

bool AppContext::copyProfile(Database &target, const QString &sourceName, QString *errorOut)
{
    const auto fail = [errorOut](const QString &error) {
        if (errorOut)
            *errorOut = error;
        return false;
    };
    // Read the source through its own connection and without writing to it.
    // A source last used with 2.0.x has no profile yet: it used the shared
    // settings, which are then what gets copied.
    Database source(Database::uniqueConnectionName(QStringLiteral("profilecopy")));
    if (!source.open(m_paths.databaseFile(sourceName)))
        return fail(source.errorString());
    ProfileStore from(source);
    const bool sourceHasProfile = from.isInitialized();
    QHash<QString, QString> values;
    if (sourceHasProfile) {
        if (!from.loadSettings(values))
            return fail(from.errorString());
    } else {
        values = m_config->sharedProfileValues();
    }
    // Only reusable settings move over. The FM27 live source (save file, on/off,
    // last import time) belongs to the source's career: copied, the new save
    // would import the old career's data on its next autosave.
    for (auto it = values.begin(); it != values.end();) {
        if (it.key().startsWith(QLatin1String("Import/live_save_")))
            it = values.erase(it);
        else
            ++it;
    }
    if (!m_config->replaceProfileValues(values, QStringLiteral("copy:") + sourceName, errorOut))
        return false;

    // Roles/tactics of every FM version the source has. Club logo and national
    // flag are the club's identity: the new save brings its own.
    ProfileStore to(target);
    ScopedTransaction tx(target);
    for (const FmDataVersion &version : fmDataVersions()) {
        QByteArray json;
        if (sourceHasProfile) {
            from.readDocument(ProfileStore::definitionsDocument(version.id), json);
        } else {
            QFile file(m_paths.definitionsFile(version.id));
            if (file.open(QIODevice::ReadOnly))
                json = file.readAll();
        }
        if (!json.isEmpty() && !to.saveDefinitions(version.id, json))
            return fail(to.errorString());
    }
    if (!tx.commit())
        return fail(target.errorString());
    return true;
}

void AppContext::migrateProfileImages(Database &db)
{
    // Up to 2.0.4 logo and flag were PNG files in the assets folder, named
    // after the save, so a duplicated save pointed at — and could delete — the
    // original's file. From 2.1.0 on they live inside the save's profile.
    ProfileStore store(db);
    const QList<QPair<QString, QString>> images = {
        {QStringLiteral("club_logo_path"), ProfileStore::clubLogoDocument()},
        {QStringLiteral("national_flag_path"), ProfileStore::nationalFlagDocument()},
    };
    for (const auto &[settingKey, document] : images) {
        const QString path = db.setting(settingKey);
        if (path.isEmpty() || store.hasDocument(document))
            continue;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            store.writeDocument(document, file.readAll(), false);
    }
}

QByteArray AppContext::profileImage(ProfileImage image)
{
    QByteArray data;
    if (m_database)
        ProfileStore(*m_database).readDocument(image == ProfileImage::ClubLogo
                                                   ? ProfileStore::clubLogoDocument()
                                                   : ProfileStore::nationalFlagDocument(),
                                               data);
    return data;
}

QByteArray AppContext::headerCrest()
{
    if (nationalUiMode())
        return profileImage(ProfileImage::NationalFlag);
    return ownClubCrestData();
}

QByteArray AppContext::ownClubCrestData()
{
    const QByteArray own = profileImage(ProfileImage::ClubLogo);
    if (!own.isEmpty() || !m_database)
        return own;
    const quint32 id = clubFmId(userClub());
    // Graphics folder (off by default: then this stays empty as before).
    const QByteArray live = m_graphics->clubLogoFile(id);
    if (!live.isEmpty())
        return live;
    // The copy kept in the save, as long as it belongs to the own club.
    if (id == 0 || m_database->setting(QStringLiteral("graphics_crest_id")).toUInt() != id)
        return {};
    QByteArray cached;
    ProfileStore(*m_database).readDocument(ProfileStore::graphicsCrestDocument(), cached);
    return cached;
}

void AppContext::refreshCachedCrest()
{
    if (!m_database)
        return;
    const quint32 id = clubFmId(userClub());
    const quint32 cachedId = m_database->setting(QStringLiteral("graphics_crest_id")).toUInt();
    ProfileStore store(*m_database);
    const QByteArray live = m_graphics->clubLogoFile(id);
    if (!live.isEmpty()) {
        QByteArray cached;
        store.readDocument(ProfileStore::graphicsCrestDocument(), cached);
        if (cachedId != id || cached != live) { // new club or a new logo file
            store.writeDocument(ProfileStore::graphicsCrestDocument(), live, false);
            m_database->setSetting(QStringLiteral("graphics_crest_id"), QString::number(id));
        }
        return;
    }
    // No crest in the folder right now (folder off, missing or still
    // loading): keep the copy, unless it belongs to a club we no longer are.
    if (cachedId != 0 && cachedId != id && id != 0) {
        store.removeDocument(ProfileStore::graphicsCrestDocument());
        m_database->removeSetting(QStringLiteral("graphics_crest_id"));
    }
}

bool AppContext::setProfileImage(ProfileImage image, const QByteArray &png, QString *errorOut)
{
    const QString document = image == ProfileImage::ClubLogo ? ProfileStore::clubLogoDocument()
                                                             : ProfileStore::nationalFlagDocument();
    const QString settingKey = image == ProfileImage::ClubLogo
                                   ? QStringLiteral("club_logo_path")
                                   : QStringLiteral("national_flag_path");
    ProfileStore store(*m_database);
    ScopedTransaction tx(*m_database);
    // The old file reference goes, so it is never migrated in again.
    const bool ok = (png.isEmpty() ? store.removeDocument(document)
                                   : store.writeDocument(document, png, false))
                    && m_database->removeSetting(settingKey) && tx.commit();
    if (!ok && errorOut)
        *errorOut = store.errorString().isEmpty() ? m_database->errorString()
                                                  : store.errorString();
    return ok;
}

bool AppContext::activateFmVersion(Database &db, const QString &versionId, QString *errorOut)
{
    const FmDataVersion &version = fmDataVersion(versionId);
    QByteArray shipped;
    {
        QFile bundled(QStringLiteral(":/defaults/") + version.definitionsFileName);
        if (bundled.open(QIODevice::ReadOnly))
            shipped = bundled.readAll();
    }
    // Saves from 2.0.x keep the roles and tactics they shared until now.
    QByteArray shared;
    if (m_config->profileSeed() == QLatin1String("config.ini")) {
        QFile file(m_paths.definitionsFile(versionId));
        if (file.open(QIODevice::ReadOnly))
            shared = file.readAll();
    }
    ProfileStore store(db);
    ProfileStore::DefinitionsResult result;
    if (!store.loadDefinitions(versionId, shipped, shared, result)) {
        if (errorOut)
            *errorOut = tr("Rollen und Taktiken für %1 konnten nicht geladen werden (%2).")
                            .arg(version.displayName, store.errorString());
        return false;
    }
    if (!result.beforeUpdate.isEmpty()) {
        // Newer shipped data replaced roles (user tactics kept): keep the
        // previous state next to the backups.
        QDir().mkpath(m_paths.backupsDir());
        QFile backup(QDir(m_paths.backupsDir())
                         .filePath(QStringLiteral("%1_definitions_%2_vor_update_%3.json")
                                       .arg(QFileInfo(db.filePath()).completeBaseName(), versionId,
                                            QDateTime::currentDateTime().toString(
                                                QStringLiteral("yyyyMMdd_HHmmss")))));
        if (backup.open(QIODevice::WriteOnly))
            backup.write(result.beforeUpdate);
    }
    if (!m_definitions->loadFromJson(result.json, versionId,
                                     QStringLiteral("%1 (%2)").arg(
                                         QDir::toNativeSeparators(db.filePath()),
                                         ProfileStore::definitionsDocument(versionId)))) {
        if (errorOut)
            *errorOut = m_definitions->errorString();
        return false;
    }
    // Edits (New Role / New Tactic pages) go back into this save's profile.
    m_definitions->setSaveHandler([this, versionId](const QByteArray &json, QString *error) {
        if (!m_database) {
            if (error)
                *error = tr("Kein Spielstand geöffnet.");
            return false;
        }
        ProfileStore target(*m_database);
        if (!target.saveDefinitions(versionId, json)) {
            if (error)
                *error = target.errorString();
            return false;
        }
        return true;
    });
    reloadEngines();
    return true;
}

bool AppContext::setFmVersion(const QString &versionId, QString *errorOut)
{
    if (versionId == fmVersionId())
        return true;
    // Role assignments, ratings and history refer to the old version's roles.
    if (m_store.size() > 0) {
        if (errorOut)
            *errorOut = tr("Die Football-Manager-Version ist an den Spielstand gebunden, "
                           "sobald er Spieler enthält. Für eine andere Version bitte einen "
                           "neuen Spielstand anlegen.");
        return false;
    }
    if (!activateFmVersion(*m_database, versionId, errorOut))
        return false;
    if (!m_database->setSetting(QStringLiteral("fm_version"), versionId)) {
        if (errorOut)
            *errorOut = m_database->errorString();
        return false;
    }
    emit dataChanged();
    return true;
}

void AppContext::reloadFromDatabase()
{
    m_store.reset(m_database->loadPlayers());
    rebuildRatingsCache();
    emit dataChanged();
}

void AppContext::refreshPlayers(const QList<int> &playerIds)
{
    if (playerIds.isEmpty())
        return;
    std::vector<Player> fresh = m_database->loadPlayers(playerIds);
    const QSet<int> wanted(playerIds.cbegin(), playerIds.cend());
    bool complete = static_cast<int>(fresh.size()) == wanted.size();
    for (const Player &player : fresh)
        complete = complete && m_store.rowById(player.id) >= 0;
    if (!complete) {
        reloadFromDatabase(); // rows appeared/disappeared: rebuild everything
        return;
    }
    for (Player &player : fresh) {
        const int row = m_store.rowById(player.id);
        const QString previousUid = m_store.at(row).uid;
        m_store.replace(row, std::move(player));
        RatingsUpdater::patchRoleRatings(m_ratings, m_store.at(row), previousUid,
                                         m_latestRatings);
    }
    emit dataChanged();
}

void AppContext::refreshRatings(const QList<int> &playerIds)
{
    if (playerIds.isEmpty())
        return;
    // dwrs_latest rows are only ever added or overwritten (merges aside, which
    // go through a full import), so merging the fresh rows in is exact.
    const LatestRatings fresh = m_database->latestDwrsRatings(playerIds);
    for (auto it = fresh.constBegin(); it != fresh.constEnd(); ++it)
        m_latestRatings.insert(it.key(), it.value());
    for (const int id : playerIds) {
        const int row = m_store.rowById(id);
        if (row >= 0) {
            const Player &player = m_store.at(row);
            RatingsUpdater::patchRoleRatings(m_ratings, player, player.uid, m_latestRatings);
        }
    }
    emit dataChanged();
}

bool AppContext::setNationalSquad(const QList<int> &playerIds)
{
    QList<int> touched = playerIds;
    for (const Player &player : m_store.players()) {
        if (player.inNationalSquad)
            touched << player.id;
    }
    if (!m_database->setNationalSquadIds(playerIds))
        return false;
    refreshPlayers(touched);
    return true;
}

void AppContext::adoptState(PlayerStore store, LatestRatings latestRatings, RoleRatings ratings)
{
    m_store = std::move(store);
    m_latestRatings = std::move(latestRatings);
    m_ratings = std::move(ratings);
    emit dataChanged();
}

void AppContext::reloadRatings()
{
    // Only the ratings changed (e.g. after a DWRS recalc) — the player rows are
    // untouched, so skip reloading 35k players and just rebuild the caches.
    rebuildRatingsCache();
    emit dataChanged();
}

void AppContext::rebuildRatingsCache()
{
    if (!RatingsUpdater::loadRatingCaches(*m_database, m_store, m_latestRatings, m_ratings))
        qWarning("Reading the latest DWRS ratings failed: %s",
                 qUtf8Printable(m_database->errorString()));
}

PlayerStatus::FreshnessContext AppContext::freshnessContext()
{
    PlayerStatus::FreshnessContext freshness;
    freshness.currentCounter = updateCounter();
    freshness.retirementAge = m_config->freshnessSetting(QStringLiteral("retirement_age"));
    freshness.staleAfterUploads = m_config->freshnessSetting(QStringLiteral("stale_after_full_uploads"));
    freshness.userClub = userClub();
    return freshness;
}

PlayerStatus::NationalCriteria AppContext::nationalCriteria()
{
    PlayerStatus::NationalCriteria criteria;
    criteria.countryCode = nationalTeamCode();
    criteria.ageLimit = nationalTeamAgeLimit();
    criteria.freshness = freshnessContext();
    return criteria;
}

Registration::Settings AppContext::registrationSettings()
{
    const QString version = fmVersionId();
    Registration::Settings settings;
    const Registration::LeagueRules league = Registration::leagueRulesFromKey(
        m_database->setting(QStringLiteral("registration_league")));
    if (Registration::leagueRulesFor(version).contains(league))
        settings.league = league;
    settings.uefa = Registration::uefaRulesFor(version)
                    && m_database->setting(QStringLiteral("registration_uefa"))
                           == QLatin1String("true");
    settings.minGoalkeepers =
        m_database->setting(QStringLiteral("registration_min_goalkeepers"), QStringLiteral("2"))
            .toInt();
    // Wage rules are entered on the registration page in the unit chosen there.
    const double weeks = Registration::weeksPer(Registration::wagePeriodFromKey(
        m_database->setting(QStringLiteral("registration_wage_period"))));
    settings.wageBudget =
        m_database->setting(QStringLiteral("registration_wage_budget")).toDouble() / weeks;
    settings.minWage =
        m_database->setting(QStringLiteral("registration_min_wage")).toDouble() / weeks;
    // Youth cut-offs depend on the in-game date: the newest one known.
    settings.gameDate = m_database->lastGameDate();
    return settings;
}

bool AppContext::setPlayerRegistration(const QString &uid, PlayerRegistration registration)
{
    const int row = m_store.rowByUid(uid);
    if (row < 0)
        return false;
    Registration::normalize(registration);
    Player &player = m_store.at(row);
    if (!m_database->setRegistrations({{player.id, registration}}))
        return false;
    player.registration = registration;
    emit dataChanged();
    return true;
}

bool AppContext::setLongTermInjured(const QString &uid, bool injured)
{
    const int row = m_store.rowByUid(uid);
    if (row < 0)
        return false;
    Player &player = m_store.at(row);
    if (!m_database->setLongTermInjured(player.id, injured))
        return false;
    player.longTermInjured = injured;
    emit dataChanged();
    return true;
}

void AppContext::reloadEngines()
{
    // reloadConfig() rebuilds the plan cache a worker may be reading.
    waitForBackgroundTasks();
    m_dwrsEngine->reloadConfig();
    m_squadBuilder->reloadConfig();
}

bool AppContext::reloadConfigAndDefinitions()
{
    waitForBackgroundTasks(); // workers read the definitions being replaced
    m_config->reload();
    bool definitionsOk = true;
    if (m_database) {
        // The open save's profile: re-read its values and roles/tactics. A
        // failed load leaves the current definitions untouched, so a bad
        // document does not wipe the running configuration.
        QString error;
        definitionsOk = m_config->attachProfile(*m_database, AppConfig::ProfileSeed::SharedConfig,
                                                &error)
                        && activateFmVersion(*m_database, fmVersionId(), &error);
        emit profileChanged();
        applyGraphicsSettings();
    }
    reloadEngines();
    emit dataChanged();
    return definitionsOk;
}

bool AppContext::saveDefinitions(QString *errorOut)
{
    if (!m_definitions->save()) {
        if (errorOut)
            *errorOut = m_definitions->errorString();
        return false;
    }
    return reloadConfigAndDefinitions();
}

void AppContext::applyGraphicsSettings(bool forceRescan)
{
    if (!m_database || !m_config->graphicsEnabled()) {
        m_graphics->configure(false, {}, {});
        return;
    }
    const QString folder = QDir(m_config->effectiveGraphicsDir(fmVersionId())).absolutePath();
    // One cache per folder, outside the graphics folder (never written to).
    const QByteArray hash = QCryptographicHash::hash(QDir::cleanPath(folder).toLower().toUtf8(),
                                                     QCryptographicHash::Sha1)
                                .toHex()
                                .left(12);
    const QString cacheFile = m_paths.dataDir() + QStringLiteral("/cache/graphics_")
                              + QString::fromLatin1(hash) + QStringLiteral(".bin");
    m_graphics->configure(true, folder, cacheFile, forceRescan);
}

quint32 AppContext::clubFmId(const QString &clubName)
{
    if (!m_database || clubName.isEmpty())
        return 0;
    if (clubName == userClub()) {
        const quint32 own = GraphicsIndex::parseId(
            m_database->setting(QStringLiteral("club_fm_id")));
        if (own != 0)
            return own;
    }
    const QJsonObject map =
        QJsonDocument::fromJson(m_database->setting(QStringLiteral("club_fm_ids")).toUtf8())
            .object();
    return GraphicsIndex::parseId(map.value(clubName).toVariant().toString());
}

bool AppContext::setClubFmId(const QString &clubName, quint32 id)
{
    if (!m_database || clubName.isEmpty())
        return false;
    if (clubName == userClub())
        return m_database->setSetting(QStringLiteral("club_fm_id"),
                                      id ? QString::number(id) : QString());
    QJsonObject map =
        QJsonDocument::fromJson(m_database->setting(QStringLiteral("club_fm_ids")).toUtf8())
            .object();
    if (id)
        map.insert(clubName, QString::number(id));
    else
        map.remove(clubName);
    return m_database->setSetting(
        QStringLiteral("club_fm_ids"),
        QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact)));
}

QPixmap AppContext::clubCrest(const QString &clubName, int size)
{
    if (!m_database || clubName.isEmpty() || size <= 0)
        return {};
    if (clubName == userClub()) {
        QPixmap own;
        if (own.loadFromData(profileImage(ProfileImage::ClubLogo)))
            return own.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return m_graphics->clubLogo(clubFmId(clubName), size);
}

} // namespace fm
