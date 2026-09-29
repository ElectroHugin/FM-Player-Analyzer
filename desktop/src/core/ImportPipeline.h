#pragma once

#include "HtmlImporter.h"
#include "Player.h"
#include "RatingsUpdater.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace fm {

class Definitions;
class DwrsEngine;

// Everything the background import pipeline produces (import + optional
// auto-assign + DWRS recalculation for the affected players).
struct ImportPipelineResult {
    ImportResult import;
    QString backupError;     // non-fatal
    QStringList autoAssignedUids;
    QString autoAssignError; // non-fatal
    bool recalcRan = false;
    RatingsUpdater::Result recalc;
    // Complete player list after import and auto-assign — same content and
    // order as Database::loadPlayers(). Valid when import.success; lets the UI
    // adopt the result instead of reloading every player on its own thread.
    std::vector<Player> players;
};

// The HTML-import pipeline as run on the worker thread: backup, parse+import,
// optional additive auto-assign, DWRS recalc for exactly the touched players.
// UI-free, so the app and the benchmark tool run the very same code.
namespace ImportPipeline {

struct Request {
    QString filePath;
    QString dbFile;
    QString backupsDir; // empty = skip the pre-import backup
    bool autoAssign = true;
    QString fmVersionId;
};

enum class Stage { Backup, Parse, Import, AutoAssign, Dwrs };

// done/total: bytes (Parse), rows (Import), role batches (Dwrs); 0/0 marks the
// start of a stage. Called on the worker thread.
using StageCallback = std::function<void(Stage stage, int done, int total)>;

// Opens its own connection to request.dbFile (thread-safe to call from a
// worker). definitions/engine are only read.
ImportPipelineResult run(const Request &request, const Definitions &definitions,
                         const DwrsEngine &engine, const StageCallback &stage = {});

} // namespace ImportPipeline

} // namespace fm
