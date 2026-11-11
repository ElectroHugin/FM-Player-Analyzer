#include "ImportPipeline.h"

#include "Database.h"
#include "Definitions.h"
#include "DwrsEngine.h"
#include "RatingSnapshot.h"
#include "RoleAssignment.h"

#include <QSet>

namespace fm {

namespace ImportPipeline {

namespace {
using Recalc = std::function<RatingsUpdater::Result(Database &, const std::vector<Player> &,
                                                    const std::vector<int> &,
                                                    std::function<void(int, int)>, const QDate &)>;

ImportPipelineResult runWith(const Request &request, const Definitions &definitions,
                             const Recalc &recalc, const StageCallback &stage)
{
    const auto report = [&stage](Stage s, int done, int total) {
        if (stage)
            stage(s, done, total);
    };
    ImportPipelineResult result;

    if (!request.backupsDir.isEmpty()) {
        report(Stage::Backup, 0, 0);
        QString backupError;
        if (!Database::createBackup(request.dbFile, request.backupsDir, &backupError))
            result.backupError = backupError;
    }

    Database db(Database::uniqueConnectionName(QStringLiteral("import_worker")));
    if (!db.open(request.dbFile)) {
        result.import.error = db.errorString();
        return result;
    }

    report(Stage::Parse, 0, 0);
    std::vector<Player> players = db.loadPlayers();
    // `players` is brought up to date in place on success — no second full
    // load after the import.
    result.import = HtmlImporter::importFile(
        request.filePath, db, players,
        [&report](int done, int total) { report(Stage::Import, done, total); },
        request.fmVersionId,
        [&report](int done, int total) { report(Stage::Parse, done, total); }, &players);
    if (!result.import.success)
        return result;

    if (request.autoAssign) {
        report(Stage::AutoAssign, 0, 0);
        result.autoAssignedUids = RoleAssignment::autoAssignMissingRoles(
            db, players, definitions, &result.autoAssignError);
    }

    // DWRS only for the players this import (or auto-assign) touched.
    QSet<QString> affected(result.import.affectedUids.cbegin(),
                           result.import.affectedUids.cend());
    for (const QString &uid : std::as_const(result.autoAssignedUids))
        affected.insert(uid);
    std::vector<int> subset;
    subset.reserve(affected.size());
    for (size_t i = 0; i < players.size(); ++i) {
        if (affected.contains(players[i].uid))
            subset.push_back(static_cast<int>(i));
    }

    report(Stage::Dwrs, 0, 0);
    result.recalcRan = true;
    const QDate gameDate = result.import.gameDate.isValid() ? result.import.gameDate : db.lastGameDate();
    result.recalc = recalc(db, players, subset,
                           [&report](int current, int total) { report(Stage::Dwrs, current, total); },
                           gameDate);

    // Build the UI's post-import state here, off the UI thread (reading ~500k
    // latest ratings and mapping them took ~1.1 s on the UI thread).
    report(Stage::Finalize, 0, 0);
    result.store.reset(std::move(players));
    if (!RatingsUpdater::loadRatingCaches(db, result.store, result.latestRatings, result.ratings))
        qWarning("Reading the latest DWRS ratings failed: %s", qUtf8Printable(db.errorString()));
    return result;
}
} // namespace

ImportPipelineResult run(const Request &request, const Definitions &definitions,
                         const DwrsEngine &engine, const StageCallback &stage)
{
    return runWith(request, definitions,
                   [&](Database &db, const std::vector<Player> &players, const std::vector<int> &subset,
                       std::function<void(int, int)> progress, const QDate &) {
                       return RatingsUpdater::updateDwrsRatings(db, players, engine, definitions.validRoles(),
                                                                subset, std::move(progress));
                   }, stage);
}

ImportPipelineResult run(const Request &request, const Definitions &definitions,
                         const RatingSnapshot &snapshot, const StageCallback &stage)
{
    QStringList unrated;
    ImportPipelineResult result = runWith(request, definitions,
                   [&](Database &db, const std::vector<Player> &players, const std::vector<int> &subset,
                       std::function<void(int, int)> progress, const QDate &gameDate) {
                       for (const int index : snapshot.ratedAttributes()) {
                           bool seen = subset.empty();
                           for (const int row : subset)
                               if (players[row].attrHi[index] > 0) { seen = true; break; }
                           if (!seen) unrated << attrNames()[index];
                       }
                       return RatingsUpdater::updateDwrsRatings(db, players, snapshot, definitions.validRoles(),
                                                                subset, std::move(progress),
                                                                RatingsOrigin::Import, gameDate);
                   }, stage);
    result.unratedColumns = unrated;
    return result;
}

} // namespace ImportPipeline

} // namespace fm
