#include "Development.h"

#include "Utils.h"

#include <QMap>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fm {
namespace Development {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

} // namespace

double Track::current() const
{
    return values.isEmpty() ? kNaN : values.last();
}

const Track *Timeline::track(int playerId, const QString &role) const
{
    const auto it = tracks.constFind({playerId, role});
    return it == tracks.constEnd() ? nullptr : &it.value();
}

QList<Session> groupSessions(const QList<QDateTime> &sortedTimes, int gapMinutes)
{
    QList<Session> sessions;
    const qint64 maxGapSecs = static_cast<qint64>(gapMinutes) * 60;
    for (const QDateTime &time : sortedTimes) {
        if (!sessions.isEmpty() && sessions.last().last.secsTo(time) <= maxGapSecs) {
            sessions.last().last = time;
            continue;
        }
        sessions.append({time, time});
    }
    return sessions;
}

Timeline buildTimeline(const std::vector<DwrsEntry> &history,
                       const QSet<QString> &recalcTimestamps, int gapMinutes)
{
    Timeline timeline;

    // Distinct timestamps -> snapshots.
    QMap<QString, QDateTime> times; // sorted by the "yyyy-MM-dd HH:mm:ss" text = chronological
    for (const DwrsEntry &entry : history) {
        if (times.contains(entry.timestamp))
            continue;
        const QDateTime parsed = parseDwrsTimestamp(entry.timestamp);
        if (parsed.isValid())
            times.insert(entry.timestamp, parsed);
    }
    timeline.sessions = groupSessions(times.values(), gapMinutes);
    const int n = timeline.size();
    if (n == 0)
        return timeline;

    QHash<QString, int> sessionOf;
    int session = 0;
    for (auto it = times.constBegin(); it != times.constEnd(); ++it) {
        while (it.value() > timeline.sessions.at(session).last)
            ++session;
        sessionOf.insert(it.key(), session);
    }

    // Rows per track in chronological order.
    QHash<TrackKey, std::vector<const DwrsEntry *>> rowsByTrack;
    for (const DwrsEntry &entry : history) {
        if (sessionOf.contains(entry.timestamp))
            rowsByTrack[{entry.playerId, entry.role}].push_back(&entry);
    }
    for (auto it = rowsByTrack.begin(); it != rowsByTrack.end(); ++it) {
        auto &rows = it.value();
        std::sort(rows.begin(), rows.end(), [](const DwrsEntry *a, const DwrsEntry *b) {
            return a->timestamp < b->timestamp;
        });
        Track track;
        track.values.fill(kNaN, n);
        track.changes.fill(0.0, n);
        track.rebases.fill(0.0, n);
        double previous = kNaN;
        for (const DwrsEntry *row : rows) {
            const int s = sessionOf.value(row->timestamp);
            if (!std::isnan(previous)) {
                const double step = row->normalized - previous;
                (recalcTimestamps.contains(row->timestamp) ? track.rebases : track.changes)[s] +=
                    step;
            }
            if (track.firstSession < 0)
                track.firstSession = s;
            track.values[s] = row->normalized; // later rows of a snapshot win
            previous = row->normalized;
        }
        for (int s = track.firstSession + 1; s < n; ++s) {
            if (std::isnan(track.values[s]))
                track.values[s] = track.values[s - 1];
        }
        track.adjusted.fill(kNaN, n);
        track.adjusted[n - 1] = track.values[n - 1];
        for (int s = n - 1; s > track.firstSession; --s)
            track.adjusted[s - 1] = track.adjusted[s] - track.changes[s];
        timeline.tracks.insert(it.key(), std::move(track));
    }
    return timeline;
}

int windowStart(int sessionCount, int window)
{
    if (window <= 0)
        return 0;
    return std::max(0, sessionCount - 1 - window);
}

double change(const Track &track, int window)
{
    const int n = static_cast<int>(track.changes.size());
    // A window's changes happen after its baseline snapshot; the whole history
    // also counts uploads merged into the very first snapshot.
    const int from = window <= 0 ? 0 : windowStart(n, window) + 1;
    double sum = 0.0;
    for (int s = from; s < n; ++s)
        sum += track.changes.at(s);
    return sum;
}

QList<int> recalcMarkers(const QList<const Track *> &tracks, int start)
{
    QSet<int> sessions;
    for (const Track *track : tracks) {
        for (int s = std::max(0, start + 1); s < track->rebases.size(); ++s) {
            if (track->rebases.at(s) != 0.0)
                sessions.insert(s);
        }
    }
    QList<int> sorted(sessions.cbegin(), sessions.cend());
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

QVector<double> cumulativeMeanChange(const QList<const Track *> &tracks, int start)
{
    int n = 0;
    for (const Track *track : tracks)
        n = std::max(n, static_cast<int>(track->values.size()));
    QVector<double> result(n, kNaN);
    if (start < 0 || start >= n)
        return result;
    result[start] = 0.0;
    for (int s = start + 1; s < n; ++s) {
        double sum = 0.0;
        int count = 0;
        for (const Track *track : tracks) {
            if (s < track->values.size() && !std::isnan(track->values.at(s - 1))) {
                sum += track->changes.at(s);
                ++count;
            }
        }
        result[s] = result[s - 1] + (count > 0 ? sum / count : 0.0);
    }
    return result;
}

} // namespace Development
} // namespace fm
