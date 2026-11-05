#include "LiveSaveWatcher.h"

#include "AppContext.h"
#include "ImportRunner.h"

#include "core/AppConfig.h"
#include "core/Database.h"
#include "core/Fm27SaveFamily.h"

#include <QApplication>
#include <QFileInfo>
#include <QLocale>
#include <QWidget>

namespace fm {

namespace {

constexpr int kTickMs = 5000;
// The game writes a save in one go; a file is read only once its size and
// time have stayed the same for a whole tick and it is this old.
constexpr qint64 kSettleMs = 4000;
constexpr int kMaxFailures = 3;

} // namespace

LiveSaveWatcher::LiveSaveWatcher(AppContext &context, QWidget *parent,
                                 std::function<void(QString)> toast)
    : QObject(parent)
    , m_context(context)
    , m_parent(parent)
    , m_toast(std::move(toast))
{
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &LiveSaveWatcher::tick);
    m_timer.start();
    QTimer::singleShot(1500, this, &LiveSaveWatcher::tick);
}

void LiveSaveWatcher::checkNow()
{
    m_candidatePath.clear();
    m_failures = 0;
    m_failedModified = 0;
    tick();
}

void LiveSaveWatcher::tick()
{
    if (m_running || !m_context.hasDatabase())
        return;
    const AppConfig &config = m_context.config();
    if (!config.liveSaveEnabled())
        return;
    const QString chosen = config.liveSavePath();
    if (chosen.isEmpty())
        return;

    const Fm27SaveFamily::Member newest = Fm27SaveFamily::newest(chosen);
    if (newest.path.isEmpty())
        return;
    const qint64 modified = newest.modified.toMSecsSinceEpoch();
    if (modified <= config.liveSaveLastModified())
        return;
    if (modified == m_failedModified && m_failures >= kMaxFailures)
        return;

    const QString db = m_context.database().filePath();
    const bool settled = m_candidateDb == db && m_candidatePath == newest.path
                         && m_candidateModified == modified && m_candidateSize == newest.size
                         && QDateTime::currentMSecsSinceEpoch() - modified >= kSettleMs;
    m_candidateDb = db;
    m_candidatePath = newest.path;
    m_candidateModified = modified;
    m_candidateSize = newest.size;
    if (!settled)
        return;
    // Wait while the user is in a dialog or another job runs (an import, a
    // recalculation): the next tick tries again.
    if (QApplication::activeModalWidget() || m_context.hasRunningBackgroundTask())
        return;
    startImport(newest.path, modified);
}

void LiveSaveWatcher::startImport(const QString &path, qint64 modifiedMs)
{
    m_running = true;
    const QString db = m_context.database().filePath();
    runImportPipeline(
        m_context, m_parent, path, /*autoAssign=*/true,
        [this, path, modifiedMs, db](const ImportPipelineResult &result) {
            m_running = false;
            const QString name = QFileInfo(path).completeBaseName();
            if (m_context.database().filePath() != db)
                return; // the user switched saves meanwhile
            if (!result.import.success) {
                if (m_failedModified != modifiedMs) {
                    m_failedModified = modifiedMs;
                    m_failures = 0;
                }
                ++m_failures;
                m_candidatePath.clear();
                if (m_failures >= kMaxFailures && m_toast)
                    m_toast(tr("Spielstand „%1“ konnte nicht eingelesen werden: %2")
                                .arg(name, importErrorText(result.import)));
                return;
            }
            m_failures = 0;
            m_context.config().setLiveSaveLastModified(modifiedMs);
            if (m_toast)
                m_toast(tr("Spielstand „%1“ eingelesen (%2): %3 Spieler aktualisiert.")
                            .arg(name,
                                 QLocale().toString(QDateTime::fromMSecsSinceEpoch(modifiedMs),
                                                    QLocale::ShortFormat))
                            .arg(result.import.playersImported));
        });
}

} // namespace fm
