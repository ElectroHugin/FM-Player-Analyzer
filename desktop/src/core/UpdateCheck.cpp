#include "UpdateCheck.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace fm {

std::optional<AppVersion> AppVersion::parse(const QString &text)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^v?(\d+)\.(\d+)(?:\.(\d+))?(?:-([0-9A-Za-z.]+))?$)"));
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    if (!match.hasMatch())
        return std::nullopt;
    AppVersion version;
    version.major = match.captured(1).toInt();
    version.minor = match.captured(2).toInt();
    version.patch = match.captured(3).isEmpty() ? 0 : match.captured(3).toInt();
    version.preRelease = match.captured(4);
    if (version.isPreRelease()) {
        static const QRegularExpression trailingNumber(QStringLiteral(R"((\d+)$)"));
        const QRegularExpressionMatch number = trailingNumber.match(version.preRelease);
        if (number.hasMatch())
            version.preReleaseNumber = number.captured(1).toInt();
    }
    return version;
}

QString AppVersion::toString() const
{
    QString text = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
    if (isPreRelease())
        text += QLatin1Char('-') + preRelease;
    return text;
}

int compareVersions(const AppVersion &a, const AppVersion &b)
{
    if (a.major != b.major)
        return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor)
        return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch)
        return a.patch < b.patch ? -1 : 1;
    // The final release comes after all of its pre-releases.
    if (a.isPreRelease() != b.isPreRelease())
        return a.isPreRelease() ? -1 : 1;
    if (a.preReleaseNumber != b.preReleaseNumber)
        return a.preReleaseNumber < b.preReleaseNumber ? -1 : 1;
    return 0;
}

std::optional<ReleaseInfo> newestReleaseAfter(const QByteArray &releasesJson,
                                              const QString &currentVersion,
                                              bool includePreReleases, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(releasesJson, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error)
            *error = QStringLiteral("unexpected reply");
        return std::nullopt;
    }
    const std::optional<AppVersion> current = AppVersion::parse(currentVersion);
    if (!current) {
        if (error)
            *error = QStringLiteral("unknown app version");
        return std::nullopt;
    }

    std::optional<ReleaseInfo> best;
    AppVersion bestVersion = *current;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject release = value.toObject();
        if (release.value(QStringLiteral("draft")).toBool())
            continue;
        const bool preRelease = release.value(QStringLiteral("prerelease")).toBool();
        const std::optional<AppVersion> version =
            AppVersion::parse(release.value(QStringLiteral("tag_name")).toString());
        if (!version)
            continue;
        // Test builds are published as pre-releases; a tag with a suffix counts
        // as one too, whatever the flag says.
        if ((preRelease || version->isPreRelease()) && !includePreReleases)
            continue;
        if (compareVersions(*version, bestVersion) <= 0)
            continue;
        bestVersion = *version;
        ReleaseInfo info;
        info.version = version->toString();
        info.name = release.value(QStringLiteral("name")).toString();
        info.notes = release.value(QStringLiteral("body")).toString();
        info.pageUrl = release.value(QStringLiteral("html_url")).toString();
        info.preRelease = preRelease || version->isPreRelease();
        best = info;
    }
    return best;
}

QString changelogNotes(const QString &changelog, const QString &fromVersion,
                       const QString &toVersion)
{
    const std::optional<AppVersion> to = AppVersion::parse(toVersion);
    if (!to)
        return {};
    const std::optional<AppVersion> from = AppVersion::parse(fromVersion);
    AppVersion toBase = *to;
    toBase.preRelease.clear();
    toBase.preReleaseNumber = 0;

    static const QRegularExpression versionHeading(
        QStringLiteral(R"(^##\s+v?(\d+\.\d+(?:\.\d+)?)\b)"));
    static const QRegularExpression unreleasedHeading(
        QStringLiteral(R"(^##\s+Unreleased\b)"), QRegularExpression::CaseInsensitiveOption);

    struct Section
    {
        QString heading;
        QStringList lines;
    };
    QList<Section> picked;
    bool collecting = false;
    const QStringList lines = changelog.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        QString line = raw;
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (line.startsWith(QLatin1String("## ")) || line == QLatin1String("##")) {
            collecting = false;
            bool take = false;
            if (unreleasedHeading.match(line).hasMatch()) {
                take = to->isPreRelease();
            } else if (const QRegularExpressionMatch m = versionHeading.match(line); m.hasMatch()) {
                const std::optional<AppVersion> section = AppVersion::parse(m.captured(1));
                if (section) {
                    if (!from)
                        take = compareVersions(*section, toBase) == 0 && !to->isPreRelease();
                    else
                        take = compareVersions(*section, *from) > 0
                               && compareVersions(*section, *to) <= 0;
                }
            }
            if (take) {
                picked.append({line.mid(3).trimmed(), {}});
                collecting = true;
            }
            continue;
        }
        if (line.startsWith(QLatin1String("# ")))
            collecting = false; // the file title
        else if (collecting)
            picked.last().lines.append(line);
    }

    QStringList parts;
    for (const Section &section : picked) {
        const QString body = section.lines.join(QLatin1Char('\n')).trimmed();
        if (body.isEmpty())
            continue;
        parts.append(picked.size() > 1 ? QStringLiteral("### %1\n\n%2").arg(section.heading, body)
                                       : body);
    }
    return parts.join(QStringLiteral("\n\n"));
}

} // namespace fm
