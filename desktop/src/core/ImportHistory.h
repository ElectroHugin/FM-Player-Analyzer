#pragma once

// What every import leaves behind besides the current player data: one entry
// in the import log (real time, in-game date, source) and, for the players the
// user follows, a snapshot of their visible data per in-game month.
//
// Rules that keep the history small even for databases of 100k players:
//   - at most one snapshot per player and in-game month: a later import in the
//     same month replaces it (autosaves and live following add nothing);
//   - only changes: a snapshot equal to the player's previous one is not kept;
//   - only followed players by default (own club, second team, shortlist,
//     national squad and, from an FM27 save, players the user's scouting knows);
//     the per-save setting kAllPlayersSetting extends it to everyone imported;
//   - monthly for the current season and the two before it; older seasons are
//     condensed to one snapshot per player: the season's last one (end-of-season
//     attributes; FM's average rating is a season figure, so its last value is
//     the season's).
// A season starts on 1 July (European calendar), see seasonOf().
//
// Without an in-game date (an export whose date the user did not give) the
// import is logged, but no snapshot is taken: it could not be placed in time.

#include "Attributes.h"

#include <QDate>
#include <QDateTime>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>
#include <vector>

namespace fm {

class Database;
struct Player;

namespace ImportHistory {

// Per-database setting: "1" = snapshots of every imported player.
inline constexpr const char *kAllPlayersSetting = "history_all_players";
// Seasons before the current one that keep monthly snapshots.
inline constexpr int kMonthlySeasonsBack = 2;
// First month of a season.
inline constexpr int kSeasonStartMonth = 7;

struct ImportRecord {
    int id = 0;
    QString importedAt; // "yyyy-MM-dd HH:mm:ss", local time
    QDate gameDate;     // invalid = unknown
    QString source;     // file name
    QString format;     // "fm", "csv", "html"
    int players = 0;
};

struct Snapshot {
    int playerId = 0;
    QDate gameDate;
    bool condensed = false; // one snapshot for a whole (old) season
    std::array<uint8_t, kAttrCount> attrLo{};
    std::array<uint8_t, kAttrCount> attrHi{};
    double averageRating = 0.0;
    double transferValue = 0.0; // parsed, compared
    QString transferValueText;  // as the import showed it
    QString wage;

    // Same visible data (dates and condensing aside).
    bool sameData(const Snapshot &other) const;
};

// Season of a date: the year it starts in (2026-08-01 and 2027-05-31 -> 2026).
int seasonOf(const QDate &date);
// "yyyy-MM" for a monthly snapshot, "S<season>" for a condensed one.
QString periodKey(const QDate &date, bool condensed);
// Display label of a season ("2026/27").
QString seasonLabel(int season);

// In-game date at a wall-clock moment: that of the newest import at or
// before it (imports oldest first, as Database::imports() returns them).
// Invalid when that import had no date or there was none.
QDate gameDateAt(const std::vector<ImportRecord> &imports, const QDateTime &moment);

// The player data a snapshot keeps.
Snapshot snapshotOf(const Player &player, const QDate &gameDate);

// Whether a player of this import is followed (default scope).
struct Scope {
    bool allPlayers = false;
    QString userClub;
    QString secondTeamClub;
    QSet<QString> knownUids; // from the save: scouted players (FM UIDs)
    bool contains(const Player &player) const;
};
Scope scopeFor(Database &db, const QStringList &knownUids);

// Writes the import log entry and the snapshots of one import (inside the
// caller's transaction), then condenses seasons that left the monthly window.
// Returns false with db.errorString() set on a database error.
bool record(Database &db, const ImportRecord &import, const std::vector<Snapshot> &snapshots);

} // namespace ImportHistory

} // namespace fm
