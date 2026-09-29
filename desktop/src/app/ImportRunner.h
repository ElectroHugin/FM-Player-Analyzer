#pragma once

#include "core/ImportPipeline.h"

#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

namespace fm {

class AppContext;

// Runs the full HTML-import pipeline (core ImportPipeline: backup, parse+import,
// optional auto-assign, DWRS recalc for affected players) in a background thread
// with a modal progress dialog. On success the context adopts the pipeline's
// final player list (no full reload on the UI thread), then onDone(result) runs
// on the UI thread (result.players has been moved out by then).
// Shared by the club dashboard and the national dashboard.
void runImportPipeline(AppContext &context, QWidget *parent, const QString &filePath,
                       bool autoAssign, std::function<void(ImportPipelineResult)> onDone);

// Builds the localized summary + warning lists for a finished pipeline run.
QStringList importSummaryLines(const ImportPipelineResult &result);
QStringList importWarningLines(const ImportPipelineResult &result);

} // namespace fm
