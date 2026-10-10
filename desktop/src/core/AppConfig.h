#pragma once

#include <QHash>
#include <QSettings>
#include <QString>

#include <memory>

namespace fm {

// App settings stored in <dataDir>/config.ini, mirroring the legacy
// config_handler.py sections and keys 1:1 so a user's tuned legacy config.ini
// can be copied in unchanged. Missing sections are created with the legacy
// defaults on construction.
class AppConfig
{
public:
    explicit AppConfig(const QString &configFilePath);

    QString filePath() const { return m_filePath; }

    // Re-reads the file from disk (e.g. after the migration wizard replaced
    // it) and re-seeds any missing sections.
    void reload();

    // [Database]
    QString dbName() const;
    void setDbName(const QString &name);

    // [Weights] / [GKWeights] — category weights per FM version, category name
    // as in constants ("Extremely Important", "Top Importance", ...). FM24 uses
    // the legacy section names, later versions "Weights_<id>"/"GKWeights_<id>",
    // so tuning one game's weights never leaks into another.
    double weight(const QString &fmVersionId, const QString &category) const;
    void setWeight(const QString &fmVersionId, const QString &category, double value);
    double gkWeight(const QString &fmVersionId, const QString &category) const;
    void setGkWeight(const QString &fmVersionId, const QString &category, double value);
    static QString weightsSection(const QString &fmVersionId, bool goalkeeper);

    // [RoleMultipliers] — type "key" | "preferable".
    double roleMultiplier(const QString &type) const;
    void setRoleMultiplier(const QString &type, double value);

    // [APTWeights] — APT display name ("Star Player", ...); "None"/empty -> default.
    double aptWeight(const QString &apt, double defaultValue = 1.0) const;
    void setAptWeight(const QString &apt, double value);

    // [AgeThresholds] — playerType "outfielder" | "goalkeeper".
    int ageThreshold(const QString &playerType) const;
    void setAgeThreshold(const QString &playerType, int value);

    // [SelectionBonuses] — key without "_multiplier" suffix, e.g. "natural_position".
    double selectionBonus(const QString &key) const;
    void setSelectionBonus(const QString &key, double value);

    // [SquadManagement]
    int squadManagementSetting(const QString &key) const;
    void setSquadManagementSetting(const QString &key, int value);

    // [GapAnalysis]
    double gapAnalysisSetting(const QString &key) const;
    void setGapAnalysisSetting(const QString &key, double value);

    // [Freshness] — "retirement_age" (X) and "stale_after_full_uploads" (Y).
    int freshnessSetting(const QString &key) const;
    void setFreshnessSetting(const QString &key, int value);

    // [Training] — devFactor age windows: "<group>_peak_age" / "<group>_locked_age"
    // for groups "explosive", "strength", "technical".
    int trainingSetting(const QString &key) const;
    void setTrainingSetting(const QString &key, int value);

    // [ThemeSettings] — full key/value map ("current_mode", "night_primary_color", ...).
    QHash<QString, QString> themeSettings() const;
    void saveThemeSettings(const QHash<QString, QString> &settings);

    // [Import] — user-configured default folder for the FM-HTML-export file
    // dialogs. Empty = automatic default (see defaultHtmlExportDir).
    QString htmlExportDir() const;
    void setHtmlExportDir(const QString &dir);
    // "<Documents>/Sports Interactive/<game folder>" of the given FM version
    // for the current user when that folder exists, otherwise the Documents
    // folder itself.
    static QString defaultHtmlExportDir(const QString &fmVersionId);
    // Configured folder if set and existing, otherwise defaultHtmlExportDir().
    QString effectiveHtmlExportDir(const QString &fmVersionId) const;

private:
    void ensureDefaults();
    static QString iniKey(const QString &name); // "Star Player" -> "star_player"

    QString m_filePath;
    std::unique_ptr<QSettings> m_settings;
};

} // namespace fm
