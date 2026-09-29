#include "ImportRunner.h"

#include "AppContext.h"
#include "widgets/BusyProgressDialog.h"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrentRun>

namespace fm {

namespace {

// Translation context "ImportRunner" for this free-function module. A plain
// helper taking a const char* hid every string from lupdate, so the English
// UI showed them in German.
struct ImportText {
    Q_DECLARE_TR_FUNCTIONS(ImportRunner)
};

} // namespace

void runImportPipeline(AppContext &context, QWidget *parent, const QString &filePath,
                       bool autoAssign, std::function<void(ImportPipelineResult)> onDone)
{
    auto *dialog = new BusyProgressDialog(ImportText::tr("Import wird vorbereitet…"), parent);
    dialog->setMinimumWidth(420);

    // Snapshot everything the worker needs; it opens its own DB connection.
    ImportPipeline::Request request;
    request.filePath = filePath;
    request.dbFile = context.database().filePath();
    request.backupsDir = context.paths().backupsDir();
    request.autoAssign = autoAssign;
    request.fmVersionId = context.fmVersionId();
    const Definitions *definitions = &context.definitions();
    const DwrsEngine *engine = &context.dwrsEngine();

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
            text = ImportText::tr("Backup wird erstellt…");
            percent = 2;
            break;
        case ImportPipeline::Stage::Parse:
            text = ImportText::tr("Datei wird analysiert…");
            percent = scaled(5, 10);
            break;
        case ImportPipeline::Stage::Import:
            text = ImportText::tr("Spieler werden importiert… (%1/%2)").arg(done).arg(total);
            percent = scaled(15, 40);
            break;
        case ImportPipeline::Stage::AutoAssign:
            text = ImportText::tr("Rollen werden automatisch zugewiesen…");
            percent = 58;
            break;
        case ImportPipeline::Stage::Dwrs:
            text = ImportText::tr("DWRS-Bewertungen werden berechnet…");
            percent = scaled(62, 36);
            break;
        case ImportPipeline::Stage::Finalize:
            text = ImportText::tr("Ergebnis wird vorbereitet…");
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

    auto *watcher = new QFutureWatcher<ImportPipelineResult>(parent);
    QObject::connect(watcher, &QFutureWatcher<ImportPipelineResult>::finished, parent,
                     [watcher, dialog, &context, onDone = std::move(onDone)] {
                         // takeResult(): move the (large) state out instead of
                         // copying it.
                         ImportPipelineResult result = watcher->future().takeResult();
                         watcher->deleteLater();
                         dialog->finish();
                         if (result.import.success)
                             context.adoptState(std::move(result.store),
                                                std::move(result.latestRatings),
                                                std::move(result.ratings));
                         if (onDone)
                             onDone(result);
                     });

    const auto future = QtConcurrent::run([request, definitions, engine, stage] {
        return ImportPipeline::run(request, *definitions, *engine, stage);
    });
    context.registerBackgroundTask(QFuture<void>(future));
    watcher->setFuture(future);
}

QStringList importSummaryLines(const ImportPipelineResult &result)
{
    QStringList summary;
    summary << ImportText::tr("%1 Spieler importiert (davon %2 neu).")
                   .arg(result.import.playersImported)
                   .arg(result.import.newPlayers);
    if (!result.autoAssignedUids.isEmpty())
        summary << ImportText::tr("%1 Spielern wurden automatisch Rollen zugewiesen.")
                       .arg(result.autoAssignedUids.size());
    if (result.recalcRan && result.recalc.success)
        summary << ImportText::tr("DWRS: %1 Bewertungen berechnet, %2 geänderte Einträge gespeichert.")
                       .arg(result.recalc.computed)
                       .arg(result.recalc.inserted);
    return summary;
}

QStringList importWarningLines(const ImportPipelineResult &result)
{
    QStringList warnings;
    if (!result.backupError.isEmpty())
        warnings << ImportText::tr("Backup fehlgeschlagen: %1").arg(result.backupError);
    if (result.import.malformedRows > 0)
        warnings << ImportText::tr("%1 fehlerhafte Zeile(n) übersprungen (Zellenzahl passte nicht "
                             "zur Kopfzeile). Exportiere die Datei ggf. neu aus FM.")
                        .arg(result.import.malformedRows);
    if (result.import.emptyUidRows > 0)
        warnings << ImportText::tr("%1 Zeile(n) ohne UID übersprungen.")
                        .arg(result.import.emptyUidRows);
    if (!result.import.duplicateUidNames.isEmpty())
        warnings << ImportText::tr("Doppelte UIDs in der Datei — nur die letzte Zeile wurde "
                             "übernommen: %1")
                        .arg(result.import.duplicateUidNames.mid(0, 10)
                                 .join(QStringLiteral(", ")));
    if (!result.import.unknownColumns.isEmpty())
        warnings << ImportText::tr("Unbekannte Spalten (werden ignoriert): %1")
                        .arg(result.import.unknownColumns.join(QStringLiteral(", ")));
    if (!result.import.idNameConflicts.isEmpty())
        warnings << ImportText::tr("ID/Namens-Konflikt übersprungen: %1. Eine numerische UID "
                             "entspricht einer bekannten Newgen-ID, aber die Namen "
                             "unterscheiden sich.")
                        .arg(result.import.idNameConflicts.mid(0, 10)
                                 .join(QStringLiteral("; ")));
    if (!result.import.identityChanges.isEmpty())
        warnings << ImportText::tr("Name unter bekannter UID geändert: %1. FM hat die ID evtl. an "
                             "einen neuen Newgen vergeben — zugewiesene Rollen und "
                             "DWRS-Historie gehören ggf. noch zum alten Spieler.")
                        .arg(result.import.identityChanges.mid(0, 10)
                                 .join(QStringLiteral("; ")));
    if (!result.autoAssignError.isEmpty())
        warnings << ImportText::tr("Automatische Rollen-Zuweisung fehlgeschlagen: %1")
                        .arg(result.autoAssignError);
    if (result.recalcRan && !result.recalc.success)
        warnings << ImportText::tr("DWRS-Berechnung fehlgeschlagen: %1").arg(result.recalc.error);
    return warnings;
}

} // namespace fm
