#include "AppConfig.h"
#include <cmath>

#include "Constants.h"
#include "Database.h"
#include "ProfileStore.h"

#include <QDir>
#include <QFileInfo>
#include <QReadLocker>
#include <QWriteLocker>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace fm {

namespace {

// Version of the goalkeeper weight scheme the [GKWeights] values belong to.
constexpr int kGkWeightsScheme = 2;

const QHash<QString, double> &gapAnalysisDefaults()
{
    static const QHash<QString, double> defaults = {
        {QStringLiteral("displacement_threshold"), 8.0},
        {QStringLiteral("dropoff_threshold"), 8.0},
        {QStringLiteral("wrong_side_penalty"), 5.0},
    };
    return defaults;
}

const QHash<QString, int> &squadManagementDefaults()
{
    static const QHash<QString, int> defaults = {
        {QStringLiteral("max_roles_per_depth_player"), 2},
        {QStringLiteral("min_loan_talent_score"), 45},
        // Hard floor: no loan suggestion below this age (age >= value).
        {QStringLiteral("min_loan_age"), 17},
        // Youth-XI regulars are additionally proposed for loan once they are
        // strictly older than this (age > value), so they get real minutes
        // instead of stagnating in the youth setup.
        {QStringLiteral("youth_loan_over_age"), 18},
    };
    return defaults;
}

const QHash<QString, int> &freshnessDefaults()
{
    static const QHash<QString, int> defaults = {
        // Auto-retire age (X): player at/above this age who is stale and not in
        // the user's own club is treated as retired.
        {QStringLiteral("retirement_age"), 35},
        // Stale-after full exports (Y): a player missing from this many
        // consecutive full exports counts as stale (and, if old enough, retired).
        // Replaces "stale_after_uploads", which counted every import.
        {QStringLiteral("stale_after_full_uploads"), 2},
    };
    return defaults;
}

const QHash<QString, int> &trainingWindowDefaults()
{
    static const QHash<QString, int> defaults = {
        // devFactor = 1 up to *_peak_age, linear down to 0 at *_locked_age.
        // Explosive physical (Pace, Acceleration, Agility): the youth spike.
        {QStringLiteral("explosive_peak_age"), 21},
        {QStringLiteral("explosive_locked_age"), 24},
        // Strength/stamina physical: matures a little later.
        {QStringLiteral("strength_peak_age"), 23},
        {QStringLiteral("strength_locked_age"), 25},
        // Technical: trainable young, tapering off toward the mid-20s.
        {QStringLiteral("technical_peak_age"), 21},
        {QStringLiteral("technical_locked_age"), 27},
    };
    return defaults;
}

const QHash<QString, QString> &themeDefaults()
{
    // Club-neutral, professional dark/light base (seven color roles per mode).
    // Club presets override primary/interactive/header on top of this.
    static const QHash<QString, QString> defaults = {
        {QStringLiteral("current_mode"), QStringLiteral("night")},
        {QStringLiteral("theme_preset"), QStringLiteral("neutral")},
        // Club header: "gradient" (club colors + crest) or "solid" (header color).
        {QStringLiteral("header_style"), QStringLiteral("gradient")},
        {QStringLiteral("header_crest_watermark"), QStringLiteral("true")},
        // --- Night ---
        {QStringLiteral("night_background_color"), QStringLiteral("#262626")},
        {QStringLiteral("night_surface_color"), QStringLiteral("#343434")},
        {QStringLiteral("night_sidebar_color"), QStringLiteral("#1f1f1f")},
        {QStringLiteral("night_header_color"), QStringLiteral("#2d2d2d")},
        {QStringLiteral("night_primary_color"), QStringLiteral("#6a1bf5")},
        {QStringLiteral("night_interactive_color"), QStringLiteral("#9d6bff")},
        {QStringLiteral("night_text_color"), QStringLiteral("#F5F5F5")},
        // --- Day ---
        {QStringLiteral("day_background_color"), QStringLiteral("#F0F2F6")},
        {QStringLiteral("day_surface_color"), QStringLiteral("#FFFFFF")},
        {QStringLiteral("day_sidebar_color"), QStringLiteral("#E7E9ED")},
        {QStringLiteral("day_header_color"), QStringLiteral("#FFFFFF")},
        {QStringLiteral("day_primary_color"), QStringLiteral("#6a1bf5")},
        {QStringLiteral("day_interactive_color"), QStringLiteral("#9d6bff")},
        {QStringLiteral("day_text_color"), QStringLiteral("#31333F")},
    };
    return defaults;
}

// Sections of config.ini that belong to a save's profile (see AppConfig.h).
// Weights/GKWeights carry a "_<version>" suffix for every FM version after 24.
bool isProfileSection(const QString &section)
{
    static const QSet<QString> sections = {
        QStringLiteral("Weights"),          QStringLiteral("GKWeights"),
        QStringLiteral("RoleMultipliers"),  QStringLiteral("APTWeights"),
        QStringLiteral("AgeThresholds"),    QStringLiteral("SelectionBonuses"),
        QStringLiteral("Phases"),           QStringLiteral("SquadManagement"),
        QStringLiteral("GapAnalysis"),      QStringLiteral("Freshness"),
        QStringLiteral("Training"),         QStringLiteral("ThemeSettings"),
        QStringLiteral("Import"),           QStringLiteral("Profile"),
        QStringLiteral("Graphics"),
    };
    if (sections.contains(section))
        return true;
    return section.startsWith(QLatin1String("Weights_"))
           || section.startsWith(QLatin1String("GKWeights_"));
}

const QString kThemeSection = QStringLiteral("ThemeSettings/");
const QString kStartThemeSection = QStringLiteral("StartTheme/");

} // namespace

AppConfig::AppConfig(const QString &configFilePath)
    : m_filePath(configFilePath)
{
    QDir().mkpath(QFileInfo(configFilePath).absolutePath());
    m_settings = std::make_unique<QSettings>(configFilePath, QSettings::IniFormat);
    ensureDefaults();
}

AppConfig::~AppConfig() = default;

void AppConfig::reload()
{
    m_settings = std::make_unique<QSettings>(m_filePath, QSettings::IniFormat);
    ensureDefaults();
}

QString AppConfig::iniKey(const QString &name)
{
    return name.toLower().replace(QLatin1Char(' '), QLatin1Char('_'));
}

bool AppConfig::isProfileKey(const QString &fullKey)
{
    const int slash = fullKey.indexOf(QLatin1Char('/'));
    return slash > 0 && isProfileSection(fullKey.left(slash));
}

QHash<QString, QString> AppConfig::profileDefaults()
{
    QHash<QString, QString> values;
    const auto put = [&values](const QString &key, const QString &value) {
        values.insert(key, value);
    };

    for (const FmDataVersion &version : fmDataVersions()) {
        if (!version.supported)
            continue;
        const QString fieldSection = weightsSection(version.id, false) + QLatin1Char('/');
        const auto &weights = weightDefaults(version.id);
        for (auto it = weights.constBegin(); it != weights.constEnd(); ++it)
            put(fieldSection + iniKey(it.key()), QString::number(it.value()));

        const QString gkSection = weightsSection(version.id, true) + QLatin1Char('/');
        const auto &gkWeights = gkWeightDefaults(version.id);
        for (auto it = gkWeights.constBegin(); it != gkWeights.constEnd(); ++it)
            put(gkSection + iniKey(it.key()), QString::number(it.value()));
    }

    put(QStringLiteral("RoleMultipliers/key_multiplier"), QStringLiteral("1.5"));
    put(QStringLiteral("RoleMultipliers/preferable_multiplier"), QStringLiteral("1.2"));

    QSet<QString> allApt;
    for (const QString &apt : fieldPlayerAptOptions())
        allApt.insert(apt);
    for (const QString &apt : gkAptOptions())
        allApt.insert(apt);
    for (const QString &apt : allApt) {
        if (apt != QLatin1String("None"))
            put(QStringLiteral("APTWeights/") + iniKey(apt), QStringLiteral("1.0"));
    }

    put(QStringLiteral("AgeThresholds/outfielder_youth_age"), QStringLiteral("20"));
    put(QStringLiteral("AgeThresholds/goalkeeper_youth_age"), QStringLiteral("25"));

    put(QStringLiteral("SelectionBonuses/natural_position_multiplier"), QStringLiteral("1.00"));
    put(QStringLiteral("Phases/ip_share_percent"), QStringLiteral("50"));
    // Portraits/logos from the FM graphics folder: opt-in, off by default.
    put(QStringLiteral("Graphics/enabled"), QStringLiteral("false"));

    const auto &squad = squadManagementDefaults();
    for (auto it = squad.constBegin(); it != squad.constEnd(); ++it)
        put(QStringLiteral("SquadManagement/") + it.key(), QString::number(it.value()));

    const auto &gap = gapAnalysisDefaults();
    for (auto it = gap.constBegin(); it != gap.constEnd(); ++it)
        put(QStringLiteral("GapAnalysis/") + it.key(), QString::number(it.value()));

    const auto &freshness = freshnessDefaults();
    for (auto it = freshness.constBegin(); it != freshness.constEnd(); ++it)
        put(QStringLiteral("Freshness/") + it.key(), QString::number(it.value()));

    const auto &training = trainingWindowDefaults();
    for (auto it = training.constBegin(); it != training.constEnd(); ++it)
        put(QStringLiteral("Training/") + it.key(), QString::number(it.value()));

    const auto &theme = themeDefaults();
    for (auto it = theme.constBegin(); it != theme.constEnd(); ++it)
        put(kThemeSection + it.key(), it.value());
    return values;
}

void AppConfig::ensureDefaults()
{
    QSettings &s = *m_settings;
    bool modified = false;

    const auto ensure = [&](const QString &fullKey, const QVariant &value) {
        if (!s.contains(fullKey)) {
            s.setValue(fullKey, value);
            modified = true;
        }
    };

    ensure(QStringLiteral("Database/db_name"), QStringLiteral("default"));

    // Goalkeeper scheme 2 (v1.4.13): FM24 goalkeeper weights tuned for the old
    // formula — six attributes, Key/Preferable/Other without effect — do not
    // fit the new tiers, so they are reset once to the new defaults. (Runs
    // before seeding, so a missing section gets the new values right away.)
    if (s.value(QStringLiteral("Migrations/gk_weights_scheme"), 1).toInt() < kGkWeightsScheme) {
        const QString fm24 = defaultFmVersionId();
        const QString gkSection = weightsSection(fm24, true) + QLatin1Char('/');
        const auto &gkWeights = gkWeightDefaults(fm24);
        for (auto it = gkWeights.constBegin(); it != gkWeights.constEnd(); ++it)
            s.setValue(gkSection + iniKey(it.key()), QString::number(it.value()));
        s.setValue(QStringLiteral("Migrations/gk_weights_scheme"), kGkWeightsScheme);
        modified = true;
    }

    const QHash<QString, QString> defaults = profileDefaults();
    for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it)
        ensure(it.key(), it.value());

    // Old threshold counted every import, partial exports included (v1.4.5).
    if (s.contains(QStringLiteral("Freshness/stale_after_uploads"))) {
        s.remove(QStringLiteral("Freshness/stale_after_uploads"));
        modified = true;
    }

    // v1.7: the neutral (club-free) look moved from blue to the logo purple.
    // Only untouched blue defaults follow; club themes and custom colors stay.
    const auto &theme = themeDefaults();
    if (s.value(QStringLiteral("ThemeSettings/theme_preset")).toString()
            == QLatin1String("neutral")
        && s.value(QStringLiteral("ThemeSettings/night_primary_color")).toString().toLower()
               == QLatin1String("#3d7dff")) {
        for (const QString &key :
             {QStringLiteral("night_primary_color"), QStringLiteral("night_interactive_color"),
              QStringLiteral("day_primary_color"), QStringLiteral("day_interactive_color")}) {
            s.setValue(kThemeSection + key, theme.value(key));
        }
        modified = true;
    }

    if (modified)
        s.sync();
}

// --- Central value access --------------------------------------------------

QString AppConfig::readValue(const QString &key, const QString &fallback) const
{
    const bool profileKey = isProfileKey(key);
    {
        QReadLocker lock(&m_lock);
        if (m_inTransaction) {
            if (m_pendingRemovals.contains(key))
                return fallback;
            const auto pending = m_pendingUpserts.constFind(key);
            if (pending != m_pendingUpserts.constEnd())
                return pending.value();
        }
        if (profileKey && m_profileDb)
            return m_profileValues.value(key, fallback);
    }
    // Without a profile the start screen's own look wins over the shared
    // (2.0.x) theme, see writeValue().
    if (key.startsWith(kThemeSection)) {
        const QString startKey = kStartThemeSection + key.mid(kThemeSection.size());
        if (m_settings->contains(startKey))
            return m_settings->value(startKey).toString();
    }
    return m_settings->value(key, fallback).toString();
}

bool AppConfig::writeValue(const QString &key, const QString &value)
{
    if (m_inTransaction) {
        QWriteLocker lock(&m_lock);
        m_pendingRemovals.remove(key);
        m_pendingUpserts.insert(key, value);
        return true;
    }
    if (!flush({{key, value}}, {}))
        return false;
    mirrorStartTheme({{key, value}});
    return true;
}

bool AppConfig::removeValue(const QString &key)
{
    if (m_inTransaction) {
        QWriteLocker lock(&m_lock);
        m_pendingUpserts.remove(key);
        m_pendingRemovals.insert(key);
        return true;
    }
    return flush({}, {key});
}

bool AppConfig::flush(const QHash<QString, QString> &upserts, const QSet<QString> &removals)
{
    QHash<QString, QString> profileUpserts;
    QStringList profileRemovals;
    QHash<QString, QString> fileUpserts;
    QStringList fileRemovals;
    {
        QReadLocker lock(&m_lock);
        for (auto it = upserts.constBegin(); it != upserts.constEnd(); ++it) {
            if (m_profileDb && isProfileKey(it.key())) {
                // Unchanged values are not written again.
                const auto current = m_profileValues.constFind(it.key());
                if (current == m_profileValues.constEnd() || current.value() != it.value())
                    profileUpserts.insert(it.key(), it.value());
            } else {
                fileUpserts.insert(it.key(), it.value());
            }
        }
        for (const QString &key : removals) {
            if (m_profileDb && isProfileKey(key)) {
                if (m_profileValues.contains(key))
                    profileRemovals << key;
            } else {
                fileRemovals << key;
            }
        }
    }

    if (!profileUpserts.isEmpty() || !profileRemovals.isEmpty()) {
        ProfileStore store(*m_profileDb);
        if (!store.writeSettings(profileUpserts, profileRemovals)) {
            m_lastError = store.errorString();
            return false;
        }
        QWriteLocker lock(&m_lock);
        for (auto it = profileUpserts.constBegin(); it != profileUpserts.constEnd(); ++it)
            m_profileValues.insert(it.key(), it.value());
        for (const QString &key : profileRemovals)
            m_profileValues.remove(key);
    }

    if (!fileUpserts.isEmpty() || !fileRemovals.isEmpty()) {
        for (auto it = fileUpserts.constBegin(); it != fileUpserts.constEnd(); ++it) {
            // Without a profile, theme changes belong to the start screen;
            // [ThemeSettings] stays the untouched 2.0.x migration source.
            if (it.key().startsWith(kThemeSection))
                m_settings->setValue(kStartThemeSection + it.key().mid(kThemeSection.size()),
                                     it.value());
            else
                m_settings->setValue(it.key(), it.value());
        }
        for (const QString &key : fileRemovals)
            m_settings->remove(key);
        m_settings->sync();
        if (m_settings->status() != QSettings::NoError) {
            m_lastError = QStringLiteral("config.ini could not be written: %1").arg(m_filePath);
            return false;
        }
    }
    return true;
}

void AppConfig::mirrorStartTheme(const QHash<QString, QString> &upserts)
{
    if (!m_profileDb)
        return; // without a profile writeValue already wrote [StartTheme]
    bool changed = false;
    for (auto it = upserts.constBegin(); it != upserts.constEnd(); ++it) {
        if (!it.key().startsWith(kThemeSection))
            continue;
        const QString startKey = kStartThemeSection + it.key().mid(kThemeSection.size());
        if (m_settings->value(startKey).toString() != it.value()) {
            m_settings->setValue(startKey, it.value());
            changed = true;
        }
    }
    if (changed)
        m_settings->sync();
}

// --- Profile ----------------------------------------------------------------

bool AppConfig::attachProfile(Database &db, ProfileSeed seed, QString *errorOut)
{
    const auto fail = [&](const QString &error) {
        m_lastError = error;
        if (errorOut)
            *errorOut = error;
        return false;
    };
    if (m_inTransaction)
        return fail(QStringLiteral("attachProfile() during an open AppConfig::Transaction"));

    ProfileStore store(db);
    if (!store.ensureTables())
        return fail(store.errorString());
    if (!store.isInitialized()) {
        QHash<QString, QString> values;
        QString label;
        if (seed == ProfileSeed::SharedConfig) {
            // Saves from 2.0.x keep exactly what they used until now.
            values = sharedProfileValues();
            label = QStringLiteral("config.ini");
        } else {
            values = profileDefaults();
            label = QStringLiteral("defaults");
        }
        values.insert(ProfileStore::formatKey(), QString::number(ProfileStore::kFormatVersion));
        values.insert(ProfileStore::seedKey(), label);
        values.insert(ProfileStore::createdKey(),
                      QDateTime::currentDateTime().toString(Qt::ISODate));
        if (!store.writeSettings(values))
            return fail(store.errorString());
    }

    QHash<QString, QString> loaded;
    if (!store.loadSettings(loaded))
        return fail(store.errorString());
    {
        QWriteLocker lock(&m_lock);
        m_profileDb = &db;
        m_profileValues = std::move(loaded);
    }
    QHash<QString, QString> theme;
    {
        QReadLocker lock(&m_lock);
        for (auto it = m_profileValues.constBegin(); it != m_profileValues.constEnd(); ++it) {
            if (it.key().startsWith(kThemeSection))
                theme.insert(it.key(), it.value());
        }
    }
    mirrorStartTheme(theme);
    return true;
}

void AppConfig::detachProfile()
{
    QWriteLocker lock(&m_lock);
    m_profileDb = nullptr;
    m_profileValues.clear();
}

QString AppConfig::profileSeed() const
{
    QReadLocker lock(&m_lock);
    return m_profileDb ? m_profileValues.value(ProfileStore::seedKey()) : QString();
}

QHash<QString, QString> AppConfig::profileValues() const
{
    QHash<QString, QString> values;
    QReadLocker lock(&m_lock);
    if (m_profileDb) {
        for (auto it = m_profileValues.constBegin(); it != m_profileValues.constEnd(); ++it) {
            if (!it.key().startsWith(QLatin1String("Profile/")))
                values.insert(it.key(), it.value());
        }
        return values;
    }
    lock.unlock();
    return sharedProfileValues();
}

QHash<QString, QString> AppConfig::sharedProfileValues() const
{
    QHash<QString, QString> values;
    const QStringList keys = m_settings->allKeys();
    for (const QString &key : keys) {
        if (isProfileKey(key) && !key.startsWith(QLatin1String("Profile/")))
            values.insert(key, m_settings->value(key).toString());
    }
    return values;
}

bool AppConfig::replaceProfileValues(const QHash<QString, QString> &values,
                                     const QString &seedLabel, QString *errorOut)
{
    const auto fail = [&](const QString &error) {
        m_lastError = error;
        if (errorOut)
            *errorOut = error;
        return false;
    };
    if (!m_profileDb || m_inTransaction)
        return fail(QStringLiteral("replaceProfileValues() needs an attached profile and no "
                                   "open transaction"));
    QHash<QString, QString> upserts;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (isProfileKey(it.key()) && !it.key().startsWith(QLatin1String("Profile/")))
            upserts.insert(it.key(), it.value());
    }
    QStringList removals;
    {
        QReadLocker lock(&m_lock);
        for (auto it = m_profileValues.constBegin(); it != m_profileValues.constEnd(); ++it) {
            if (!it.key().startsWith(QLatin1String("Profile/")) && !upserts.contains(it.key()))
                removals << it.key();
        }
    }
    upserts.insert(ProfileStore::seedKey(), seedLabel);
    ProfileStore store(*m_profileDb);
    if (!store.writeSettings(upserts, removals))
        return fail(store.errorString());
    {
        QWriteLocker lock(&m_lock);
        for (auto it = upserts.constBegin(); it != upserts.constEnd(); ++it)
            m_profileValues.insert(it.key(), it.value());
        for (const QString &key : removals)
            m_profileValues.remove(key);
    }
    mirrorStartTheme(upserts);
    return true;
}

// --- Transaction ------------------------------------------------------------

AppConfig::Transaction::Transaction(AppConfig &config)
    : m_config(config)
{
    if (config.m_inTransaction)
        return; // not nestable: stays inactive, commit() fails
    if (config.m_profileDb && !config.m_profileDb->beginTransaction()) {
        config.m_lastError = config.m_profileDb->errorString();
        return;
    }
    QWriteLocker lock(&config.m_lock);
    config.m_inTransaction = true;
    config.m_pendingUpserts.clear();
    config.m_pendingRemovals.clear();
    m_active = true;
}

AppConfig::Transaction::~Transaction()
{
    if (!m_active)
        return;
    {
        QWriteLocker lock(&m_config.m_lock);
        m_config.m_inTransaction = false;
        m_config.m_pendingUpserts.clear();
        m_config.m_pendingRemovals.clear();
    }
    if (m_config.m_profileDb)
        m_config.m_profileDb->rollbackTransaction();
}

bool AppConfig::Transaction::commit(QString *errorOut)
{
    if (!m_active) {
        if (errorOut)
            *errorOut = m_config.m_lastError.isEmpty()
                            ? QStringLiteral("No active settings transaction.")
                            : m_config.m_lastError;
        return false;
    }
    m_active = false;
    QHash<QString, QString> upserts;
    QSet<QString> removals;
    QHash<QString, QString> before;
    {
        QWriteLocker lock(&m_config.m_lock);
        m_config.m_inTransaction = false;
        upserts.swap(m_config.m_pendingUpserts);
        removals.swap(m_config.m_pendingRemovals);
        before = m_config.m_profileValues;
    }
    Database *db = m_config.m_profileDb;
    bool ok = m_config.flush(upserts, removals);
    if (db) {
        if (!ok) {
            db->rollbackTransaction();
        } else if (!db->commitTransaction()) {
            // A failed COMMIT is already rolled back by Database.
            m_config.m_lastError = db->errorString();
            ok = false;
        }
        if (!ok) {
            QWriteLocker lock(&m_config.m_lock);
            m_config.m_profileValues = before; // memory matches the disk again
        }
    }
    if (!ok) {
        if (errorOut)
            *errorOut = m_config.m_lastError;
        return false;
    }
    m_config.mirrorStartTheme(upserts);
    return true;
}

QString AppConfig::dbName() const
{
    return m_settings->value(QStringLiteral("Database/db_name"), QStringLiteral("default"))
        .toString();
}

void AppConfig::setDbName(const QString &name)
{
    m_settings->setValue(QStringLiteral("Database/db_name"), name);
    m_settings->sync();
}

bool AppConfig::autoOpenLastDatabase() const
{
    return m_settings->value(QStringLiteral("Startup/auto_open_last"), false).toBool();
}

void AppConfig::setAutoOpenLastDatabase(bool enabled)
{
    m_settings->setValue(QStringLiteral("Startup/auto_open_last"), enabled);
    m_settings->sync();
}

bool AppConfig::saveTipDismissed() const
{
    return m_settings->value(QStringLiteral("Hints/save_tip_dismissed"), false).toBool();
}

void AppConfig::setSaveTipDismissed(bool dismissed)
{
    m_settings->setValue(QStringLiteral("Hints/save_tip_dismissed"), dismissed);
    m_settings->sync();
}

namespace {
// Save names may contain spaces and umlauts; percent-encoding keeps them valid
// and unambiguous as INI keys on every platform.
QString recentKey(const QString &name)
{
    return QStringLiteral("RecentDatabases/")
        + QString::fromLatin1(QUrl::toPercentEncoding(name));
}
} // namespace

QDateTime AppConfig::databaseLastOpened(const QString &name) const
{
    return QDateTime::fromString(m_settings->value(recentKey(name)).toString(), Qt::ISODate);
}

void AppConfig::touchDatabaseLastOpened(const QString &name)
{
    m_settings->setValue(recentKey(name),
                         QDateTime::currentDateTime().toString(Qt::ISODate));
    m_settings->sync();
}

void AppConfig::renameDatabaseEntry(const QString &oldName, const QString &newName)
{
    const QVariant value = m_settings->value(recentKey(oldName));
    m_settings->remove(recentKey(oldName));
    if (value.isValid())
        m_settings->setValue(recentKey(newName), value);
    if (dbName() == oldName)
        m_settings->setValue(QStringLiteral("Database/db_name"), newName);
    m_settings->sync();
}

void AppConfig::removeDatabaseEntry(const QString &name)
{
    m_settings->remove(recentKey(name));
    m_settings->sync();
}

QString AppConfig::weightsSection(const QString &fmVersionId, bool goalkeeper)
{
    const QString base = goalkeeper ? QStringLiteral("GKWeights") : QStringLiteral("Weights");
    const QString id = fmDataVersion(fmVersionId).id;
    return id == defaultFmVersionId() ? base : base + QLatin1Char('_') + id;
}

double AppConfig::weight(const QString &fmVersionId, const QString &category) const
{
    const double fallback = weightDefaults(fmVersionId).value(category, 1.0);
    return readValue(weightsSection(fmVersionId, false) + QLatin1Char('/') + iniKey(category),
                     QString::number(fallback))
        .toDouble();
}

bool AppConfig::setWeight(const QString &fmVersionId, const QString &category, double value)
{
    return writeValue(weightsSection(fmVersionId, false) + QLatin1Char('/') + iniKey(category),
                      QString::number(value));
}

double AppConfig::gkWeight(const QString &fmVersionId, const QString &category) const
{
    const double fallback = gkWeightDefaults(fmVersionId).value(category, 1.0);
    return readValue(weightsSection(fmVersionId, true) + QLatin1Char('/') + iniKey(category),
                     QString::number(fallback))
        .toDouble();
}

bool AppConfig::setGkWeight(const QString &fmVersionId, const QString &category, double value)
{
    return writeValue(weightsSection(fmVersionId, true) + QLatin1Char('/') + iniKey(category),
                      QString::number(value));
}

double AppConfig::roleMultiplier(const QString &type) const
{
    const double fallback = type == QLatin1String("key") ? kKeyMultiplierDefault
                                                         : kPreferableMultiplierDefault;
    return readValue(QStringLiteral("RoleMultipliers/") + type + QStringLiteral("_multiplier"),
                     QString::number(fallback))
        .toDouble();
}

bool AppConfig::setRoleMultiplier(const QString &type, double value)
{
    return writeValue(QStringLiteral("RoleMultipliers/") + type + QStringLiteral("_multiplier"),
                      QString::number(value));
}

double AppConfig::aptWeight(const QString &apt, double defaultValue) const
{
    if (apt.isEmpty() || apt == QLatin1String("None"))
        return defaultValue;
    return readValue(QStringLiteral("APTWeights/") + iniKey(apt), QString::number(defaultValue))
        .toDouble();
}

bool AppConfig::setAptWeight(const QString &apt, double value)
{
    if (apt.isEmpty() || apt == QLatin1String("None"))
        return true;
    return writeValue(QStringLiteral("APTWeights/") + iniKey(apt), QString::number(value));
}

int AppConfig::ageThreshold(const QString &playerType) const
{
    const int fallback = playerType == QLatin1String("goalkeeper") ? 25 : 20;
    return readValue(QStringLiteral("AgeThresholds/") + playerType + QStringLiteral("_youth_age"),
                     QString::number(fallback))
        .toInt();
}

bool AppConfig::setAgeThreshold(const QString &playerType, int value)
{
    return writeValue(QStringLiteral("AgeThresholds/") + playerType
                          + QStringLiteral("_youth_age"),
                      QString::number(value));
}

int AppConfig::ipSharePercent() const
{
    return std::clamp(readValue(QStringLiteral("Phases/ip_share_percent"), QStringLiteral("50"))
                          .toInt(),
                      0, 100);
}

bool AppConfig::setIpSharePercent(int percent)
{
    return writeValue(QStringLiteral("Phases/ip_share_percent"),
                      QString::number(std::clamp(percent, 0, 100)));
}

double AppConfig::bridgeTolerance() const
{
    const double value = readValue(QStringLiteral("Phases/bridge_tolerance"),
                                   QStringLiteral("0.1")).toDouble();
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.1;
}

bool AppConfig::setBridgeTolerance(double value)
{
    if (!std::isfinite(value))
        return false;
    return writeValue(QStringLiteral("Phases/bridge_tolerance"),
                      QString::number(std::clamp(value, 0.0, 1.0)));
}

double AppConfig::selectionBonus(const QString &key) const
{
    // Note: legacy get_selection_bonus falls back to 1.05 for
    // "natural_position" but load_config seeds the file with 1.00, so the
    // seeded value is what takes effect in practice.
    const double fallback = key == QLatin1String("natural_position") ? 1.05 : 1.0;
    return readValue(QStringLiteral("SelectionBonuses/") + key + QStringLiteral("_multiplier"),
                     QString::number(fallback))
        .toDouble();
}

bool AppConfig::setSelectionBonus(const QString &key, double value)
{
    return writeValue(QStringLiteral("SelectionBonuses/") + key + QStringLiteral("_multiplier"),
                      QString::number(value));
}

int AppConfig::squadManagementSetting(const QString &key) const
{
    const int fallback = squadManagementDefaults().value(key, 0);
    return readValue(QStringLiteral("SquadManagement/") + key, QString::number(fallback)).toInt();
}

bool AppConfig::setSquadManagementSetting(const QString &key, int value)
{
    return writeValue(QStringLiteral("SquadManagement/") + key, QString::number(value));
}

double AppConfig::gapAnalysisSetting(const QString &key) const
{
    const double fallback = gapAnalysisDefaults().value(key, 0.0);
    return readValue(QStringLiteral("GapAnalysis/") + key, QString::number(fallback)).toDouble();
}

bool AppConfig::setGapAnalysisSetting(const QString &key, double value)
{
    return writeValue(QStringLiteral("GapAnalysis/") + key, QString::number(value));
}

int AppConfig::freshnessSetting(const QString &key) const
{
    const int fallback = freshnessDefaults().value(key, 0);
    return readValue(QStringLiteral("Freshness/") + key, QString::number(fallback)).toInt();
}

bool AppConfig::setFreshnessSetting(const QString &key, int value)
{
    return writeValue(QStringLiteral("Freshness/") + key, QString::number(value));
}

int AppConfig::trainingSetting(const QString &key) const
{
    const int fallback = trainingWindowDefaults().value(key, 0);
    return readValue(QStringLiteral("Training/") + key, QString::number(fallback)).toInt();
}

bool AppConfig::setTrainingSetting(const QString &key, int value)
{
    return writeValue(QStringLiteral("Training/") + key, QString::number(value));
}

QHash<QString, QString> AppConfig::themeSettings() const
{
    QHash<QString, QString> result = themeDefaults();
    // Every key the profile/file knows, plus the defaults' keys: readValue()
    // resolves pending > profile > [StartTheme] > [ThemeSettings].
    QSet<QString> keys(result.keyBegin(), result.keyEnd());
    {
        QReadLocker lock(&m_lock);
        if (m_profileDb) {
            for (auto it = m_profileValues.constBegin(); it != m_profileValues.constEnd(); ++it) {
                if (it.key().startsWith(kThemeSection))
                    keys.insert(it.key().mid(kThemeSection.size()));
            }
        }
        for (auto it = m_pendingUpserts.constBegin(); it != m_pendingUpserts.constEnd(); ++it) {
            if (it.key().startsWith(kThemeSection))
                keys.insert(it.key().mid(kThemeSection.size()));
        }
    }
    if (!hasProfile()) {
        for (const QString &group : {QStringLiteral("ThemeSettings"), QStringLiteral("StartTheme")}) {
            m_settings->beginGroup(group);
            const QStringList childKeys = m_settings->childKeys();
            m_settings->endGroup();
            for (const QString &key : childKeys)
                keys.insert(key);
        }
    }
    for (const QString &key : keys) {
        const QString value = readValue(kThemeSection + key, result.value(key));
        result.insert(key, value);
    }
    return result;
}

bool AppConfig::saveThemeSettings(const QHash<QString, QString> &settings)
{
    if (m_inTransaction) {
        for (auto it = settings.constBegin(); it != settings.constEnd(); ++it)
            writeValue(kThemeSection + it.key(), it.value());
        return true;
    }
    // Only what actually changed is written (a mode toggle = one row).
    QHash<QString, QString> upserts;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it)
        upserts.insert(kThemeSection + it.key(), it.value());
    if (!flush(upserts, {}))
        return false;
    mirrorStartTheme(upserts);
    return true;
}

QString AppConfig::htmlExportDir() const
{
    return readValue(QStringLiteral("Import/html_export_dir"));
}

bool AppConfig::setHtmlExportDir(const QString &dir)
{
    // Empty means "automatic default"; keep the store clean in that case so the
    // default stays dynamic (it depends on the current user's Documents dir).
    if (dir.isEmpty())
        return removeValue(QStringLiteral("Import/html_export_dir"));
    return writeValue(QStringLiteral("Import/html_export_dir"), dir);
}

QString AppConfig::defaultHtmlExportDir(const QString &fmVersionId)
{
    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString fmDir = documents + QStringLiteral("/Sports Interactive/")
                          + fmDataVersion(fmVersionId).exportFolderName;
    return QDir(fmDir).exists() ? fmDir : documents;
}

QString AppConfig::effectiveHtmlExportDir(const QString &fmVersionId) const
{
    const QString configured = htmlExportDir();
    if (!configured.isEmpty() && QDir(configured).exists())
        return configured;
    return defaultHtmlExportDir(fmVersionId);
}

bool AppConfig::attributeMasking() const
{
    return readValue(QStringLiteral("Import/attribute_masking"), QStringLiteral("true"))
           != QLatin1String("false");
}

bool AppConfig::setAttributeMasking(bool enabled)
{
    return writeValue(QStringLiteral("Import/attribute_masking"),
                      enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

bool AppConfig::liveSaveEnabled() const
{
    return readValue(QStringLiteral("Import/live_save_enabled"), QStringLiteral("false"))
           == QLatin1String("true");
}

bool AppConfig::setLiveSaveEnabled(bool enabled)
{
    return writeValue(QStringLiteral("Import/live_save_enabled"),
                      enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

QString AppConfig::liveSavePath() const
{
    return readValue(QStringLiteral("Import/live_save_path"));
}

bool AppConfig::setLiveSavePath(const QString &path)
{
    return writeValue(QStringLiteral("Import/live_save_path"), path);
}

qint64 AppConfig::liveSaveLastModified() const
{
    return readValue(QStringLiteral("Import/live_save_last_mtime"), QStringLiteral("0")).toLongLong();
}

bool AppConfig::setLiveSaveLastModified(qint64 msecsSinceEpoch)
{
    return writeValue(QStringLiteral("Import/live_save_last_mtime"),
                      QString::number(msecsSinceEpoch));
}

bool AppConfig::graphicsEnabled() const
{
    return readValue(QStringLiteral("Graphics/enabled"), QStringLiteral("false"))
           == QLatin1String("true");
}

bool AppConfig::setGraphicsEnabled(bool enabled)
{
    return writeValue(QStringLiteral("Graphics/enabled"),
                      enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

QString AppConfig::graphicsDir() const
{
    return readValue(QStringLiteral("Graphics/folder"));
}

bool AppConfig::setGraphicsDir(const QString &dir)
{
    if (dir.isEmpty())
        return removeValue(QStringLiteral("Graphics/folder"));
    return writeValue(QStringLiteral("Graphics/folder"), dir);
}

QString AppConfig::defaultGraphicsDir(const QString &fmVersionId)
{
    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    // The game's own folder (not the export folder: FM27 exports go to the
    // FM View Exporter's folder). Up to FM24 the folder carries the full
    // year, from FM26 on the short number ("Football Manager 27").
    QString game = QStringLiteral("Football Manager ");
    const QString number = fmVersionId.mid(2);
    game += number.toInt() <= 24 ? QStringLiteral("20") + number : number;
    return documents + QStringLiteral("/Sports Interactive/") + game + QStringLiteral("/graphics");
}

QString AppConfig::defaultSavesDir(const QString &fmVersionId)
{
    return QFileInfo(defaultGraphicsDir(fmVersionId)).absolutePath() + QStringLiteral("/games");
}

bool AppConfig::followSave(const QString &savePath, qint64 alreadyReadMs)
{
    Transaction transaction(*this);
    bool ok = transaction.isActive() && setLiveSavePath(savePath)
              && setLiveSaveLastModified(alreadyReadMs) && setLiveSaveEnabled(!savePath.isEmpty());
    QString error;
    return ok && transaction.commit(&error);
}

QString AppConfig::effectiveGraphicsDir(const QString &fmVersionId) const
{
    const QString configured = graphicsDir();
    return configured.isEmpty() ? defaultGraphicsDir(fmVersionId) : configured;
}

} // namespace fm
