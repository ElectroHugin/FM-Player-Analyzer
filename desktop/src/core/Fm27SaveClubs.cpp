#include "Fm27SaveClubs.h"

#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

namespace fm::fm27save {

namespace {

inline quint32 u32(const char *p)
{
    quint32 v;
    std::memcpy(&v, p, 4);
    return v;
}
inline quint16 u16(const char *p)
{
    quint16 v;
    std::memcpy(&v, p, 2);
    return v;
}

constexpr quint32 kMissing = 0xFFFFFFFFu;

// Club record (fmsave ClubRecordLayout).
const QByteArray kAnchor("\xff\xff\xff\xff", 4);
constexpr int kAnchorOffset = 17;
constexpr int kClubIndexOffset = 0;
constexpr int kUidOffset = 4;
constexpr int kUidCopyOffset = 8;
constexpr int kZeroByteOffset = 12;
constexpr int kNationOffset = 13;
constexpr int kFaNationOffset = 21;
constexpr int kNationCopyOffset = 25;
constexpr int kLongNameOffset = 39;
constexpr quint32 kNationMax = 999;
constexpr quint32 kMaxNameBytes = 255;
constexpr qint64 kStopGapBytes = 64 * 1024;
constexpr int kRecordHeaderBytes = kLongNameOffset + 4;

// Counted club table (fmsave ClubTableLayout, FM27 27.0.0).
constexpr int kTableHeaderBytes = 5;
constexpr quint32 kTableCountMax = 200000;
constexpr uint8_t kOrdinaryVariant = 0;
constexpr uint8_t kExtendedVariant = 12;
constexpr int kExtendedPrefixBytes = 4;
constexpr int kPrefixBytes = 26;
constexpr quint32 kTableMaxNameBytes = 16384;
constexpr quint32 kMaxEntries = 100000;
constexpr int kBaseFinanceBytes = 45;

struct Step {
    int countBytes; // 0 = exactly one entry
    int entryBytes;
};
const Step kAfterNames[] = {{0, 8}, {1, 4}};
const Step kFullFinance[] = {{0, 66},
                             {0, 235}, {1, 8}, {1, 8},
                             {0, 235}, {1, 8}, {1, 8},
                             {0, 235}, {1, 8}, {1, 8},
                             {0, 235}, {1, 8}, {1, 8},
                             {0, 67}, {1, 230}, {4, 49}, {0, 71}, {1, 19}, {0, 67}};
const Step kAfterFinance[] = {{1, 25}, {1, 12}, {0, 9}, {1, 4}, {1, 4},
                              {2, 4}, {2, 4}, {2, 4}, {1, 16}, {2, 28}};

// Team list (fmsave TeamListLayout).
constexpr int kCountsOffset = 37;
constexpr int kFirstEntryBytes = 25;
constexpr uint8_t kFirstEntryLead = 0x02;
constexpr int kSecondEntryBytes = 12;
constexpr uint8_t kSecondEntryLead = 0x01;
constexpr int kFillerBytes = 9;
constexpr int kTeamCountMax = 8;
constexpr quint32 kTeamIdMax = 3000000;
constexpr int kAffiliateCountMax = 8;

template <size_t N>
std::optional<qint64> walkSteps(const QByteArray &d, qint64 position, const Step (&steps)[N])
{
    const qint64 n = d.size();
    for (const Step &step : steps) {
        quint64 count = 1;
        if (step.countBytes) {
            if (position + step.countBytes > n)
                return std::nullopt;
            const char *p = d.constData() + position;
            count = step.countBytes == 1 ? quint64(uint8_t(*p))
                    : step.countBytes == 2 ? quint64(u16(p))
                                           : quint64(u32(p));
            position += step.countBytes;
            if (count > kMaxEntries)
                return std::nullopt;
        }
        position += qint64(count) * step.entryBytes;
        if (position > n)
            return std::nullopt;
    }
    return position;
}

struct TableRow {
    qint64 bodyStart;
    qint64 footerStart;
    quint32 uniqueId;
    qint64 teamCountsStart;
};

std::optional<TableRow> clubTableRecord(const QByteArray &d, qint64 body)
{
    const qint64 n = d.size();
    if (body < 0 || body >= n)
        return std::nullopt;
    const uint8_t kind = uint8_t(d[body]);
    if (kind != kOrdinaryVariant && kind != kExtendedVariant)
        return std::nullopt;
    qint64 cursor = body + 1 + kPrefixBytes + (kind == kExtendedVariant ? kExtendedPrefixBytes : 0);
    for (int i = 0; i < 2; ++i) {
        if (cursor + 4 > n)
            return std::nullopt;
        const quint32 length = u32(d.constData() + cursor);
        if (length > kTableMaxNameBytes)
            return std::nullopt;
        cursor += 4 + length;
        if (cursor > n)
            return std::nullopt;
    }
    auto next = walkSteps(d, cursor, kAfterNames);
    if (!next || *next >= n)
        return std::nullopt;
    cursor = *next;
    const uint8_t finance = uint8_t(d[cursor]);
    ++cursor;
    if (finance == 0) {
        cursor += kBaseFinanceBytes;
    } else if (finance == 1) {
        next = walkSteps(d, cursor, kFullFinance);
        if (!next)
            return std::nullopt;
        cursor = *next;
    } else if (finance != 2) {
        return std::nullopt;
    }
    if (cursor > n)
        return std::nullopt;
    const qint64 teamCountsStart = cursor;
    next = walkSteps(d, cursor, kAfterFinance);
    if (!next || *next + 12 > n)
        return std::nullopt;
    return TableRow{body, *next, u32(d.constData() + *next + 4), teamCountsStart};
}

std::optional<std::vector<TableRow>> walkClubTable(const QByteArray &d, qint64 header)
{
    if (header < 0 || header + kTableHeaderBytes > d.size())
        return std::nullopt;
    const quint32 count = u32(d.constData() + header);
    if (count < 1 || count > kTableCountMax || d[header + 4] != 0 || count > quint64(d.size()) / 12)
        return std::nullopt;
    qint64 cursor = header + kTableHeaderBytes;
    std::vector<TableRow> rows;
    rows.reserve(count);
    QSet<quint32> seen;
    for (quint32 ordinal = 0; ordinal < count; ++ordinal) {
        const auto row = clubTableRecord(d, cursor);
        if (!row)
            return std::nullopt;
        const quint32 stored = u32(d.constData() + row->footerStart);
        const quint32 uid = u32(d.constData() + row->footerStart + 4);
        if (stored != ordinal || uid == 0 || uid == kMissing || seen.contains(uid))
            return std::nullopt;
        seen.insert(uid);
        rows.push_back(*row);
        cursor = row->footerStart + 12;
    }
    return rows;
}

struct Record {
    qint64 start = 0;
    quint32 clubIndex = 0;
    quint32 uid = 0;
    bool mainNationMissing = false;
    QString name;
    QString shortName;
};

bool recordNames(const QByteArray &d, qint64 start, QString *name, QString *shortName)
{
    const qint64 n = d.size();
    const qint64 longAt = start + kLongNameOffset;
    if (longAt < 0 || longAt + 4 > n)
        return false;
    const quint32 longLength = u32(d.constData() + longAt);
    if (longLength < 1 || longLength > kMaxNameBytes)
        return false;
    const qint64 shortAt = longAt + 4 + longLength;
    if (shortAt + 4 > n)
        return false;
    const quint32 shortLength = u32(d.constData() + shortAt);
    if (shortLength < 1 || shortLength > kMaxNameBytes || shortAt + 4 + shortLength > n)
        return false;
    const QByteArray longBytes(d.constData() + longAt + 4, longLength);
    const QByteArray shortBytes(d.constData() + shortAt + 4, shortLength);
    QStringDecoder decode(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    *name = decode(longBytes);
    if (decode.hasError())
        return false;
    QStringDecoder decodeShort(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    *shortName = decodeShort(shortBytes);
    return !decodeShort.hasError();
}

std::optional<Record> acceptedRecord(const QByteArray &d, qint64 start,
                                     std::optional<std::pair<quint32, quint32>> identity,
                                     bool allowMissingMainNation)
{
    if (start < 0 || start + kRecordHeaderBytes > d.size())
        return std::nullopt;
    const char *p = d.constData() + start;
    const quint32 nation = u32(p + kNationOffset);
    const bool mainMissing = allowMissingMainNation && nation == kMissing;
    if (!mainMissing && nation > kNationMax)
        return std::nullopt;
    if (u32(p + kNationCopyOffset) > kNationMax || u32(p + kFaNationOffset) > kNationMax)
        return std::nullopt;
    if (!identity || mainMissing) {
        const quint32 storedUid = u32(p + kUidOffset);
        if (u32(p + kUidCopyOffset) != storedUid)
            return std::nullopt;
        if (!identity)
            identity = std::make_pair(u32(p + kClubIndexOffset) + 1, storedUid + 1);
    }
    Record record;
    if (!recordNames(d, start, &record.name, &record.shortName))
        return std::nullopt;
    record.start = start;
    record.clubIndex = identity->first;
    record.uid = identity->second;
    record.mainNationMissing = mainMissing;
    return record;
}

std::vector<Record> scanClubRecords(const QByteArray &d)
{
    std::vector<Record> records;
    qint64 lastAccepted = 0;
    qsizetype hit = d.indexOf(kAnchor, kAnchorOffset);
    while (hit >= 0) {
        const qint64 start = hit - kAnchorOffset;
        if (!records.empty() && start - lastAccepted >= kStopGapBytes)
            break;
        if (start + kRecordHeaderBytes <= d.size() && d[start + kZeroByteOffset] == 0) {
            if (auto record = acceptedRecord(d, start, std::nullopt, false)) {
                records.push_back(std::move(*record));
                lastAccepted = start;
            }
        }
        hit = d.indexOf(kAnchor, hit + 1);
    }
    return records;
}

struct TeamList {
    std::vector<quint32> teamIds;
    std::vector<quint32> affiliateTeamIds;
    int gender = -1; // 0 men, 1 women, -1 unknown
};

std::vector<quint32> readIds(const QByteArray &d, qint64 at, int count)
{
    std::vector<quint32> ids;
    ids.reserve(count);
    for (int i = 0; i < count; ++i)
        ids.push_back(u32(d.constData() + at + 4 * i));
    return ids;
}

std::optional<TeamList> parseTeamList(const QByteArray &d, qint64 listStart, qint64 recordEnd)
{
    qint64 cursor = listStart + kCountsOffset;
    const std::pair<int, uint8_t> entries[] = {{kFirstEntryBytes, kFirstEntryLead},
                                               {kSecondEntryBytes, kSecondEntryLead}};
    for (const auto &[entryBytes, lead] : entries) {
        if (cursor < 0 || cursor >= recordEnd)
            return std::nullopt;
        const qint64 entriesStart = cursor + 1;
        const qint64 entriesEnd = entriesStart + qint64(uint8_t(d[cursor])) * entryBytes;
        if (entriesEnd > recordEnd)
            return std::nullopt;
        for (qint64 e = entriesStart; e < entriesEnd; e += entryBytes) {
            if (uint8_t(d[e]) != lead)
                return std::nullopt;
        }
        cursor = entriesEnd;
    }
    const qint64 teamCountAt = cursor + kFillerBytes;
    if (teamCountAt >= recordEnd)
        return std::nullopt;
    const int teamCount = uint8_t(d[teamCountAt]);
    if (teamCount < 1 || teamCount > kTeamCountMax)
        return std::nullopt;
    const qint64 idsStart = teamCountAt + 1;
    if (idsStart + 4 * teamCount > recordEnd)
        return std::nullopt;
    TeamList list;
    // The byte before the team count codes the club's gender (0 men, 1 women).
    if (teamCountAt - 1 >= listStart)
        list.gender = uint8_t(d[teamCountAt - 1]);
    list.teamIds = readIds(d, idsStart, teamCount);
    for (quint32 id : list.teamIds) {
        if (id < 1 || id > kTeamIdMax)
            return std::nullopt;
    }
    const qint64 affiliateCountAt = idsStart + 4 * teamCount;
    if (affiliateCountAt < recordEnd) {
        const int affiliateCount = uint8_t(d[affiliateCountAt]);
        if (affiliateCount >= 1 && affiliateCount <= kAffiliateCountMax
            && affiliateCountAt + 1 + 4 * affiliateCount <= recordEnd) {
            auto ids = readIds(d, affiliateCountAt + 1, affiliateCount);
            if (std::all_of(ids.begin(), ids.end(), [](quint32 id) { return id >= 1 && id <= kTeamIdMax; }))
                list.affiliateTeamIds = std::move(ids);
        }
    }
    return list;
}

TeamList readTeamList(const QByteArray &d, qint64 countsStart, qint64 recordEnd)
{
    return parseTeamList(d, countsStart - kCountsOffset, recordEnd).value_or(TeamList{});
}

} // namespace

bool readClubTeams(const QByteArray &d, ClubTeams *out, QString *error)
{
    const auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    const std::vector<Record> scanned = scanClubRecords(d);

    // Find the counted table that frames every scanned record.
    const qint64 prefixEnd = scanned.empty() ? d.size()
                                             : scanned.front().start + kAnchorOffset + kAnchor.size();
    const int anchorDistance = kAnchorOffset - kZeroByteOffset;
    QSet<qint64> headers;
    qsizetype hit = d.indexOf(kAnchor, 0);
    while (hit >= 0 && hit + kAnchor.size() <= prefixEnd) {
        for (int extra : {0, kExtendedPrefixBytes}) {
            const qint64 body = hit - anchorDistance - extra;
            const qint64 head = body - kTableHeaderBytes;
            if (head < 0 || d[head + 4] != 0)
                continue;
            const uint8_t variant = extra ? kExtendedVariant : kOrdinaryVariant;
            if (uint8_t(d[body]) != variant)
                continue;
            const auto row = clubTableRecord(d, body);
            if (!row)
                continue;
            const quint32 count = u32(d.constData() + head);
            const bool plausible = count >= 1 && count <= kTableCountMax;
            const bool closesAtFirst = !scanned.empty() && row->footerStart == scanned.front().start;
            if ((plausible || closesAtFirst) && row->uniqueId != 0 && row->uniqueId != kMissing)
                headers.insert(head);
        }
        hit = d.indexOf(kAnchor, hit + 1);
    }
    QList<qint64> sortedHeaders(headers.cbegin(), headers.cend());
    std::sort(sortedHeaders.begin(), sortedHeaders.end());
    std::vector<std::vector<TableRow>> tables;
    for (qint64 head : sortedHeaders) {
        auto rows = walkClubTable(d, head);
        if (!rows)
            continue;
        const qint64 tableEnd = rows->back().footerStart + 12;
        const qint64 first = rows->front().bodyStart;
        if (std::all_of(scanned.begin(), scanned.end(), [&](const Record &r) {
                const qint64 zero = r.start + kZeroByteOffset;
                return first <= zero && zero < tableEnd;
            }))
            tables.push_back(std::move(*rows));
    }
    if (tables.size() != 1)
        return fail(QStringLiteral("the club table was not found or is ambiguous"));
    const std::vector<TableRow> &rows = tables.front();

    // One club per usable table row.
    struct Bound {
        Record record;
        TableRow row;
        TeamList teams;
    };
    std::vector<Bound> bound;
    for (size_t ordinal = 0; ordinal < rows.size(); ++ordinal) {
        const TableRow &row = rows[ordinal];
        const int extra = uint8_t(d[row.bodyStart]) == kExtendedVariant ? kExtendedPrefixBytes : 0;
        const qint64 start = row.bodyStart + extra - kZeroByteOffset;
        if (ordinal && extra)
            continue;
        if (ordinal && d.mid(start + kAnchorOffset, 4) != kAnchor)
            continue;
        const quint32 uid = ordinal == 0 ? 0 : rows[ordinal - 1].uniqueId + 1;
        auto record = acceptedRecord(d, start, std::make_pair(quint32(ordinal), uid), ordinal != 0);
        TeamList teams;
        if (record)
            teams = readTeamList(d, row.teamCountsStart, row.footerStart);
        if (record && record->mainNationMissing && teams.teamIds.empty())
            record.reset();
        if (!record) {
            if (ordinal == 0)
                return fail(QStringLiteral("the first club has invalid fields"));
            continue;
        }
        bound.push_back({std::move(*record), row, std::move(teams)});
    }

    ClubTeams result;
    QHash<quint32, int> clubByUid;
    QHash<quint32, int> storingClub; // team id -> index of the club that stores it
    for (const Bound &b : bound) {
        if (clubByUid.contains(b.record.uid))
            return fail(QStringLiteral("a club appears twice in the club table"));
        clubByUid.insert(b.record.uid, result.clubs.size());
        ClubInfo info;
        info.uid = b.record.uid;
        // The footer that closes a table row holds this club's index and Unique ID.
        if (u32(d.constData() + b.row.footerStart) == b.record.clubIndex)
            info.uniqueId = b.row.uniqueId;
        info.name = b.record.name;
        info.shortName = b.record.shortName;
        info.women = b.teams.gender == 1;
        for (quint32 team : b.teams.teamIds) {
            if (storingClub.contains(team))
                return fail(QStringLiteral("a team is listed by two clubs"));
            storingClub.insert(team, result.clubs.size());
        }
        result.clubs.append(info);
    }
    result.clubByTeam = storingClub;
    // Affiliated teams are fielded by the club that lists them.
    QSet<quint32> claimed;
    for (int i = 0; i < int(bound.size()); ++i) {
        std::vector<quint32> linked;
        for (quint32 team : bound[size_t(i)].teams.affiliateTeamIds) {
            const auto storing = storingClub.constFind(team);
            if (storing == storingClub.constEnd() || *storing == i || claimed.contains(team)
                || std::find(linked.begin(), linked.end(), team) != linked.end())
                continue;
            linked.push_back(team);
        }
        for (quint32 team : linked) {
            claimed.insert(team);
            result.fieldingByTeam.insert(team, i);
        }
    }
    *out = std::move(result);
    return true;
}

} // namespace fm::fm27save
