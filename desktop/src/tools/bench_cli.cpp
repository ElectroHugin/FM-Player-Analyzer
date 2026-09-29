// Performance benchmark for the hot data paths.
//
//   fmbench <db>
//       Load path: times exactly what AppContext::reloadFromDatabase does on app
//       start / after imports (players + latest ratings + uid mapping).
//
//   fmbench <db> --import <export.html> <definitions.json>
//       Import path: runs core ImportPipeline (the app's worker code: import,
//       auto-assign, DWRS recalc for the affected players) plus the UI-side
//       adoption, phase by phase. Works on a plain file copy of <db> in a temp
//       dir; SQLite never opens the given file.

#include "core/AppConfig.h"
#include "core/Database.h"
#include "core/Definitions.h"
#include "core/DwrsEngine.h"
#include "core/ImportPipeline.h"
#include "core/PlayerStore.h"
#include "core/RatingsUpdater.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int benchLoad(const QString &dbPath)
{
    QElapsedTimer timer;
    timer.start();
    fm::Database db(QStringLiteral("bench"));
    if (!db.open(dbPath)) {
        std::fprintf(stderr, "FEHLER: %s\n", qUtf8Printable(db.errorString()));
        return 1;
    }
    const qint64 openMs = timer.restart();

    fm::PlayerStore store;
    store.reset(db.loadPlayers());
    const qint64 playersMs = timer.restart();

    const fm::LatestRatings latest = db.latestDwrsRatings();
    const qint64 ratingsMs = timer.restart();

    const fm::RoleRatings roleRatings = fm::RatingsUpdater::roleRatingsForAssigned(store, latest);
    const qint64 mappingMs = timer.restart();

    std::printf("Spieler geladen:   %d in %lld ms\n", store.size(), playersMs);
    std::printf("Ratings geladen:   %lld in %lld ms\n",
                static_cast<long long>(latest.size()), ratingsMs);
    std::printf("Rollen-Mapping:    %lld Rollen in %lld ms\n",
                static_cast<long long>(roleRatings.size()), mappingMs);
    std::printf("Gesamt (inkl. Open %lld ms): %lld ms\n", openMs,
                openMs + playersMs + ratingsMs + mappingMs);
    return 0;
}

int benchImport(const QString &sourceDb, const QString &htmlPath, const QString &definitionsPath)
{
    // Plain file copy (db + WAL sidecar) into a temp dir: the source file is
    // never opened by SQLite, so not even a WAL checkpoint touches it.
    QTemporaryDir tmp;
    const QString dbPath = tmp.filePath(QStringLiteral("bench.db"));
    if (!QFile::copy(sourceDb, dbPath)) {
        std::fprintf(stderr, "FEHLER: Kopie von %s fehlgeschlagen\n", qUtf8Printable(sourceDb));
        return 1;
    }
    if (QFile::exists(sourceDb + QStringLiteral("-wal")))
        QFile::copy(sourceDb + QStringLiteral("-wal"), dbPath + QStringLiteral("-wal"));

    fm::Definitions definitions;
    if (!definitions.load(definitionsPath)) {
        std::fprintf(stderr, "FEHLER: %s\n", qUtf8Printable(definitions.errorString()));
        return 1;
    }
    fm::AppConfig config(tmp.filePath(QStringLiteral("config.ini")));
    fm::DwrsEngine engine(definitions, config);

    // Exactly the pipeline the app runs on its worker thread; stage changes
    // are timestamped to get per-phase durations.
    fm::ImportPipeline::Request request;
    request.filePath = htmlPath;
    request.dbFile = dbPath;
    request.autoAssign = true; // backupsDir empty: no backup (not part of the measurement)

    const char *names[] = {"Backup", "Laden + Parsen", "Import (DB)", "Auto-Rollen", "DWRS"};
    qint64 stageMs[5] = {0, 0, 0, 0, 0};
    int current = -1;
    QElapsedTimer timer;
    timer.start();
    QElapsedTimer total;
    total.start();
    const auto stage = [&](fm::ImportPipeline::Stage s, int, int) {
        const int index = static_cast<int>(s);
        if (index == current)
            return;
        if (current >= 0)
            stageMs[current] += timer.restart();
        else
            timer.restart();
        current = index;
    };
    fm::ImportPipelineResult result = fm::ImportPipeline::run(request, definitions, engine, stage);
    if (current >= 0)
        stageMs[current] += timer.restart();
    const qint64 workerMs = total.elapsed();
    if (!result.import.success) {
        std::fprintf(stderr, "FEHLER (Import): %s\n", qUtf8Printable(result.import.error));
        return 1;
    }

    // UI thread afterwards: AppContext::adoptPlayers (store + ratings cache).
    timer.restart();
    fm::Database db(QStringLiteral("bench_ui"));
    if (!db.open(dbPath)) {
        std::fprintf(stderr, "FEHLER: %s\n", qUtf8Printable(db.errorString()));
        return 1;
    }
    fm::PlayerStore store;
    store.reset(std::move(result.players));
    const fm::RoleRatings ratings =
        fm::RatingsUpdater::roleRatingsForAssigned(store, db.latestDwrsRatings());
    const qint64 uiMs = timer.restart();

    std::printf("Import von %d Zeilen -> DB mit %d Spielern (%d neu)\n",
                result.import.rowsParsed, store.size(), result.import.newPlayers);
    for (int i = 1; i < 5; ++i)
        std::printf("  [Worker] %-15s %6lld ms\n", names[i], stageMs[i]);
    std::printf("  = Worker gesamt:         %6lld ms  (Auto-Rollen: %d, DWRS: %d berechnet, "
                "%d geschrieben)\n",
                workerMs, static_cast<int>(result.autoAssignedUids.size()),
                result.recalc.computed, result.recalc.inserted);
    std::printf("  [UI-Thread] Uebernahme:  %6lld ms  (%d Rollen)\n", uiMs,
                static_cast<int>(ratings.size()));
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() == 2)
        return benchLoad(args[1]);
    if (args.size() == 5 && args[2] == QLatin1String("--import"))
        return benchImport(args[1], args[3], args[4]);
    std::fprintf(stderr, "Usage: fmbench <db>\n"
                         "       fmbench <db> --import <export.html> <definitions.json>\n");
    return 2;
}
