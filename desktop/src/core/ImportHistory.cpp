#include "ImportHistory.h"

#include "Database.h"
#include "Player.h"

#include <QHash>
#include <QSet>

#include <cmath>
#include <map>

namespace fm {

namespace ImportHistory {

bool Snapshot::sameData(const Snapshot &other) const
{
    return attrLo == other.attrLo && attrHi == other.attrHi
           && std::abs(averageRating - other.averageRating) < 0.005
           && std::abs(transferValue - other.transferValue) < 0.5 && wage == other.wage;
}

int seasonOf(const QDate &date)
{
    return date.month() >= kSeasonStartMonth ? date.year() : date.year() - 1;
}

QString periodKey(const QDate &date, bool condensed)
{
    if (condensed)
        return QStringLiteral("S%1").arg(seasonOf(date));
    return date.toString(QStringLiteral("yyyy-MM"));
}

QString seasonLabel(int season)
{
    return QStringLiteral("%1/%2").arg(season).arg((season + 1) % 100, 2, 10, QLatin1Char('0'));
}

QDate gameDateAt(const std::vector<ImportRecord> &imports, const QDateTime &moment)
{
    const QString key = moment.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QDate date;
    for (const ImportRecord &record : imports) {
        if (record.importedAt > key)
            break;
        date = record.gameDate;
    }
    return date;
}

Snapshot snapshotOf(const Player &player, const QDate &gameDate)
{
    Snapshot s;
    s.playerId = player.id;
    s.gameDate = gameDate;
    s.attrLo = player.attrLo;
    s.attrHi = player.attrHi;
    s.averageRating = player.averageRating;
    s.transferValue = player.transferValue;
    s.transferValueText = player.transferValueRaw;
    s.wage = player.wageRaw;
    return s;
}

bool Scope::contains(const Player &player) const
{
    if (allPlayers || player.onShortlist || player.inNationalSquad)
        return true;
    if (!player.club.isEmpty()
        && (player.club == userClub || (!secondTeamClub.isEmpty() && player.club == secondTeamClub)))
        return true;
    if (knownUids.isEmpty())
        return false;
    // The import may have unified "123" into a newgen's "r-123".
    return knownUids.contains(player.uid)
           || (player.uid.startsWith(QLatin1String("r-")) && knownUids.contains(player.uid.mid(2)));
}

Scope scopeFor(Database &db, const QStringList &knownUids)
{
    Scope scope;
    scope.allPlayers = db.setting(QString::fromLatin1(kAllPlayersSetting)) == QLatin1String("1");
    scope.userClub = db.setting(QStringLiteral("user_club"));
    scope.secondTeamClub = db.setting(QStringLiteral("second_team_club"));
    scope.knownUids = QSet<QString>(knownUids.cbegin(), knownUids.cend());
    return scope;
}

namespace {

// Seasons before `keepFrom` collapse to each player's last snapshot of the
// season (a condensed row already there counts like any other).
bool condense(Database &db, int keepFrom)
{
    const QDate cut(keepFrom, kSeasonStartMonth, 1);
    const std::vector<Snapshot> monthly = db.monthlySnapshotsBefore(cut);
    if (monthly.empty())
        return true;

    // Last snapshot per (player, season) among the monthly rows.
    std::map<std::pair<int, int>, const Snapshot *> last;
    for (const Snapshot &s : monthly) {
        const auto key = std::make_pair(s.playerId, seasonOf(s.gameDate));
        auto it = last.find(key);
        if (it == last.end() || it->second->gameDate < s.gameDate)
            last[key] = &s;
    }
    // An existing condensed row of the same season wins when it is newer.
    std::vector<std::pair<int, QString>> removed;
    removed.reserve(monthly.size());
    for (const Snapshot &s : monthly)
        removed.push_back({s.playerId, periodKey(s.gameDate, false)});
    std::vector<Snapshot> kept;
    QHash<int, std::vector<Snapshot>> condensedByPlayer;
    for (const auto &[key, snapshot] : last) {
        auto &own = condensedByPlayer[key.first];
        if (own.empty())
            for (const Snapshot &s : db.snapshots(key.first))
                if (s.condensed)
                    own.push_back(s);
        bool newerExists = false;
        for (const Snapshot &s : own)
            newerExists |= seasonOf(s.gameDate) == key.second && s.gameDate >= snapshot->gameDate;
        if (newerExists)
            continue;
        Snapshot c = *snapshot;
        c.condensed = true;
        kept.push_back(c);
    }
    return db.deleteSnapshots(removed) && db.upsertSnapshots(kept);
}

} // namespace

bool record(Database &db, const ImportRecord &import, const std::vector<Snapshot> &snapshots)
{
    if (!db.beginTransaction())
        return false;
    const auto fail = [&db] {
        db.rollbackTransaction();
        return false;
    };
    if (db.addImport(import) == 0)
        return fail();
    if (!import.gameDate.isValid() || snapshots.empty())
        return db.commitTransaction();

    // Only changes against each player's last snapshot before this month; an
    // unchanged player also loses an earlier snapshot of this same month.
    const QDate monthStart(import.gameDate.year(), import.gameDate.month(), 1);
    const QHash<int, Snapshot> previous = db.latestSnapshotsBefore(monthStart);
    std::vector<Snapshot> changed;
    QList<int> unchanged;
    for (const Snapshot &s : snapshots) {
        const auto it = previous.constFind(s.playerId);
        if (it != previous.constEnd() && it->sameData(s)) {
            unchanged << s.playerId;
            continue;
        }
        Snapshot copy = s;
        copy.gameDate = import.gameDate;
        copy.condensed = false;
        changed.push_back(std::move(copy));
    }
    if (!db.deleteMonthSnapshots(unchanged, import.gameDate) || !db.upsertSnapshots(changed))
        return fail();
    if (!condense(db, seasonOf(import.gameDate) - kMonthlySeasonsBack))
        return fail();
    return db.commitTransaction();
}

} // namespace ImportHistory

} // namespace fm
