#include "RecalcHelper.h"

#include "AppContext.h"
#include "widgets/BusyProgressDialog.h"
#include "core/RatingsUpdater.h"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QPointer>
#include <QSet>
#include <QtConcurrentRun>

namespace fm {

void recalcDwrsFor(AppContext &context, QWidget *parent, const QStringList &affectedUids,
                   std::function<void(QString)> onDone)
{
    auto *dialog = new BusyProgressDialog(QObject::tr("DWRS-Bewertungen werden berechnet…"),
                                          parent);

    const QString dbFile = context.database().filePath();
    const QStringList validRoles = context.definitions().validRoles();
    const DwrsEngine *engine = &context.dwrsEngine();
    // Copy only the affected players (often one), not the whole store.
    std::vector<Player> players;
    QList<int> ids;
    players.reserve(static_cast<size_t>(affectedUids.size()));
    for (const QString &uid : affectedUids) {
        if (const Player *player = context.store().findByUid(uid)) {
            players.push_back(*player);
            ids << player->id;
        }
    }

    auto *watcher = new QFutureWatcher<RatingsUpdater::Result>(parent);
    QObject::connect(watcher, &QFutureWatcher<RatingsUpdater::Result>::finished, parent,
                     [watcher, dialog, &context, ids, onDone = std::move(onDone)] {
                         const RatingsUpdater::Result result = watcher->result();
                         watcher->deleteLater();
                         dialog->finish();
                         if (result.success)
                             context.refreshRatings(ids);
                         if (onDone)
                             onDone(result.success ? QString() : result.error);
                     });

    const QFuture<RatingsUpdater::Result> future = QtConcurrent::run(
        [dbFile, players = std::move(players), engine, validRoles,
         dialogGuard = QPointer<BusyProgressDialog>(dialog)] {
            Database db(Database::uniqueConnectionName(QStringLiteral("recalc_helper")));
            if (!db.open(dbFile)) {
                RatingsUpdater::Result result;
                result.error = db.errorString();
                return result;
            }
            // Every copied player is affected: the whole (small) list is the subset.
            std::vector<int> subset(players.size());
            for (size_t i = 0; i < players.size(); ++i)
                subset[i] = static_cast<int>(i);
            return RatingsUpdater::updateDwrsRatings(
                db, players, *engine, validRoles, subset,
                [dialogGuard](int current, int total) {
                    const int percent = total > 0 ? current * 100 / total : 0;
                    QMetaObject::invokeMethod(
                        qApp,
                        [dialogGuard, percent] {
                            if (dialogGuard)
                                dialogGuard->setValue(percent);
                        },
                        Qt::QueuedConnection);
                });
        });
    context.registerBackgroundTask(QFuture<void>(future));
    watcher->setFuture(future);
}

} // namespace fm
