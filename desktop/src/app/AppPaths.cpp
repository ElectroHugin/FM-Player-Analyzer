#include "AppPaths.h"

#include "core/Constants.h"
#include "core/Definitions.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <QDir>
#include <QLocale>
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

QString AppPaths::colorTemplatesFile() const
{
    return QDir(dataDir()).filePath(QStringLiteral("color_templates.json"));
}

QString AppPaths::dwrsColorSchemesFile() const
{
    return QDir(dataDir()).filePath(QStringLiteral("dwrs_color_schemes.json"));
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

QString AppPaths::ratingLibraryFile() const
{
    return QDir(dataDir()).filePath(QStringLiteral("rating_profiles.json"));
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
    QFile bundled(QStringLiteral(":/defaults/") + fmDataVersion(fmVersionId).definitionsFileName);
    if (QFile::exists(target)) {
        // Newer shipped data (see Definitions::applyShippedUpdate): back up
        // the user's file, then write the merged one.
        QFile userFile(target);
        if (!bundled.open(QIODevice::ReadOnly) || !userFile.open(QIODevice::ReadOnly))
            return true;
        const QJsonObject shipped = QJsonDocument::fromJson(bundled.readAll()).object();
        QJsonObject user = QJsonDocument::fromJson(userFile.readAll()).object();
        userFile.close();
        bundled.close();
        if (user.isEmpty() || !Definitions::applyShippedUpdate(shipped, user))
            return true;
        QDir().mkpath(backupsDir());
        const QString backup = QDir(backupsDir()).filePath(
            QFileInfo(target).completeBaseName() + QStringLiteral("_before_update_")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))
            + QStringLiteral(".json"));
        QFile::copy(target, backup);
        if (userFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
            userFile.write(QJsonDocument(user).toJson(QJsonDocument::Indented));
        return true;
    }
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

bool AppPaths::updateQuestionAnswered() const
{
    return m_settings->contains(QStringLiteral("Updates/check"));
}

bool AppPaths::updateCheckEnabled() const
{
    return m_settings->value(QStringLiteral("Updates/check"), false).toBool();
}

void AppPaths::setUpdateCheckEnabled(bool enabled)
{
    m_settings->setValue(QStringLiteral("Updates/check"), enabled);
    m_settings->sync();
}

bool AppPaths::updateIncludesTestBuilds() const
{
    return m_settings->value(QStringLiteral("Updates/testBuilds"), false).toBool();
}

void AppPaths::setUpdateIncludesTestBuilds(bool include)
{
    m_settings->setValue(QStringLiteral("Updates/testBuilds"), include);
    m_settings->sync();
}

QString AppPaths::skippedUpdateVersion() const
{
    return m_settings->value(QStringLiteral("Updates/skippedVersion")).toString();
}

void AppPaths::setSkippedUpdateVersion(const QString &version)
{
    m_settings->setValue(QStringLiteral("Updates/skippedVersion"), version);
    m_settings->sync();
}

QDateTime AppPaths::lastUpdateCheck() const
{
    return m_settings->value(QStringLiteral("Updates/lastCheck")).toDateTime();
}

void AppPaths::setLastUpdateCheck(const QDateTime &when)
{
    m_settings->setValue(QStringLiteral("Updates/lastCheck"), when);
    m_settings->sync();
}

QString AppPaths::currency() const
{
    return m_settings->value(QStringLiteral("currency")).toString();
}

void AppPaths::setCurrency(const QString &code)
{
    m_settings->setValue(QStringLiteral("currency"), code);
    m_settings->sync();
}

QList<AppPaths::UiLanguage> AppPaths::uiLanguages()
{
    QList<UiLanguage> languages{
        {QStringLiteral("en"), QStringLiteral("English")},
    };
    // Every bundled translation (:/i18n/fmplayeranalyzer_<code>.qm), labelled
    // with its own name for the language. The English file only holds plural
    // forms; English itself is the source language.
    const QString prefix = QStringLiteral("fmplayeranalyzer_");
    const QStringList files = QDir(QStringLiteral(":/i18n"))
                                  .entryList({prefix + QStringLiteral("*.qm")}, QDir::Files, QDir::Name);
    for (const QString &file : files) {
        const QString code = file.mid(prefix.size()).chopped(3);
        if (code == QLatin1String("en"))
            continue;
        QString label = QLocale(code).nativeLanguageName();
        if (label.isEmpty())
            label = code;
        label[0] = label[0].toUpper();
        languages.append({code, label});
    }
    return languages;
}

} // namespace fm
