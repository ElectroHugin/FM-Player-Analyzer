#pragma once

// Follows an FM27 save while the app is open (Settings: "Spielstand live
// verfolgen"). Every few seconds it looks at the files of the chosen career
// (Fm27SaveFamily); when the game has written a newer one (a manual save or an
// autosave) and the file has stopped changing, it reads it in through the
// regular import pipeline and tells the user with a toast.

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

class QWidget;

namespace fm {

class AppContext;

class LiveSaveWatcher : public QObject
{
    Q_OBJECT
public:
    // parent hosts the progress dialog; toast shows a short message.
    LiveSaveWatcher(AppContext &context, QWidget *parent, std::function<void(QString)> toast);

    // Checks right away (e.g. after the settings changed).
    void checkNow();

private:
    void tick();
    void startImport(const QString &path, qint64 modifiedMs);

    AppContext &m_context;
    QWidget *m_parent;
    std::function<void(QString)> m_toast;
    QTimer m_timer;
    bool m_running = false;

    // The newer file seen last tick; read once it is unchanged for a tick.
    QString m_candidateDb;
    QString m_candidatePath;
    qint64 m_candidateModified = 0;
    qint64 m_candidateSize = -1;
    // A file that failed to read is retried a few times (the game may still
    // have been writing), then left until a newer one appears.
    qint64 m_failedModified = 0;
    int m_failures = 0;
};

} // namespace fm
