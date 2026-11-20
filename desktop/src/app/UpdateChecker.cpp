#include "UpdateChecker.h"

#include "AppPaths.h"
#include "WhatsNewDialog.h"
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
// A file of the repository as it was at a release tag (raw text).
constexpr auto kFileUrl =
    "https://api.github.com/repos/ElectroHugin/FM-Player-Analyzer/contents/%1?ref=%2";
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
    QNetworkRequest request = makeRequest(QString::fromLatin1(kReleasesUrl));
    request.setRawHeader("Accept", "application/vnd.github+json");
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::handleReply);
}

QNetworkRequest UpdateChecker::makeRequest(const QString &url) const
{
    QNetworkRequest request{QUrl(url)};
    // GitHub requires a User-Agent; it names only the app and its version.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("FMPlayerAnalyzer/%1").arg(appVersion()));
    request.setTransferTimeout(kTimeoutMs);
    return request;
}

void UpdateChecker::fetchTranslatedNotes(const Result &result)
{
    m_pending = result;
    const QString path = QStringLiteral("desktop/changelog/")
                         + WhatsNewDialog::changelogFileName(m_paths.language());
    QNetworkRequest request = makeRequest(
        QString::fromLatin1(kFileUrl).arg(path, QStringLiteral("v") + result.release.version));
    request.setRawHeader("Accept", "application/vnd.github.raw+json");
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::handleNotesReply);
}

void UpdateChecker::handleNotesReply()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();
    Result result = m_pending;
    // Without a translation (older release, no network) the English notes stay.
    if (reply->error() == QNetworkReply::NoError) {
        const QString notes = changelogNotes(QString::fromUtf8(reply->readAll()), appVersion(),
                                             result.release.version);
        if (!notes.isEmpty())
            result.release.notes = notes;
    }
    emit finished(result);
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
        if (m_paths.language() != QLatin1String("en")) {
            fetchTranslatedNotes(result);
            return;
        }
    } else if (error.isEmpty()) {
        result.outcome = Outcome::UpToDate;
    } else {
        result.error = error;
    }
    emit finished(result);
}

} // namespace fm
