#pragma once

#include <QDateTime>
#include <QList>
#include <QSettings>
#include <QString>

#include <memory>

namespace fm {

// Bootstrap settings in %APPDATA%\FM24PlayerAnalyzer\bootstrap.ini: only the
// data directory location, window geometry and similar host-level state.
// Everything else (weights, theme colors, active DB) lives in
// <dataDir>/config.ini so the whole data directory stays self-contained and
// portable.
class AppPaths
{
public:
    AppPaths();

    // True until a data directory has been chosen (first run).
    bool isFirstRun() const;

    // The data directory (default: %LOCALAPPDATA%\FM24PlayerAnalyzer).
    QString dataDir() const;
    void setDataDir(const QString &dir);
    static QString defaultDataDir();

    // Derived locations inside the data directory.
    QString databasesDir() const;
    QString backupsDir() const;
    QString assetsDir() const;
    QString configFile() const;      // <dataDir>/config.ini
    // The user's own color templates, app-wide: <dataDir>/color_templates.json.
    QString colorTemplatesFile() const;
    // Own DWRS colour schemes (app-wide, like colour templates).
    QString dwrsColorSchemesFile() const;
    // Roles/tactics of one FM version: <dataDir>/definitions.json for FM24,
    // <dataDir>/definitions_<id>.json for later releases.
    QString definitionsFile(const QString &fmVersionId) const;
    QString databaseFile(const QString &dbName) const;
    // Shared rating profile library: <dataDir>/rating_profiles.json
    QString ratingLibraryFile() const;

    // Creates the directory skeleton. Returns false on IO failure.
    bool ensureDataDirInitialized();

    // Seeds the version's definitions file from the bundled default if it is
    // missing. Returns false if there is no bundled default or on IO failure.
    bool ensureDefinitionsFile(const QString &fmVersionId);

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);

    // UI language, app-global (host-level). "en" (default) uses the English
    // source strings. Any other code loads fmplayeranalyzer_<code>.qm on top
    // of the English plural forms, so strings it does not translate yet stay
    // English.
    QString language() const;
    void setLanguage(const QString &language);

    // Languages offered in the menus, in display order. The label is the
    // endonym, shown the same regardless of the current UI language.
    struct UiLanguage
    {
        QString code;
        QString label;
    };
    static QList<UiLanguage> uiLanguages();

    // Optional update check, app-global. Off until the user switches it on:
    // the app stays offline otherwise. The question at first start is asked
    // once; answered either way, it does not come back.
    bool updateQuestionAnswered() const;
    bool updateCheckEnabled() const;
    void setUpdateCheckEnabled(bool enabled); // also marks the question answered
    bool updateIncludesTestBuilds() const;
    void setUpdateIncludesTestBuilds(bool include);
    // A version the user chose to skip: no startup notice for it.
    QString skippedUpdateVersion() const;
    void setSkippedUpdateVersion(const QString &version);
    QDateTime lastUpdateCheck() const;
    void setLastUpdateCheck(const QDateTime &when);

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace fm
