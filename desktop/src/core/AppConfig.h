#pragma once

#include <QDateTime>
#include <QHash>
#include <QReadWriteLock>
#include <QSet>
#include <QSettings>
#include <QString>

#include <memory>

namespace fm {

class Database;

// Settings with two homes:
//
// * App-wide (host) settings in <dataDir>/config.ini: last save, start-screen
//   options, recently opened saves, one-off migrations, and the look of the
//   start screen ([StartTheme], a copy of the last opened save's theme).
//
// * Profile settings of the open save: weights, analysis options, the full
//   color scheme with day/night mode and the import folder. Since 2.1.0 they
//   live inside the save (ProfileStore, table profile_settings) once
//   attachProfile() was called; every getter/setter below then reads/writes
//   the open save, so two saves never share them.
//
// Without an attached profile (start screen, command-line tools, tests, and
// 2.0.4 behavior) the profile keys are read from and written to config.ini as
// before. config.ini keeps the legacy config_handler.py sections and keys 1:1,
// so a tuned legacy config.ini can be copied in unchanged; missing sections are
// created with the legacy defaults on construction. Since 2.1.0 its profile
// sections only serve as the migration source for saves that were last used
// with 2.0.x (ProfileSeed::SharedConfig) and are not written while a profile
// is attached.
//
// Reads are thread-safe (workers read weights); writes happen on the UI
// thread.
class AppConfig
{
public:
    explicit AppConfig(const QString &configFilePath);
    ~AppConfig();

    QString filePath() const { return m_filePath; }

    // Re-reads the file from disk (e.g. after the migration wizard replaced
    // it) and re-seeds any missing sections. An attached profile stays.
    void reload();

    // --- Profile of the open save --------------------------------------
    // Where a profile without stored values gets its first values from.
    enum class ProfileSeed {
        SharedConfig, // a save from 2.0.x: the settings all saves shared until then (config.ini)
        Defaults,     // a new save: the built-in defaults
    };

    // Attaches the profile stored in db (creating the profile tables and
    // seeding them first if the save has none yet) and loads all its values
    // in one query. From now on all profile settings belong to this save.
    // On failure nothing changes (a previously attached profile stays).
    bool attachProfile(Database &db, ProfileSeed seed, QString *errorOut = nullptr);
    // Back to the app-wide values (no save open).
    void detachProfile();
    bool hasProfile() const { return m_profileDb != nullptr; }
    // How the attached profile was first filled: "config.ini", "defaults" or
    // "copy:<save>"; empty without a profile.
    QString profileSeed() const;

    // Every profile value (keys "Section/key") of the attached profile, or of
    // config.ini without one.
    QHash<QString, QString> profileValues() const;
    // The profile values all saves shared up to 2.0.x (config.ini).
    QHash<QString, QString> sharedProfileValues() const;
    // Replaces the attached profile's values with these (one transaction).
    bool replaceProfileValues(const QHash<QString, QString> &values, const QString &seedLabel,
                              QString *errorOut = nullptr);

    // True for keys that belong to a save's profile ("Weights_fm27/...",
    // "ThemeSettings/...", ...), false for app-wide ones ("Database/...").
    static bool isProfileKey(const QString &fullKey);
    // Every profile key with its built-in default.
    static QHash<QString, QString> profileDefaults();

    // Groups setters into one atomic write (the settings page's Save
    // button): nothing reaches the disk until commit(), then everything in
    // one SQLite transaction (or one config.ini sync without a profile).
    // While active, getters already see the pending values. A Transaction
    // with a profile also opens a transaction on the profile's Database, so
    // settings written there in between (club, favorites) commit or roll
    // back together with it. Destroyed without commit() = rolled back.
    // Not nestable.
    class Transaction
    {
    public:
        explicit Transaction(AppConfig &config);
        ~Transaction();
        Transaction(const Transaction &) = delete;
        Transaction &operator=(const Transaction &) = delete;
        bool isActive() const { return m_active; }
        // false: nothing was written (memory and disk keep the old state).
        bool commit(QString *errorOut = nullptr);

    private:
        AppConfig &m_config;
        bool m_active = false;
    };

    // Error of the last failed write (setters return false then).
    QString lastError() const { return m_lastError; }

    // [Database]
    QString dbName() const;
    void setDbName(const QString &name);

    // [Startup] — open the last used save directly instead of showing the
    // start screen (default: off).
    bool autoOpenLastDatabase() const;
    void setAutoOpenLastDatabase(bool enabled);

    // [RecentDatabases] — when each save was last opened (for the start
    // screen). Unknown saves return an invalid QDateTime.
    QDateTime databaseLastOpened(const QString &name) const;
    void touchDatabaseLastOpened(const QString &name);
    void renameDatabaseEntry(const QString &oldName, const QString &newName);
    void removeDatabaseEntry(const QString &name);

    // [Weights] / [GKWeights] — category weights per FM version, category name
    // as in constants ("Extremely Important", "Top Importance", ...). FM24 uses
    // the legacy section names, later versions "Weights_<id>"/"GKWeights_<id>",
    // so tuning one game's weights never leaks into another.
    double weight(const QString &fmVersionId, const QString &category) const;
    bool setWeight(const QString &fmVersionId, const QString &category, double value);
    double gkWeight(const QString &fmVersionId, const QString &category) const;
    bool setGkWeight(const QString &fmVersionId, const QString &category, double value);
    static QString weightsSection(const QString &fmVersionId, bool goalkeeper);

    // [RoleMultipliers] — type "key" | "preferable".
    double roleMultiplier(const QString &type) const;
    bool setRoleMultiplier(const QString &type, double value);

    // [APTWeights] — APT display name ("Star Player", ...); "None"/empty -> default.
    double aptWeight(const QString &apt, double defaultValue = 1.0) const;
    bool setAptWeight(const QString &apt, double value);

    // [AgeThresholds] — playerType "outfielder" | "goalkeeper".
    int ageThreshold(const QString &playerType) const;
    bool setAgeThreshold(const QString &playerType, int value);

    // [SelectionBonuses] — key without "_multiplier" suffix, e.g. "natural_position".
    double selectionBonus(const QString &key) const;
    bool setSelectionBonus(const QString &key, double value);

    // [Phases] — share of the in-possession role in a two-role slot (FM26+),
    // percent 0..100, default 50.
    int ipSharePercent() const;
    bool setIpSharePercent(int percent);
    double bridgeTolerance() const;
    bool setBridgeTolerance(double value);

    // [SquadManagement]
    int squadManagementSetting(const QString &key) const;
    bool setSquadManagementSetting(const QString &key, int value);

    // [GapAnalysis]
    double gapAnalysisSetting(const QString &key) const;
    bool setGapAnalysisSetting(const QString &key, double value);

    // [Freshness] — "retirement_age" (X) and "stale_after_full_uploads" (Y).
    int freshnessSetting(const QString &key) const;
    bool setFreshnessSetting(const QString &key, int value);

    // [Training] — devFactor age windows: "<group>_peak_age" / "<group>_locked_age"
    // for groups "explosive", "strength", "technical".
    int trainingSetting(const QString &key) const;
    bool setTrainingSetting(const QString &key, int value);

    // [ThemeSettings] — full key/value map ("current_mode", "night_primary_color", ...).
    QHash<QString, QString> themeSettings() const;
    bool saveThemeSettings(const QHash<QString, QString> &settings);

    // [Import] — user-configured default folder for the FM-HTML-export file
    // dialogs. Empty = automatic default (see defaultHtmlExportDir).
    QString htmlExportDir() const;
    bool setHtmlExportDir(const QString &dir);
    // "<Documents>/Sports Interactive/<game folder>" of the given FM version
    // for the current user when that folder exists, otherwise the Documents
    // folder itself.
    static QString defaultHtmlExportDir(const QString &fmVersionId);
    // Configured folder if set and existing, otherwise defaultHtmlExportDir().
    QString effectiveHtmlExportDir(const QString &fmVersionId) const;

    // [Import] attribute_masking — FM27 save games: show only what the user's
    // scouting knows (ranges for partly known players), like the game's own
    // attribute masking. Per save, on by default. Off = real values of every
    // player; hidden values (ability, consistency, ...) are never imported.
    bool attributeMasking() const;
    bool setAttributeMasking(bool enabled);

    // [Import] live_save_* — follow an FM27 save while the app is open: the
    // newest file of the chosen career (autosaves and "(v02)" versions too) is
    // read in as soon as the game has written it. Per save, off by default.
    // last_mtime = modification time (ms since epoch) of the file read last.
    bool liveSaveEnabled() const;
    bool setLiveSaveEnabled(bool enabled);
    QString liveSavePath() const;
    bool setLiveSavePath(const QString &path);
    qint64 liveSaveLastModified() const;
    bool setLiveSaveLastModified(qint64 msecsSinceEpoch);

    // [Graphics] — player portraits and club logos from the FM graphics
    // folder (face/logo packs). Opt-in per save, off by default. Folder empty
    // = "<Documents>/Sports Interactive/<game folder>/graphics".
    bool graphicsEnabled() const;
    bool setGraphicsEnabled(bool enabled);
    QString graphicsDir() const;
    bool setGraphicsDir(const QString &dir);
    static QString defaultGraphicsDir(const QString &fmVersionId);
    QString effectiveGraphicsDir(const QString &fmVersionId) const;

private:
    void ensureDefaults();
    static QString iniKey(const QString &name); // "Star Player" -> "star_player"

    // Central access to every setting. Profile keys go to the attached
    // profile (or config.ini without one), app keys always to config.ini.
    QString readValue(const QString &key, const QString &fallback = QString()) const;
    bool writeValue(const QString &key, const QString &value);
    bool removeValue(const QString &key);
    // Applies pending writes (profile rows or config.ini).
    bool flush(const QHash<QString, QString> &upserts, const QSet<QString> &removals);
    void mirrorStartTheme(const QHash<QString, QString> &upserts);

    QString m_filePath;
    std::unique_ptr<QSettings> m_settings;

    // Attached profile: its values in memory (all of them, loaded at attach).
    mutable QReadWriteLock m_lock;
    Database *m_profileDb = nullptr;
    QHash<QString, QString> m_profileValues;

    // Open Transaction: pending writes (reads see them first).
    bool m_inTransaction = false;
    QHash<QString, QString> m_pendingUpserts;
    QSet<QString> m_pendingRemovals;

    QString m_lastError;
};

} // namespace fm
