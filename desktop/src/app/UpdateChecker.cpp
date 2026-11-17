#include "UpdateChecker.h"

#include "AppPaths.h"
#include "core/Version.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace fm {

namespace {

// Public list of the app's releases (finished ones and test builds).
constexpr auto kReleasesUrl =
    "https://api.github.com/repos/ElectroHugin/FM-Player-Analyzer/releases?per_page=30";
constexpr int kTimeoutMs = 15000;

} // namespace

UpdateChecker::UpdateChecker(AppPaths &paths, QObject *parent)
    : QObject(parent)
    , m_paths(paths)
{
}

void UpdateChecker::check()
{
    if (isRunning())
        return;
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(QString::fromLatin1(kReleasesUrl))};
    request.setRawHeader("Accept", "application/vnd.github+json");
    // GitHub requires a User-Agent; it names only the app and its version.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("FMPlayerAnalyzer/%1").arg(appVersion()));
    request.setTransferTimeout(kTimeoutMs);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::handleReply);
}

void UpdateChecker::handleReply()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();

    Result result;
    if (reply->error() != QNetworkReply::NoError) {
        result.error = reply->errorString();
        emit finished(result);
        return;
    }
    m_paths.setLastUpdateCheck(QDateTime::currentDateTime());
    QString error;
    const std::optional<ReleaseInfo> release = newestReleaseAfter(
        reply->readAll(), appVersion(), m_paths.updateIncludesTestBuilds(), &error);
    if (release) {
        result.outcome = Outcome::NewVersion;
        result.release = *release;
    } else if (error.isEmpty()) {
        result.outcome = Outcome::UpToDate;
    } else {
        result.error = error;
    }
    emit finished(result);
}

} // namespace fm
