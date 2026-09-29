#include "AppContext.h"

#include "core/RatingsUpdater.h"

#include <QDir>

namespace fm {

AppContext::AppContext(QObject *parent)
    : QObject(parent)
{
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

bool AppContext::initialize(QString *errorOut)
{
    if (!m_paths.ensureDataDirInitialized()) {
        if (errorOut)
            *errorOut = tr("Datenordner %1 konnte nicht angelegt werden.").arg(m_paths.dataDir());
        return false;
    }

    m_config = std::make_unique<AppConfig>(m_paths.configFile());

    m_definitions = std::make_unique<Definitions>();
    if (!m_definitions->load(m_paths.definitionsFile())) {
        if (errorOut)
            *errorOut = m_definitions->errorString();
        return false;
    }

    m_dwrsEngine = std::make_unique<DwrsEngine>(*m_definitions, *m_config);
    m_squadBuilder = std::make_unique<SquadBuilder>(*m_definitions, *m_config);
    m_tacticExplorer = std::make_unique<TacticExplorer>(*m_definitions, *m_squadBuilder);

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
    auto database = std::make_unique<Database>(
        Database::uniqueConnectionName(QStringLiteral("main_%1").arg(dbName)));
    if (!database->open(m_paths.databaseFile(dbName))) {
        if (errorOut)
            *errorOut = database->errorString();
        return false;
    }
    m_database = std::move(database);
    m_config->setDbName(dbName);

    reloadFromDatabase();
    emit databaseChanged(dbName);
    return true;
}

void AppContext::reloadFromDatabase()
{
    m_store.reset(m_database->loadPlayers());
    rebuildRatingsCache();
    emit dataChanged();
}

void AppContext::adoptPlayers(std::vector<Player> players)
{
    m_store.reset(std::move(players));
    rebuildRatingsCache();
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
    m_latestRatings = m_database->latestDwrsRatings();
    m_ratings = RatingsUpdater::roleRatingsForAssigned(m_store, m_latestRatings);
}

PlayerStatus::FreshnessContext AppContext::freshnessContext()
{
    PlayerStatus::FreshnessContext freshness;
    freshness.currentCounter = updateCounter();
    freshness.retirementAge = m_config->freshnessSetting(QStringLiteral("retirement_age"));
    freshness.staleAfterUploads = m_config->freshnessSetting(QStringLiteral("stale_after_uploads"));
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
    // load() leaves the current definitions untouched if it fails, so a bad
    // file after a migration/edit does not wipe the running configuration.
    const bool definitionsOk = m_definitions->load(m_paths.definitionsFile());
    reloadEngines();
    emit dataChanged();
    return definitionsOk;
}

} // namespace fm
