#include "Fm27SaveFamily.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace fm::Fm27SaveFamily {

namespace {

// " (v02)" before the extension marks an older version of the same save.
const QRegularExpression &versionSuffix()
{
    static const QRegularExpression re(QStringLiteral("^(.*) \\(v\\d+\\)$"));
    return re;
}

QString stripVersion(const QString &stem)
{
    const QRegularExpressionMatch match = versionSuffix().match(stem);
    return match.hasMatch() ? match.captured(1) : stem;
}

} // namespace

QString baseName(const QString &savePath)
{
    const QFileInfo info(savePath);
    if (info.suffix().compare(QLatin1String("fm"), Qt::CaseInsensitive) != 0)
        return {};
    return stripVersion(info.completeBaseName());
}

bool belongsTo(const QString &fileName, const QString &base)
{
    if (base.isEmpty())
        return false;
    const QFileInfo info(fileName);
    if (info.suffix().compare(QLatin1String("fm"), Qt::CaseInsensitive) != 0)
        return false;
    return stripVersion(info.completeBaseName()).compare(base, Qt::CaseInsensitive) == 0;
}

Member newest(const QString &savePath)
{
    const QString base = baseName(savePath);
    if (base.isEmpty())
        return {};
    const QDir dir = QFileInfo(savePath).absoluteDir();
    Member best;
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.fm")}, QDir::Files); // filters ignore case
    for (const QFileInfo &file : files) {
        if (!belongsTo(file.fileName(), base))
            continue;
        const QDateTime modified = file.lastModified();
        if (best.path.isEmpty() || modified > best.modified) {
            best.path = file.absoluteFilePath();
            best.modified = modified;
            best.size = file.size();
        }
    }
    return best;
}

} // namespace fm::Fm27SaveFamily
