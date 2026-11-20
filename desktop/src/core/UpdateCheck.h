#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace fm {

// Pure logic of the optional update check (no network): version comparison
// and picking the newest release from a GitHub Releases API reply. The
// network request itself lives in the app (UpdateChecker), and only runs
// when the user switched the check on.

// A version as the app and the release tags spell it: "2.3.1",
// "v2.3.1-test.2". A suffix marks a pre-release; it sorts below the plain
// version, and pre-releases of one version sort by their trailing number.
struct AppVersion
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    QString preRelease; // "" for a final release, e.g. "test.2"
    int preReleaseNumber = 0;

    bool isPreRelease() const { return !preRelease.isEmpty(); }

    static std::optional<AppVersion> parse(const QString &text);
    QString toString() const;
};

// <0, 0, >0 like strcmp.
int compareVersions(const AppVersion &a, const AppVersion &b);

struct ReleaseInfo
{
    QString version;  // without the leading "v", e.g. "2.4.0"
    QString name;     // release title
    QString notes;    // release text (Markdown)
    QString pageUrl;  // the release's web page (download)
    bool preRelease = false;
};

// Reads the reply of GET /repos/<owner>/<repo>/releases and returns the
// newest release that is newer than currentVersion. Drafts are skipped,
// pre-releases (test builds) only count when includePreReleases is set.
// Returns nothing when there is no newer release or the reply is unusable;
// *error (optional) is set only for an unusable reply.
std::optional<ReleaseInfo> newestReleaseAfter(const QByteArray &releasesJson,
                                              const QString &currentVersion,
                                              bool includePreReleases,
                                              QString *error = nullptr);

// The part of a changelog (CHANGELOG.md or one of its translations) that is
// new between two versions: every "## X.Y.Z" section after fromVersion up to
// and including toVersion, newest first. A test build (toVersion with a
// suffix such as "-test.2") also gets the "## Unreleased" section, where the
// changes of the coming version are collected. Without fromVersion (unknown
// previous version) only the section of toVersion itself is returned. With
// several sections each keeps its heading, one level lower ("### 2.3.2 – …").
// Returns an empty string when the changelog has nothing for that range.
QString changelogNotes(const QString &changelog, const QString &fromVersion,
                       const QString &toVersion);

} // namespace fm
