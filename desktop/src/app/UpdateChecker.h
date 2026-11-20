#pragma once

#include "core/UpdateCheck.h"

#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

namespace fm {

class AppPaths;

// Asks GitHub for the newest release of the app (one GET to the public
// Releases API, nothing about the user is sent). The app is offline-first:
// nothing here runs unless the caller starts a check, and the startup check
// is only started when the user switched it on.
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    enum class Outcome { NewVersion, UpToDate, Failed };
    struct Result
    {
        Outcome outcome = Outcome::Failed;
        ReleaseInfo release; // set for NewVersion
        QString error;       // set for Failed (for the log, not translated)
    };

    explicit UpdateChecker(AppPaths &paths, QObject *parent = nullptr);

    // Starts a check (test builds included when the user chose so). A check
    // already running is not started twice.
    void check();
    bool isRunning() const { return !m_reply.isNull(); }

signals:
    void finished(const fm::UpdateChecker::Result &result);

private:
    void handleReply();

    AppPaths &m_paths;
    QNetworkAccessManager *m_network = nullptr; // created on the first check
    QPointer<QNetworkReply> m_reply;
};

} // namespace fm
