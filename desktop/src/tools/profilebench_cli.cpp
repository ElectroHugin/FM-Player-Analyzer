// Measurements for the save profiles (2.1.0): size and timing of opening,
// saving settings, switching saves and large imports, with a small and a
// large synthetic save. Works only in a temporary directory; never touches a
// real data folder.
//
//   fmprofilebench <definitions.json> [--players-small N] [--players-large N]
//                  [--repeat N]
//
// The "shared" rows reproduce the 2.0.4 storage (config.ini + definitions
// file for every save), the "profile" rows the per-save profile inside the
// .db file. Results are printed as a Markdown table (desktop/PROFILE.md).

#include "core/AppConfig.h"
#include "core/Constants.h"
#include "core/Database.h"
#include "core/Definitions.h"
#include "core/PlayerStore.h"
#include "core/ProfileStore.h"
#include "core/RatingsUpdater.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace fm;

namespace {

int g_repeat = 3;

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

template <typename F>
double timeMs(F &&f)
{
    std::vector<double> runs;
    for (int i = 0; i < g_repeat; ++i) {
        QElapsedTimer t;
        t.start();
        f();
        runs.push_back(t.nsecsElapsed() / 1e6);
    }
    return median(runs);
}

qint64 fileBytes(const QString &path)
{
    qint64 total = 0;
    for (const QString &p : {path, path + QStringLiteral("-wal")})
        total += QFileInfo(p).size();
    return total;
}

void checkpoint(Database &db)
{
    QSqlQuery(db.handle()).exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
}

std::vector<Player> makePlayers(int count, const QStringList &roles, quint32 seed)
{
    QRandomGenerator rng(seed);
    std::vector<Player> players;
    players.reserve(count);
    for (int i = 0; i < count; ++i) {
        Player p;
        p.uid = QString::number(100000 + i);
        p.name = QStringLiteral("Spieler %1").arg(i);
        p.age = 16 + rng.bounded(20);
        p.club = QStringLiteral("Verein %1").arg(rng.bounded(400));
        p.nationality = QStringLiteral("GER");
        p.positionRaw = QStringLiteral("D (C), DM");
        p.personality = QStringLiteral("Balanced");
        for (int a = 0; a < kAttrCount; ++a) {
            const auto v = static_cast<uint8_t>(1 + rng.bounded(20));
            p.attrLo[a] = v;
            p.attrHi[a] = v;
        }
        for (int r = 0; r < 12 && r < roles.size(); ++r)
            p.assignedRoles << roles.at((i + r * 7) % roles.size());
        players.push_back(std::move(p));
    }
    return players;
}

std::vector<DwrsEntry> makeRatings(const std::vector<Player> &players, int snapshot)
{
    std::vector<DwrsEntry> entries;
    const QString ts = QStringLiteral("2026-11-%1 12:00:00").arg(1 + snapshot, 2, 10, QLatin1Char('0'));
    for (const Player &p : players) {
        for (const QString &role : p.assignedRoles) {
            DwrsEntry e;
            e.playerId = p.id;
            e.role = role;
            e.absolute = 50.0 + (p.id % 37) + snapshot;
            e.normalized = 40.0 + (p.id % 53);
            e.timestamp = ts;
            entries.push_back(e);
        }
    }
    return entries;
}

struct Save {
    QString path;
    int players = 0;
    double importMs = 0;     // first full import (players + ratings)
    double reimportMs = 0;   // one more full export of every player
};

Save createSave(const QString &path, int count, const QStringList &roles, int snapshots)
{
    Save save;
    save.path = path;
    save.players = count;
    Database db(Database::uniqueConnectionName(QStringLiteral("gen")));
    db.open(path);
    std::vector<Player> players = makePlayers(count, roles, 7);
    QElapsedTimer t;
    t.start();
    {
        ScopedTransaction tx(db);
        db.upsertPlayers(players);
        db.appendDwrsRatings(makeRatings(players, 0));
        tx.commit();
    }
    save.importMs = t.nsecsElapsed() / 1e6;
    for (int s = 1; s < snapshots; ++s) {
        ScopedTransaction tx(db);
        db.appendDwrsRatings(makeRatings(players, s));
        tx.commit();
    }
    for (Player &p : players)
        p.age += 1;
    t.restart();
    {
        ScopedTransaction tx(db);
        db.upsertPlayers(players, Database::RoleWrite::Keep);
        db.appendDwrsRatings(makeRatings(players, snapshots));
        tx.commit();
    }
    save.reimportMs = t.nsecsElapsed() / 1e6;
    db.setSetting(QStringLiteral("fm_version"), QStringLiteral("fm24"));
    checkpoint(db);
    return save;
}

// What a save switch loads, with the 2.0.4 storage: shared definitions file.
struct Loaded {
    PlayerStore store;
    LatestRatings latest;
    RoleRatings roleRatings;
};

double openShared(const QString &dbPath, const QString &definitionsPath, const QString &iniPath)
{
    return timeMs([&] {
        Database db(Database::uniqueConnectionName(QStringLiteral("open")));
        db.open(dbPath);
        AppConfig config(iniPath);
        Definitions defs;
        defs.load(definitionsPath, QStringLiteral("fm24"));
        config.themeSettings();
        Loaded l;
        l.store.reset(db.loadPlayers());
        l.latest = db.latestDwrsRatings();
        l.roleRatings = RatingsUpdater::roleRatingsForAssigned(l.store, l.latest);
    });
}

// Time until the profile (theme, weights, definitions) is usable, and the
// complete open, with the per-save profile.
QByteArray g_shippedJson;

QPair<double, double> openProfile(const QString &dbPath, const QString &iniPath)
{
    std::vector<double> firstPaint, total;
    for (int i = 0; i < g_repeat; ++i) {
        QElapsedTimer t;
        t.start();
        Database db(Database::uniqueConnectionName(QStringLiteral("open")));
        db.open(dbPath);
        AppConfig config(iniPath);
        QString error;
        config.attachProfile(db, AppConfig::ProfileSeed::SharedConfig, &error);
        config.themeSettings();
        ProfileStore store(db);
        ProfileStore::DefinitionsResult defsResult;
        store.loadDefinitions(QStringLiteral("fm24"), g_shippedJson, {}, defsResult);
        Definitions defs;
        defs.loadFromJson(defsResult.json, QStringLiteral("fm24"));
        firstPaint.push_back(t.nsecsElapsed() / 1e6);
        Loaded l;
        l.store.reset(db.loadPlayers());
        l.latest = db.latestDwrsRatings();
        l.roleRatings = RatingsUpdater::roleRatingsForAssigned(l.store, l.latest);
        total.push_back(t.nsecsElapsed() / 1e6);
        config.detachProfile();
    }
    return {median(firstPaint), median(total)};
}

// Everything the settings page's "Save" button writes (2.0.4: one sync per
// setter), with changed values each run.
void writeAllSettings(AppConfig &config, int run)
{
    const QString v = QStringLiteral("fm24");
    const double d = 0.01 * (run % 7);
    for (auto it = weightDefaults(v).constBegin(); it != weightDefaults(v).constEnd(); ++it)
        config.setWeight(v, it.key(), it.value() + d);
    for (auto it = gkWeightDefaults(v).constBegin(); it != gkWeightDefaults(v).constEnd(); ++it)
        config.setGkWeight(v, it.key(), it.value() + d);
    config.setRoleMultiplier(QStringLiteral("key"), 1.5 + d);
    config.setRoleMultiplier(QStringLiteral("preferable"), 1.2 + d);
    for (const QString &apt : fieldPlayerAptOptions())
        config.setAptWeight(apt, 1.0 + d);
    config.setAgeThreshold(QStringLiteral("outfielder"), 20 + run % 2);
    config.setAgeThreshold(QStringLiteral("goalkeeper"), 25);
    config.setSelectionBonus(QStringLiteral("natural_position"), 1.0 + d);
    config.setIpSharePercent(50 + run % 3);
    for (const char *k : {"max_roles_per_depth_player", "min_loan_talent_score", "min_loan_age",
                          "youth_loan_over_age"})
        config.setSquadManagementSetting(QLatin1String(k), 2 + run % 3);
    for (const char *k : {"displacement_threshold", "dropoff_threshold", "wrong_side_penalty"})
        config.setGapAnalysisSetting(QLatin1String(k), 8.0 + d);
    config.setFreshnessSetting(QStringLiteral("retirement_age"), 35);
    config.setFreshnessSetting(QStringLiteral("stale_after_full_uploads"), 2 + run % 2);
    for (const char *k : {"explosive_peak_age", "explosive_locked_age", "strength_peak_age",
                          "strength_locked_age", "technical_peak_age", "technical_locked_age"})
        config.setTrainingSetting(QLatin1String(k), 21 + run % 2);
    auto theme = config.themeSettings();
    theme.insert(QStringLiteral("night_primary_color"),
                 run % 2 ? QStringLiteral("#dc052d") : QStringLiteral("#6a1bf5"));
    config.saveThemeSettings(theme);
}

void toggleMode(AppConfig &config, int run)
{
    auto theme = config.themeSettings();
    theme.insert(QStringLiteral("current_mode"),
                 run % 2 ? QStringLiteral("day") : QStringLiteral("night"));
    config.saveThemeSettings(theme);
}

QString kb(qint64 bytes)
{
    return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
}

QString ms(double value)
{
    return QStringLiteral("%1 ms").arg(value, 0, 'f', value < 10 ? 2 : 0);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments();
    if (args.size() < 2) {
        std::fprintf(stderr, "Aufruf: fmprofilebench <definitions.json> [--players-small N] "
                             "[--players-large N] [--repeat N]\n");
        return 2;
    }
    const QString definitionsPath = args.at(1);
    int small = 500, large = 35000;
    for (int i = 2; i + 1 < args.size(); i += 2) {
        if (args.at(i) == QLatin1String("--players-small"))
            small = args.at(i + 1).toInt();
        else if (args.at(i) == QLatin1String("--players-large"))
            large = args.at(i + 1).toInt();
        else if (args.at(i) == QLatin1String("--repeat"))
            g_repeat = std::max(1, args.at(i + 1).toInt());
    }

    Definitions defs;
    if (!defs.load(definitionsPath, QStringLiteral("fm24"))) {
        std::fprintf(stderr, "FEHLER: %s\n", qUtf8Printable(defs.errorString()));
        return 1;
    }
    const QStringList roles = defs.validRoles();
    const QByteArray definitionsJson = [&] {
        QFile f(definitionsPath);
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    }();

    g_shippedJson = definitionsJson;
    QTemporaryDir dir;
    const QString ini = dir.filePath(QStringLiteral("config.ini"));
    { AppConfig seed(ini); }

    std::printf("Erzeuge Spielstände (%d / %d Spieler) ...\n", small, large);
    const Save saveSmall = createSave(dir.filePath(QStringLiteral("klein.db")), small, roles, 4);
    const Save saveLarge = createSave(dir.filePath(QStringLiteral("gross.db")), large, roles, 4);

    std::printf("\n| Messung | klein (%d Spieler) | groß (%d Spieler) |\n|---|---|---|\n", small,
                large);
    const auto row = [](const char *label, const QString &a, const QString &b) {
        std::printf("| %s | %s | %s |\n", label, qUtf8Printable(a), qUtf8Printable(b));
    };
    row("Spielstand-Datei ohne Profil", kb(fileBytes(saveSmall.path)), kb(fileBytes(saveLarge.path)));
    row("Import aller Spieler (erster Import)", ms(saveSmall.importMs), ms(saveLarge.importMs));
    row("Erneuter Voll-Import", ms(saveSmall.reimportMs), ms(saveLarge.reimportMs));
    std::printf("| Gemeinsame Dateien 2.0.4 (config.ini + definitions.json) | %s | |\n",
                qUtf8Printable(kb(QFileInfo(ini).size() + definitionsJson.size())));

    // --- 2.0.4: shared config.ini, one sync per setter ---
    row("2.0.4 Öffnen komplett", ms(openShared(saveSmall.path, definitionsPath, ini)),
        ms(openShared(saveLarge.path, definitionsPath, ini)));
    {
        AppConfig config(ini);
        int run = 0;
        const double save = timeMs([&] { writeAllSettings(config, ++run); });
        const double toggle = timeMs([&] { toggleMode(config, ++run); });
        row("2.0.4 Speichern-Knopf (alle Einstellungen)", ms(save), QStringLiteral("gleich"));
        row("2.0.4 Hell/Dunkel umschalten", ms(toggle), QStringLiteral("gleich"));
    }
    const double switchShared = timeMs([&] {
        openShared(saveLarge.path, definitionsPath, ini);
        openShared(saveSmall.path, definitionsPath, ini);
    });
    row("2.0.4 Wechsel groß → klein (je einmal geöffnet)", QStringLiteral("–"),
        ms(switchShared / g_repeat));

    // --- 2.1.0: per-save profile inside the .db ---
    const auto seedProfile = [&](const QString &path) {
        Database db(Database::uniqueConnectionName(QStringLiteral("seed")));
        db.open(path);
        AppConfig config(ini);
        QString error;
        QElapsedTimer t;
        t.start();
        config.attachProfile(db, AppConfig::ProfileSeed::SharedConfig, &error);
        ProfileStore store(db);
        store.writeDocument(ProfileStore::definitionsDocument(QStringLiteral("fm24")),
                            definitionsJson, true);
        const double seedMs = t.nsecsElapsed() / 1e6;
        config.detachProfile();
        checkpoint(db);
        return seedMs;
    };
    const qint64 sizeSmallBefore = fileBytes(saveSmall.path);
    const qint64 sizeLargeBefore = fileBytes(saveLarge.path);
    const double seedSmall = seedProfile(saveSmall.path);
    const double seedLarge = seedProfile(saveLarge.path);
    row("2.1.0 Umstellung (Profil einmalig anlegen)", ms(seedSmall), ms(seedLarge));
    row("2.1.0 Mehrgröße der Datei durch Profil",
        kb(fileBytes(saveSmall.path) - sizeSmallBefore),
        kb(fileBytes(saveLarge.path) - sizeLargeBefore));
    {
        Database db(Database::uniqueConnectionName(QStringLiteral("size")));
        db.open(saveSmall.path);
        ProfileStore store(db);
        QHash<QString, QString> values;
        store.loadSettings(values);
        qint64 settingsBytes = 0;
        for (auto it = values.constBegin(); it != values.constEnd(); ++it)
            settingsBytes += it.key().toUtf8().size() + it.value().toUtf8().size();
        std::printf("| Profil-Inhalt: %lld Einstellungen (%s), Dokumente gespeichert %s "
                    "(unkomprimiert %s) | | |\n",
                    static_cast<long long>(values.size()), qUtf8Printable(kb(settingsBytes)),
                    qUtf8Printable(kb(store.storedDocumentBytes())),
                    qUtf8Printable(kb(definitionsJson.size())));
    }
    const auto openSmall = openProfile(saveSmall.path, ini);
    const auto openLarge = openProfile(saveLarge.path, ini);
    row("2.1.0 Profil bereit (Farben, Gewichte, Rollen)", ms(openSmall.first), ms(openLarge.first));
    row("2.1.0 Öffnen komplett", ms(openSmall.second), ms(openLarge.second));
    for (const Save *save : {&saveSmall, &saveLarge}) {
        Database db(Database::uniqueConnectionName(QStringLiteral("save")));
        db.open(save->path);
        AppConfig config(ini);
        QString error;
        config.attachProfile(db, AppConfig::ProfileSeed::SharedConfig, &error);
        int run = 0;
        const double single = timeMs([&] { writeAllSettings(config, ++run); });
        const double batched = timeMs([&] {
            AppConfig::Transaction tx(config);
            writeAllSettings(config, ++run);
            tx.commit();
        });
        const double toggle = timeMs([&] { toggleMode(config, ++run); });
        std::printf("| 2.1.0 Speichern-Knopf %s: einzeln %s, als eine Transaktion %s; "
                    "Hell/Dunkel %s | | |\n",
                    save == &saveSmall ? "klein" : "groß", qUtf8Printable(ms(single)),
                    qUtf8Printable(ms(batched)), qUtf8Printable(ms(toggle)));
        config.detachProfile();
    }
    const double switchProfile = timeMs([&] {
        openProfile(saveLarge.path, ini);
        openProfile(saveSmall.path, ini);
    });
    row("2.1.0 Wechsel groß → klein (je einmal geöffnet)", QStringLiteral("–"),
        ms(switchProfile / g_repeat));
    // Import with the profile tables present (they must not slow it down).
    {
        Database db(Database::uniqueConnectionName(QStringLiteral("imp")));
        db.open(saveLarge.path);
        std::vector<Player> players = db.loadPlayers();
        QElapsedTimer t;
        t.start();
        ScopedTransaction tx(db);
        db.upsertPlayers(players, Database::RoleWrite::Keep);
        db.appendDwrsRatings(makeRatings(players, 9));
        tx.commit();
        row("2.1.0 Erneuter Voll-Import (mit Profil)", QStringLiteral("–"),
            ms(t.nsecsElapsed() / 1e6));
    }
    return 0;
}
