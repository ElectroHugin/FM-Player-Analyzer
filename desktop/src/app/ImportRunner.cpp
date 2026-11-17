#include "ImportRunner.h"

#include "core/RatingsUpdater.h"
#include "core/Fm27SaveImport.h"

#include "AppContext.h"
#include "widgets/BusyProgressDialog.h"

#include <QCoreApplication>
#include <QLocale>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrentRun>

#include <memory>

namespace fm {

namespace {

// Translation context "ImportRunner" for this free-function module. A plain
// helper taking a const char* hid every string from lupdate, so they were
// never translated.
struct ImportText {
    Q_DECLARE_TR_FUNCTIONS(ImportRunner)
};

} // namespace

void runImportPipeline(AppContext &context, QWidget *parent, const QString &filePath,
                       bool autoAssign, std::function<void(ImportPipelineResult)> onDone)
{
    auto *dialog = new BusyProgressDialog(ImportText::tr("Preparing import…"), parent);
    dialog->setMinimumWidth(420);

    // Snapshot everything the worker needs; it opens its own DB connection.
    ImportPipeline::Request request;
    request.filePath = filePath;
    request.dbFile = context.database().filePath();
    request.backupsDir = context.paths().backupsDir();
    request.autoAssign = autoAssign;
    request.fmVersionId = context.fmVersionId();
    // A save game (.fm) is filtered on the worker by the save's masking option.
    Fm27SaveImport::setAttributeMasking(context.config().attributeMasking());
    const Definitions *definitions = &context.definitions();
    const std::shared_ptr<const RatingSnapshot> snapshot = context.ratingSnapshot();

    // Maps pipeline stages to label + percent. Guards against the dialog being
    // destroyed while the worker still posts progress: qApp is a stable context
    // object on the GUI thread; the QPointer is re-checked there before use.
    const auto stage = [dialogGuard = QPointer<BusyProgressDialog>(dialog)](
                           ImportPipeline::Stage s, int done, int total) {
        // 64-bit math: byte offsets on a large export overflow int.
        const auto scaled = [done, total](int from, int span) {
            return from + (total > 0 ? static_cast<int>(qint64(done) * span / total) : 0);
        };
        QString text;
        int percent = 0;
        switch (s) {
        case ImportPipeline::Stage::Backup:
            text = ImportText::tr("Creating backup…");
            percent = 2;
            break;
        case ImportPipeline::Stage::Parse:
            text = ImportText::tr("Analyzing file…");
            percent = scaled(5, 10);
            break;
        case ImportPipeline::Stage::Import:
            text = ImportText::tr("Importing players… (%1/%2)").arg(done).arg(total);
            percent = scaled(15, 40);
            break;
        case ImportPipeline::Stage::AutoAssign:
            text = ImportText::tr("Assigning roles automatically…");
            percent = 58;
            break;
        case ImportPipeline::Stage::Dwrs:
            text = RatingsUpdater::savingPhase(done, total)
                       ? ImportText::tr("Saving DWRS ratings…")
                       : ImportText::tr("Calculating DWRS ratings…");
            percent = scaled(62, 36);
            break;
        case ImportPipeline::Stage::Finalize:
            text = ImportText::tr("Preparing results…");
            percent = 99;
            break;
        }
        QMetaObject::invokeMethod(
            qApp,
            [dialogGuard, text, percent] {
                if (dialogGuard) {
                    dialogGuard->setLabelText(text);
                    dialogGuard->setValue(percent);
                }
            },
            Qt::QueuedConnection);
    };

    // The worker leaves its (large) result here and the UI thread moves it
    // out. Not QFuture::takeResult(): that resets the future, which then never
    // reports "finished" again, so waitForBackgroundTasks() (e.g. when the
    // welcome assistant or the settings are saved after an import) waited
    // forever and the app froze.
    auto holder = std::make_shared<ImportPipelineResult>();
    auto *watcher = new QFutureWatcher<void>(parent);
    QObject::connect(watcher, &QFutureWatcher<void>::finished, parent,
                     [watcher, holder, dialog, &context, onDone = std::move(onDone)] {
                         ImportPipelineResult result = std::move(*holder);
                         watcher->deleteLater();
                         dialog->finish();
                         if (result.import.success) {
                             context.adoptState(std::move(result.store),
                                                std::move(result.latestRatings),
                                                std::move(result.ratings));
                             // Tell about a missing rated column once per save.
                             const QString key = QStringLiteral("notified_unrated_columns");
                             QStringList known = context.database().setting(key).split(
                                 QLatin1Char(','), Qt::SkipEmptyParts);
                             QStringList fresh;
                             for (const QString &column : std::as_const(result.unratedColumns))
                                 if (!known.contains(column)) fresh << column;
                             result.unratedColumns = fresh;
                             if (!fresh.isEmpty())
                                 context.database().setSetting(key, (known + fresh).join(QLatin1Char(',')));
                         }
                         if (onDone)
                             onDone(result);
                     });

    const QFuture<void> future = QtConcurrent::run([request, definitions, snapshot, stage, holder] {
        *holder = ImportPipeline::run(request, *definitions, *snapshot, stage);
    });
    context.registerBackgroundTask(future);
    watcher->setFuture(future);
}

QStringList importSummaryLines(const ImportPipelineResult &result)
{
    QStringList summary;
    summary << ImportText::tr("%1 players imported (%2 of them new).")
                   .arg(result.import.playersImported)
                   .arg(result.import.newPlayers);
    summary << (result.import.fullUpload
                    ? ImportText::tr("Full export: players missing from it age by one "
                                     "export.")
                    : ImportText::tr("Partial export: only the players it contains "
                                     "were refreshed, nobody ages."));
    if (!result.import.expiredU21Names.isEmpty())
        summary << ImportText::tr("Now 22, manual U21 decision lifted (needs a registration "
                                  "slot): %1")
                       .arg(result.import.expiredU21Names.join(QStringLiteral(", ")));
    if (!result.autoAssignedUids.isEmpty())
        summary << ImportText::tr("Roles were assigned automatically to %1 players.")
                       .arg(result.autoAssignedUids.size());
    if (result.recalcRan && result.recalc.success)
        summary << ImportText::tr("DWRS: %1 ratings calculated, %2 changed entries saved.")
                       .arg(result.recalc.computed)
                       .arg(result.recalc.inserted);
    if (result.import.gameDate.isValid())
        summary << ImportText::tr("In-game date %1: history recorded for %2 players.")
                       .arg(QLocale().toString(result.import.gameDate, QLocale::ShortFormat))
                       .arg(result.import.snapshots);
    return summary;
}

QStringList importWarningLines(const ImportPipelineResult &result)
{
    QStringList warnings;
    if (!result.backupError.isEmpty())
        warnings << ImportText::tr("Backup failed: %1").arg(result.backupError);
    if (result.import.malformedRows > 0)
        warnings << ImportText::tr("%1 malformed row(s) skipped (cell count did not match the "
                             "header). Re-export the file from FM if needed.")
                        .arg(result.import.malformedRows);
    if (result.import.emptyUidRows > 0)
        warnings << ImportText::tr("%1 row(s) without UID skipped.")
                        .arg(result.import.emptyUidRows);
    if (!result.import.duplicateUidNames.isEmpty())
        warnings << ImportText::tr("Duplicate UIDs in the file — only the last row was "
                             "used: %1")
                        .arg(result.import.duplicateUidNames.mid(0, 10)
                                 .join(QStringLiteral(", ")));
    if (!result.import.unknownColumns.isEmpty())
        warnings << ImportText::tr("Unknown columns (ignored): %1")
                        .arg(result.import.unknownColumns.join(QStringLiteral(", ")));
    if (!result.import.missingColumns.isEmpty())
        warnings << ImportText::tr("Missing columns (these values are not updated): %1. Add them "
                                   "to the export view in FM if needed.")
                        .arg(result.import.missingColumns.join(QStringLiteral(", ")));
    if (!result.import.idNameConflicts.isEmpty())
        warnings << ImportText::tr("ID/name conflict skipped: %1. A numeric UID matches a "
                             "known newgen ID, but the names differ.")
                        .arg(result.import.idNameConflicts.mid(0, 10)
                                 .join(QStringLiteral("; ")));
    if (!result.import.identityChanges.isEmpty())
        warnings << ImportText::tr("Name changed under a known UID: %1. FM may have reissued the "
                             "ID to a new newgen — assigned roles and DWRS history may "
                             "still belong to the old player.")
                        .arg(result.import.identityChanges.mid(0, 10)
                                 .join(QStringLiteral("; ")));
    if (!result.autoAssignError.isEmpty())
        warnings << ImportText::tr("Automatic role assignment failed: %1")
                        .arg(result.autoAssignError);
    if (!result.unratedColumns.isEmpty())
        warnings << ImportText::tr("The rating profile uses columns that are missing from this view: %1. "
                                   "Ratings are based on the remaining attributes; add the columns to the "
                                   "export view in FM.")
                        .arg(result.unratedColumns.join(QStringLiteral(", ")));
    if (result.recalcRan && !result.recalc.success)
        warnings << ImportText::tr("DWRS calculation failed: %1").arg(result.recalc.error);
    return warnings;
}

QString importErrorText(const ImportError &error)
{
    switch (error.code) {
    case ImportErrorCode::None:
    case ImportErrorCode::Other:
        break;
    case ImportErrorCode::FileOpen:
        return ImportText::tr("File could not be opened: %1").arg(error.arg);
    case ImportErrorCode::UnsupportedFormat:
        return ImportText::tr("This FM version cannot import %1 files.")
            .arg(error.arg.toUpper());
    case ImportErrorCode::NoTable:
        return ImportText::tr("No <table> element found in the file.");
    case ImportErrorCode::NoHeaderCells:
        return ImportText::tr("The first table row contains no column headers (<th>).");
    case ImportErrorCode::NoHeaderRow:
        return ImportText::tr("No header row found in the file.");
    case ImportErrorCode::TooFewColumns:
        return ImportText::tr("Too few columns (%1), at least 10 are expected.")
            .arg(error.arg);
    case ImportErrorCode::RequiredColumnMissing:
        return ImportText::tr("Required column '%1' not found.").arg(error.arg);
    case ImportErrorCode::NoImportableRows:
        return ImportText::tr("No importable rows found (every row was missing the UID).");
    case ImportErrorCode::NotSingleRow:
        return ImportText::tr("The file must contain exactly ONE player, but %1 rows were "
                              "found.")
            .arg(error.arg);
    case ImportErrorCode::TargetNotFound:
        return ImportText::tr("Target player %1 not found.").arg(error.arg);
    case ImportErrorCode::SaveUnreadable:
        return ImportText::tr("The save could not be read. If the game is saving right now, wait "
                              "a moment and try again. After an FM patch the analyzer may need "
                              "an update.\n"
                              "\n"
                              "Details: %1")
            .arg(error.arg);
    }
    return error.message;
}

QString importErrorText(const ImportResult &result)
{
    return importErrorText(ImportError{result.errorCode, result.errorArg, result.error});
}

} // namespace fm
