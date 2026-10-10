#include "AppPaths.h"

#include "core/Constants.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace fm {

namespace {

// Single flat folder %LOCALAPPDATA%\FM24PlayerAnalyzer — Qt's app-specific
// locations would nest organization + application name.
QString baseAppFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(QStringLiteral("FM24PlayerAnalyzer"));
}

} // namespace

AppPaths::AppPaths()
{
    const QString configLocation = baseAppFolder();
    QDir().mkpath(configLocation);
    m_settings = std::make_unique<QSettings>(
        QDir(configLocation).filePath(QStringLiteral("bootstrap.ini")), QSettings::IniFormat);
}

bool AppPaths::isFirstRun() const
{
    return !m_settings->contains(QStringLiteral("dataDir"));
}

QString AppPaths::defaultDataDir()
{
    // %LOCALAPPDATA%\FM24PlayerAnalyzer
    return baseAppFolder();
}

QString AppPaths::dataDir() const
{
    return m_settings->value(QStringLiteral("dataDir"), defaultDataDir()).toString();
}

void AppPaths::setDataDir(const QString &dir)
{
    m_settings->setValue(QStringLiteral("dataDir"), dir);
    m_settings->sync();
}

QString AppPaths::databasesDir() const
{
    return QDir(dataDir()).filePath(QStringLiteral("databases"));
}

QString AppPaths::backupsDir() const
{
    return QDir(dataDir()).filePath(QStringLiteral("backups"));
}

QString AppPaths::assetsDir() const
{
    return QDir(dataDir()).filePath(QStringLiteral("assets"));
}

QString AppPaths::configFile() const
{
    return QDir(dataDir()).filePath(QStringLiteral("config.ini"));
}

QString AppPaths::definitionsFile(const QString &fmVersionId) const
{
    return QDir(dataDir()).filePath(fmDataVersion(fmVersionId).definitionsFileName);
}

QString AppPaths::databaseFile(const QString &dbName) const
{
    return QDir(databasesDir()).filePath(dbName + QStringLiteral(".db"));
}

bool AppPaths::ensureDataDirInitialized()
{
    QDir dir(dataDir());
    return dir.mkpath(QStringLiteral(".")) && dir.mkpath(QStringLiteral("databases"))
           && dir.mkpath(QStringLiteral("backups")) && dir.mkpath(QStringLiteral("assets"));
}

bool AppPaths::ensureDefinitionsFile(const QString &fmVersionId)
{
    // Seed the shipped role/tactic definitions on first use of a version.
    const QString target = definitionsFile(fmVersionId);
    if (QFile::exists(target))
        return true;
    QFile bundled(QStringLiteral(":/defaults/") + fmDataVersion(fmVersionId).definitionsFileName);
    if (!bundled.copy(target))
        return false;
    // Resource copies are read-only; the user edits this file via the app.
    QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser
                                      | QFile::WriteUser);
    return true;
}

QByteArray AppPaths::windowGeometry() const
{
    return m_settings->value(QStringLiteral("windowGeometry")).toByteArray();
}

void AppPaths::setWindowGeometry(const QByteArray &geometry)
{
    m_settings->setValue(QStringLiteral("windowGeometry"), geometry);
    m_settings->sync();
}

QString AppPaths::language() const
{
    return m_settings->value(QStringLiteral("language"), QStringLiteral("en")).toString();
}

void AppPaths::setLanguage(const QString &language)
{
    m_settings->setValue(QStringLiteral("language"), language);
    m_settings->sync();
}

} // namespace fm
