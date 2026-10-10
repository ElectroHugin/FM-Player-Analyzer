#pragma once

#include "Database.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVector>

#include <vector>

namespace fm {

// DWRS development measured in uploads, not calendar time. The export carries
// no in-game date and the wall-clock time of an upload says nothing about how
// much game time passed, so the history is laid out on an evenly spaced axis
// of "data snapshots" (sessions): uploads a few minutes apart (squad export,
// shortlist, full export of the same moment) collapse into one snapshot.
//
// A history row is written only when a rating is new or moved by >= 1%, so a
// track carries its last value forward through snapshots without a row.
// Rows written by a recalculation (formula, weights or definitions changed)
// move the rating without any development; they re-base the track and never
// count as change.
namespace Development {

// Consecutive uploads at most this far apart form one snapshot.
constexpr int kSessionGapMinutes = 10;

struct Session {
    QDateTime first; // earliest upload of the snapshot
    QDateTime last;  // latest upload of the snapshot
};

struct Track {
    QVector<double> values;  // per snapshot: rating at its end; NaN before the first rating
    QVector<double> changes; // per snapshot: genuine change during it (recalcs excluded)
    QVector<double> rebases; // per snapshot: jump caused by recalculations
    // Per snapshot: the rating re-based onto the current calculation — the
    // current value minus all genuine change since. Equals `values` until a
    // recalculation happens; afterwards the curve shows development only.
    QVector<double> adjusted;
    int firstSession = -1;   // first snapshot with a rating (-1: none)

    bool isValid() const { return firstSession >= 0; }
    double current() const;  // value at the last snapshot (NaN if invalid)
};

using TrackKey = QPair<int, QString>; // (player id, role)

struct Timeline {
    QList<Session> sessions;
    QHash<TrackKey, Track> tracks;

    int size() const { return static_cast<int>(sessions.size()); }
    const Track *track(int playerId, const QString &role) const;
};

// Groups ascending timestamps into snapshots: a gap of at most gapMinutes to
// the previous upload keeps a timestamp in the current snapshot.
QList<Session> groupSessions(const QList<QDateTime> &sortedTimes,
                             int gapMinutes = kSessionGapMinutes);

// Lays the given history rows (any players and roles, any order) out on
// snapshots built from their own timestamps. recalcTimestamps: the "ts" values
// written by recalculations (Database::dwrsRecalcTimestamps).
Timeline buildTimeline(const std::vector<DwrsEntry> &history,
                       const QSet<QString> &recalcTimestamps,
                       int gapMinutes = kSessionGapMinutes);

// First snapshot of a window over the last `window` uploads: the baseline the
// window's changes start from. window <= 0 means the whole history.
int windowStart(int sessionCount, int window);

// Genuine change of a track during the window (window <= 0: whole history).
// Equals "last value - value at windowStart" unless a recalc happened inside.
double change(const Track &track, int window);

// Snapshots after `start` in which a recalculation moved any of the tracks
// (ascending) — where a chart of these tracks marks a re-base.
QList<int> recalcMarkers(const QList<const Track *> &tracks, int start);

// Squad trend: cumulative mean genuine change per snapshot, 0 at `start` and
// NaN before it. A snapshot's step averages the tracks that already had a
// rating at the previous snapshot, so players joining the data later (new
// signings) shift no level.
QVector<double> cumulativeMeanChange(const QList<const Track *> &tracks, int start);

} // namespace Development

} // namespace fm
