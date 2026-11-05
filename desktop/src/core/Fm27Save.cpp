#include "Fm27Save.h"

#include "Fm27SaveClubs.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

namespace fm::fm27save {

namespace {

// --- Little-endian reads (bounds are checked by the callers) ---------------

inline quint32 u32(const char *p)
{
    quint32 v;
    std::memcpy(&v, p, 4);
    return v; // all supported targets are little-endian
}
inline quint16 u16(const char *p)
{
    quint16 v;
    std::memcpy(&v, p, 2);
    return v;
}
inline qint16 i16(const char *p)
{
    qint16 v;
    std::memcpy(&v, p, 2);
    return v;
}
inline quint64 u64(const char *p)
{
    quint64 v;
    std::memcpy(&v, p, 8);
    return v;
}

constexpr quint32 kMissing = 0xFFFFFFFFu;

// --- Container -----------------------------------------------------------------

const QByteArray kFileMagic("\x02\x01" "fmf.", 6);
constexpr int kHeaderSize = 26;
constexpr int kTrailerPointerAt = 9;
constexpr int kTrailerHeaderSize = 9;
constexpr int kFrameBase = 26;
constexpr quint64 kMaxTrailerBytes = 64ull * 1024 * 1024;
constexpr quint64 kMaxSectionBytes = 4ull * 1024 * 1024 * 1024;

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

// Decompresses one zstd frame of unknown size (the trailer), capped.
bool decompressUnknown(const QByteArray &in, QByteArray *out, quint64 cap)
{
    ZSTD_DStream *stream = ZSTD_createDStream();
    if (!stream)
        return false;
    ZSTD_initDStream(stream);
    QByteArray result;
    ZSTD_inBuffer input{in.constData(), size_t(in.size()), 0};
    std::vector<char> chunk(ZSTD_DStreamOutSize());
    size_t ret = 1;
    while (input.pos < input.size) {
        ZSTD_outBuffer output{chunk.data(), chunk.size(), 0};
        ret = ZSTD_decompressStream(stream, &output, &input);
        if (ZSTD_isError(ret)) {
            ZSTD_freeDStream(stream);
            return false;
        }
        result.append(chunk.data(), qsizetype(output.pos));
        if (quint64(result.size()) > cap) {
            ZSTD_freeDStream(stream);
            return false;
        }
        if (ret == 0)
            break;
    }
    ZSTD_freeDStream(stream);
    if (ret != 0)
        return false; // truncated frame
    *out = std::move(result);
    return true;
}

bool validEntryName(const QByteArray &name)
{
    for (char c : name) {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return !name.isEmpty();
}

bool validExtension(const QByteArray &ext)
{
    if (ext.size() != 4 || ext[0] != '.')
        return false;
    for (int i = 1; i < 4; ++i) {
        const char c = ext[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            return false;
    }
    return true;
}

} // namespace

bool Container::open(const QString &path, QString *error)
{
    m_path = path;
    m_entries.clear();
    const QString fileName = QFileInfo(path).fileName();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Could not open %1: %2").arg(fileName, file.errorString()));
    m_fileSize = file.size();
    const QByteArray header = file.read(kHeaderSize);
    if (!header.startsWith(kFileMagic))
        return fail(error, QStringLiteral("%1 is not a Football Manager save").arg(fileName));
    if (header.size() < kHeaderSize)
        return fail(error, QStringLiteral("%1 is too short to be a save").arg(fileName));
    const quint64 trailerOffset = kTrailerPointerAt + u64(header.constData() + kTrailerPointerAt);
    if (trailerOffset < quint64(kHeaderSize)
        || trailerOffset >= quint64(m_fileSize) - kTrailerHeaderSize
        || quint64(m_fileSize) - trailerOffset - kTrailerHeaderSize > kMaxTrailerBytes)
        return fail(error, QStringLiteral("%1: the save directory is missing or damaged. If the game "
                                          "was saving, wait for it to finish and try again.")
                               .arg(fileName));
    if (!file.seek(qint64(trailerOffset)))
        return fail(error, file.errorString());
    const QByteArray trailer = file.readAll();
    if (!trailer.startsWith(kFileMagic))
        return fail(error, QStringLiteral("%1: the save directory is damaged").arg(fileName));
    QByteArray payload;
    if (!decompressUnknown(trailer.mid(kTrailerHeaderSize), &payload, kMaxTrailerBytes))
        return fail(error, QStringLiteral("%1: the save directory could not be decompressed").arg(fileName));

    // Save name (u32 length + UTF-8), an unidentified u32, then the entries.
    const char *p = payload.constData();
    const qsizetype n = payload.size();
    if (n < 8)
        return fail(error, QStringLiteral("%1: the save directory is damaged").arg(fileName));
    const quint32 nameLength = u32(p);
    if (nameLength > 4096 || 4 + qsizetype(nameLength) + 4 > n)
        return fail(error, QStringLiteral("%1: the save directory is damaged").arg(fileName));
    m_saveName = QString::fromUtf8(p + 4, nameLength);
    qsizetype cursor = 4 + nameLength + 4;
    while (cursor + 4 <= n) {
        const quint32 entryNameLength = u32(p + cursor);
        if (entryNameLength < 1 || entryNameLength > 256)
            break;
        const qsizetype extLengthAt = cursor + 4 + entryNameLength;
        const qsizetype tailAt = extLengthAt + 8;
        if (tailAt + 40 > n)
            break;
        const QByteArray entryName(p + cursor + 4, entryNameLength);
        const QByteArray ext(p + extLengthAt + 4, 4);
        if (!validEntryName(entryName) || u32(p + extLengthAt) != 4 || !validExtension(ext))
            break;
        DirectoryEntry entry;
        entry.name = QString::fromLatin1(entryName);
        entry.extension = QString::fromLatin1(ext);
        entry.frameOffset = kFrameBase + u64(p + tailAt);
        entry.compressedSize = u64(p + tailAt + 8);
        entry.decompressedSize = u64(p + tailAt + 16);
        if (entry.frameOffset + entry.compressedSize > trailerOffset)
            return fail(error, QStringLiteral("%1: the save directory points past its data").arg(fileName));
        m_entries.append(entry);
        cursor = tailAt + 40;
        if (m_entries.size() > 100000)
            return fail(error, QStringLiteral("%1: the save directory is damaged").arg(fileName));
    }
    if (!hasSection(QStringLiteral("game_db")))
        return fail(error, QStringLiteral("%1: the save lists no game database").arg(fileName));
    return true;
}

bool Container::hasSection(const QString &name) const
{
    return std::any_of(m_entries.cbegin(), m_entries.cend(), [&](const DirectoryEntry &e) {
        return e.name == name && (e.extension == QLatin1String(".dat") || e.extension == QLatin1String(".cmt"));
    });
}

bool Container::readSection(const QString &name, QByteArray *out, QString *error) const
{
    const QString fileName = QFileInfo(m_path).fileName();
    const auto it = std::find_if(m_entries.cbegin(), m_entries.cend(), [&](const DirectoryEntry &e) {
        return e.name == name && (e.extension == QLatin1String(".dat") || e.extension == QLatin1String(".cmt"));
    });
    if (it == m_entries.cend())
        return fail(error, QStringLiteral("%1: section %2 is missing").arg(fileName, name));
    if (it->decompressedSize > kMaxSectionBytes || it->compressedSize > kMaxSectionBytes
        || it->decompressedSize > it->compressedSize * 64 + 64ull * 1024 * 1024)
        return fail(error, QStringLiteral("%1: section %2 is implausibly large").arg(fileName, name));
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly) || file.size() != m_fileSize)
        return fail(error, QStringLiteral("%1 changed while it was read. If the game was saving, wait "
                                          "for it to finish and try again.")
                               .arg(fileName));
    if (!file.seek(qint64(it->frameOffset)))
        return fail(error, file.errorString());
    const QByteArray compressed = file.read(qint64(it->compressedSize));
    if (quint64(compressed.size()) != it->compressedSize)
        return fail(error, QStringLiteral("%1: section %2 is truncated").arg(fileName, name));
    QByteArray data;
    data.resize(qsizetype(it->decompressedSize));
    const size_t got = ZSTD_decompress(data.data(), size_t(data.size()), compressed.constData(),
                                       size_t(compressed.size()));
    if (ZSTD_isError(got) || got != it->decompressedSize)
        return fail(error, QStringLiteral("%1: section %2 could not be decompressed").arg(fileName, name));
    *out = std::move(data);
    return true;
}

// --- Small sections --------------------------------------------------------------

namespace {

std::optional<QDate> decodeDate(const QByteArray &buf, qsizetype offset)
{
    if (offset < 0 || offset + 4 > buf.size())
        return std::nullopt;
    const quint16 packed = u16(buf.constData() + offset);
    const int year = u16(buf.constData() + offset + 2);
    const int dayOfYear = packed & 0x1FF;
    if (year < 1901 || year > 2200)
        return std::nullopt;
    const int days = QDate::isLeapYear(year) ? 366 : 365;
    if (dayOfYear < 1 || dayOfYear > days)
        return std::nullopt;
    return QDate(year, 1, 1).addDays(dayOfYear - 1);
}

// Reads u32 length + bytes at offset; returns the offset after it or -1.
qsizetype skipString(const QByteArray &buf, qsizetype offset, quint32 maxLength)
{
    if (offset < 0 || offset + 4 > buf.size())
        return -1;
    const quint32 length = u32(buf.constData() + offset);
    if (length > maxLength || offset + 4 + qsizetype(length) > buf.size())
        return -1;
    return offset + 4 + length;
}

QString findBuild(const QByteArray &summary)
{
    static const QRegularExpression pattern(
        QStringLiteral("^([1-9][0-9]{0,2})\\.([0-9]{1,3})\\.([0-9]{1,4})\\+([0-9]{1,10})$"));
    const char *p = summary.constData();
    for (qsizetype at = 4; at < summary.size(); ++at) {
        if (p[at] < '1' || p[at] > '9')
            continue;
        const quint32 length = u32(p + at - 4);
        if (length == 0 || length > 32 || at + qsizetype(length) > summary.size())
            continue;
        const QString candidate = QString::fromLatin1(p + at, length);
        if (pattern.match(candidate).hasMatch())
            return candidate;
    }
    return {};
}

// save_game_summary: setup string at 8, the build string, counted division
// names, an 8-byte manager header, manager and club names, the club's uid,
// then the displayed clock (fmsave read_summary_clock).
struct SummaryFacts {
    QString managerName;
    QString clubName;
    std::optional<QDate> clock;
};

QString readString(const QByteArray &buf, qsizetype *offset, quint32 maxLength)
{
    const qsizetype after = skipString(buf, *offset, maxLength);
    if (after < 0) {
        *offset = -1;
        return {};
    }
    const QString text = QString::fromUtf8(buf.constData() + *offset + 4, after - *offset - 4);
    *offset = after;
    return text;
}

SummaryFacts readSummary(const QByteArray &summary, const QString &build)
{
    SummaryFacts facts;
    qsizetype offset = 8;
    readString(summary, &offset, 65536);
    if (offset < 0 || readString(summary, &offset, 32) != build || offset + 4 > summary.size())
        return facts;
    const quint32 divisions = u32(summary.constData() + offset);
    if (divisions > 4096)
        return facts;
    offset += 4;
    for (quint32 i = 0; i < divisions && offset >= 0; ++i)
        readString(summary, &offset, 1024);
    if (offset < 0)
        return facts;
    offset += 8;
    facts.managerName = readString(summary, &offset, 1024);
    if (offset < 0)
        return facts;
    facts.clubName = readString(summary, &offset, 1024);
    if (offset < 0)
        return facts;
    facts.clock = decodeDate(summary, offset + 4);
    return facts;
}

// game_info: db version string at 8, a filename list whose count sits 167
// bytes after it, and the clock 174 bytes after the version plus that list.
std::optional<QDate> findGameDate(const QByteArray &gameInfo)
{
    const qsizetype versionEnd = skipString(gameInfo, 8, 64);
    if (versionEnd < 0)
        return std::nullopt;
    const qsizetype countAt = versionEnd + 167;
    if (countAt + 4 > gameInfo.size())
        return std::nullopt;
    const quint32 count = u32(gameInfo.constData() + countAt);
    if (count > 1000)
        return std::nullopt;
    const qsizetype stringsStart = countAt + 4;
    qsizetype cursor = stringsStart;
    for (quint32 i = 0; i < count; ++i) {
        cursor = skipString(gameInfo, cursor, 4096);
        if (cursor < 0)
            return std::nullopt;
    }
    return decodeDate(gameInfo, versionEnd + (cursor - stringsStart) + 174);
}

// --- Name pools ----------------------------------------------------------------------

struct NamePool {
    std::vector<qint64> lengthWordAt; // per entry: offset of its byte-length word

    QString nameAt(const QByteArray &db, quint32 index) const
    {
        if (index == kMissing || index >= lengthWordAt.size())
            return {};
        const qint64 at = lengthWordAt[index];
        const quint32 length = u32(db.constData() + at);
        if (length == 0 || at + 4 + qint64(length) > db.size())
            return {};
        return QString::fromUtf8(db.constData() + at + 4, length);
    }
};

struct NamePools {
    NamePool first, surnames, common;
    qint64 end = 0;
};

bool indexPool(const QByteArray &db, qint64 countAt, NamePool *pool, qint64 *after)
{
    const char *p = db.constData();
    const qint64 n = db.size();
    if (countAt + 4 > n)
        return false;
    const quint32 count = u32(p + countAt);
    qint64 position = countAt + 4;
    if (qint64(count) * 8 > n - position)
        return false;
    pool->lengthWordAt.clear();
    pool->lengthWordAt.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        if (position + 8 > n)
            return false;
        const quint32 id = u32(p + position);
        const quint32 length = u32(p + position + 4);
        if (id != i || length > 1024)
            return false;
        pool->lengthWordAt.push_back(position + 4);
        position += 8 + length;
    }
    if (position > n)
        return false;
    *after = position;
    return true;
}

bool locateNamePools(const QByteArray &db, NamePools *out)
{
    // Signature <6I 46421, 0, 1024, 256, 2048, 0>; the capacity words may grow
    // together by an integer scale and bytes 5..8 are not checked.
    static const quint32 words[6] = {46421, 0, 1024, 256, 2048, 0};
    QByteArray prefix(5, '\0');
    std::memcpy(prefix.data(), &words[0], 4); // byte 4 is the zero low byte of word 1
    const char *p = db.constData();
    bool found = false;
    qsizetype from = 0;
    while (true) {
        const qsizetype at = db.indexOf(prefix, from);
        if (at < 0)
            break;
        from = at + 1;
        if (at + 24 > db.size())
            continue;
        char stored[24];
        std::memcpy(stored, p + at, 24);
        for (int b = 5; b <= 8; ++b)
            stored[b] = 0;
        quint32 s[6];
        std::memcpy(s, stored, 24);
        if (s[3] % words[3] != 0)
            continue;
        const quint32 scale = s[3] / words[3];
        if (scale < 1 || scale > 1024 || s[0] != words[0] || s[1] != words[1]
            || s[2] != words[2] * scale || s[4] != words[4] * scale || s[5] != words[5])
            continue;
        NamePools pools;
        qint64 cursor = at + 24;
        if (!indexPool(db, cursor, &pools.first, &cursor) || !indexPool(db, cursor, &pools.surnames, &cursor)
            || !indexPool(db, cursor, &pools.common, &cursor))
            continue;
        if (found)
            return false; // ambiguous
        pools.end = cursor;
        *out = std::move(pools);
        found = true;
    }
    return found;
}

// --- Player records ----------------------------------------------------------------------

// Offsets relative to a record's anchor (fmsave PlayerRecordLayout, FM26 26.3.2 / FM27 27.0.0).
constexpr int kMarkerOffset = 102;
constexpr int kPindexOffset = -19;
constexpr int kUidOffset = -15;
constexpr int kUidCopyOffset = -11;
constexpr int kObjectKindOffset = -7;
constexpr int kHomeRepOffset = -6;
constexpr int kCurrentRepOffset = -4;
constexpr int kWorldRepOffset = -2;
constexpr int kCurrentAbilityOffset = 0;
constexpr int kPotentialAbilityOffset = 2;
constexpr int kReputationBucketOffset = 8;
constexpr int kTeamIdOffset = 16;
constexpr int kRatingsOffset = 24;
constexpr int kRatingsCount = 15;
constexpr int kAttributesOffset = 39;
constexpr int kAttributeBytes = 54;
constexpr int kLeftFootByte = 24;
constexpr int kRightFootByte = 25;
constexpr int kHeightOffset = 121;
constexpr int kDecodeExtent = 122;
constexpr quint32 kAllocatedUidMinimum = 2000000000u;
constexpr int kAttributeMax = 100;
constexpr int kEditedAttributeMax = 125;
constexpr int kAbilityMax = 200;
constexpr int kEditedAbilityMax = 255;
const QByteArray kMarker = QByteArray::fromHex("01006c07");

struct Candidate {
    qint64 offset;
    quint32 pindex;
    quint32 uid;
};

inline bool isPlayerKind(uint8_t kind) { return kind == 2 || kind == 3; }

bool patternAt(const uint8_t *b)
{
    for (int i = 0; i < kRatingsCount; ++i) {
        if (b[i] < 1 || b[i] > 20)
            return false;
    }
    for (int i = kRatingsCount; i < kRatingsCount + kAttributeBytes; ++i) {
        if (b[i] < 1 || b[i] > kEditedAttributeMax)
            return false;
    }
    return true;
}

std::optional<Candidate> acceptCandidate(const QByteArray &db, qint64 rec, bool checkPattern)
{
    const qint64 n = db.size();
    if (rec + kPindexOffset < 0 || rec + kAttributesOffset + kAttributeBytes > n)
        return std::nullopt;
    const char *p = db.constData() + rec;
    const auto *ub = reinterpret_cast<const uint8_t *>(p);
    const quint32 uid = u32(p + kUidOffset);
    const quint32 uidCopy = u32(p + kUidCopyOffset);
    if (uid == 0 || uid == kMissing || uidCopy == 0 || uidCopy == kMissing)
        return std::nullopt;
    const uint8_t kind = ub[kObjectKindOffset];
    if (uid != uidCopy && (uid < kAllocatedUidMinimum || !isPlayerKind(kind)))
        return std::nullopt;
    const int ca = u16(p + kCurrentAbilityOffset);
    const int pa = i16(p + kPotentialAbilityOffset);
    int highestAbility = kAbilityMax;
    int maxAttr = 0;
    for (int i = 0; i < kAttributeBytes; ++i)
        maxAttr = std::max<int>(maxAttr, ub[kAttributesOffset + i]);
    if (ca > kAbilityMax || pa > kAbilityMax || maxAttr > kAttributeMax) {
        if (uid != uidCopy || !isPlayerKind(kind))
            return std::nullopt;
        highestAbility = kEditedAbilityMax;
    }
    if (ca < 0 || ca > highestAbility || pa < -10 || pa > highestAbility)
        return std::nullopt;
    const int bucket = ub[kReputationBucketOffset];
    if (bucket > 200)
        return std::nullopt;
    if (checkPattern && !patternAt(ub + kRatingsOffset))
        return std::nullopt;
    return Candidate{rec, u32(p + kPindexOffset), uid};
}

bool locatePlayerRecords(const QByteArray &db, qint64 namePoolsEnd, std::vector<Candidate> *out,
                         QString *error)
{
    const qint64 n = db.size();
    std::vector<Candidate> candidates;
    QSet<qint64> markerOffsets;
    // Marker pass.
    qsizetype hit = db.indexOf(kMarker, 0);
    while (hit >= 0) {
        const qint64 rec = hit - kMarkerOffset;
        if (const auto c = acceptCandidate(db, rec, true)) {
            if (rec + kDecodeExtent > n)
                return fail(error, QStringLiteral("a player record runs past the end of the database"));
            candidates.push_back(*c);
            markerOffsets.insert(rec);
        }
        hit = db.indexOf(kMarker, hit + 1);
    }
    // Completeness pass: 15 ratings then 54 attribute bytes, in runs of bytes
    // that could belong to such a block, non-overlapping like a regex scan.
    const auto *b = reinterpret_cast<const uint8_t *>(db.constData());
    constexpr int runLength = kRatingsCount + kAttributeBytes;
    qint64 position = std::max<qint64>(namePoolsEnd, 0);
    while (position < n) {
        // Find the next maximal run of bytes in [1, 125] at least runLength long.
        while (position < n && (b[position] < 1 || b[position] > kEditedAttributeMax))
            ++position;
        qint64 runEnd = position;
        while (runEnd < n && b[runEnd] >= 1 && b[runEnd] <= kEditedAttributeMax)
            ++runEnd;
        if (runEnd - position >= runLength) {
            qint64 at = position;
            while (at + runLength <= runEnd) {
                if (patternAt(b + at)) {
                    const qint64 rec = at - kRatingsOffset;
                    if (!markerOffsets.contains(rec)) {
                        if (const auto c = acceptCandidate(db, rec, false)) {
                            if (rec + kDecodeExtent > n)
                                return fail(error, QStringLiteral("a player record runs past the end of the database"));
                            candidates.push_back(*c);
                        }
                    }
                    at += runLength;
                } else {
                    ++at;
                }
            }
        }
        position = runEnd;
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &c) { return a.offset < c.offset; });
    QSet<quint32> pindexes, uids;
    for (const Candidate &c : candidates) {
        if (pindexes.contains(c.pindex) || uids.contains(c.uid))
            return fail(error, QStringLiteral("a player appears in two records; the save layout differs from the known builds"));
        pindexes.insert(c.pindex);
        uids.insert(c.uid);
    }
    if (candidates.empty())
        return fail(error, QStringLiteral("no player records found; the save layout differs from the known builds"));
    *out = std::move(candidates);
    return true;
}

// The database Unique ID: the doubled id of the unique header that carries
// pindex + 1 and closes this person's object (fmsave closing_person_header).
std::optional<std::pair<qint64, quint32>> closingHeader(const QByteArray &db, quint32 pindex,
                                                        qint64 searchStart, qint64 searchEnd,
                                                        quint32 uidFloor,
                                                        std::optional<quint32> uidCeiling)
{
    if (pindex >= kMissing)
        return std::nullopt;
    const quint32 needleValue = pindex + 1;
    const qint64 end = std::min<qint64>(searchEnd, db.size() - 8); // a hit needs 12 bytes
    std::optional<std::pair<qint64, quint32>> found;
    const char *base = db.constData();
    const char *cursor = base + std::max<qint64>(searchStart, 0);
    const char *limit = base + end; // the needle must end at or before searchEnd
    const char *needleBytes = reinterpret_cast<const char *>(&needleValue);
    while (cursor + 4 <= limit) {
        cursor = std::search(cursor, limit, needleBytes, needleBytes + 4);
        if (cursor + 4 > limit)
            break;
        const qint64 hit = cursor - base;
        if (hit + 12 <= db.size()) {
            const quint32 uid = u32(base + hit + 4);
            const quint32 uidCopy = u32(base + hit + 8);
            if (uid == uidCopy && uid != kMissing && uid > uidFloor && (!uidCeiling || uid <= *uidCeiling)) {
                if (found)
                    return std::nullopt;
                found = std::make_pair(hit, uid);
            }
        }
        ++cursor;
    }
    return found;
}

// --- Person blocks (FM27 offsets from the birth date) ---------------------------------------

constexpr int kPersonWindowStart = 100;
constexpr int kPersonalityFromBirth = 22;
constexpr int kZeroFromBirth = kPersonalityFromBirth - 1;
constexpr int kDateZeroFromBirth = 15;
constexpr int kDateZeroCount = 7;
constexpr int kNationFromBirth = 14;
constexpr int kLegalNameFromBlockStart = 19;
constexpr int kTraitBitsFromBlockStart = -8;
constexpr quint32 kNameIdLimit = 1u << 23;

struct PersonFields {
    QString name;
    QDate birth;
    int age = -1;
    quint16 nation = 0;
    std::array<uint8_t, SavePersonalityCount> personality{};
};

class PersonDecoder {
public:
    PersonDecoder(const QByteArray &db, const NamePools &pools, QDate clock)
        : m_db(db), m_pools(pools), m_clock(clock) {}

    std::optional<PersonFields> decode(qint64 windowStart, qint64 windowEnd)
    {
        const auto *b = reinterpret_cast<const uint8_t *>(m_db.constData());
        windowEnd = std::min<qint64>(windowEnd, m_db.size());
        std::optional<PersonFields> selected;
        const qint64 last = windowEnd - (SavePersonalityCount + 1);
        for (qint64 zero = std::max<qint64>(windowStart + kZeroFromBirth, 0); zero <= last; ++zero) {
            if (b[zero] != 0)
                continue;
            bool run = true;
            for (int i = 1; i <= SavePersonalityCount; ++i) {
                if (b[zero + i] < 1 || b[zero + i] > 20) {
                    run = false;
                    break;
                }
            }
            if (!run)
                continue;
            auto person = tryCandidate(zero - kZeroFromBirth);
            if (!person)
                continue;
            if (selected)
                return std::nullopt; // FM27 requires a unique candidate
            selected = std::move(person);
        }
        return selected;
    }

private:
    std::optional<PersonFields> tryCandidate(qint64 birth)
    {
        const char *p = m_db.constData();
        const qint64 n = m_db.size();
        if (birth < 4 || birth + kPersonalityFromBirth + SavePersonalityCount > n)
            return std::nullopt;
        const quint32 raw = u32(p + birth);
        const quint32 packed = raw & 0xFFFF;
        const int year = int(raw >> 16);
        if ((packed >> 9) != 0)
            return std::nullopt;
        const int dayOfYear = int(packed & 0x1FF);
        if (dayOfYear < 1 || dayOfYear > 366 || year < 1901 || year > m_clock.year())
            return std::nullopt;
        for (int i = 0; i < kDateZeroCount; ++i) {
            if (p[birth + kDateZeroFromBirth + i] != 0)
                return std::nullopt;
        }
        int legalLength = 0;
        if (u32(p + birth - 4) != 0) {
            legalLength = -1;
            for (int candidate = 2; candidate <= 80; ++candidate) {
                const qint64 at = birth - 4 - candidate;
                if (at < 0)
                    return std::nullopt;
                if (u32(p + at) == quint32(candidate)) {
                    legalLength = candidate;
                    break;
                }
            }
            if (legalLength < 0)
                return std::nullopt;
        }
        const qint64 blockStart = birth - kLegalNameFromBlockStart - legalLength;
        const qint64 nameBlock = blockStart + kTraitBitsFromBlockStart;
        if (nameBlock < 0)
            return std::nullopt;
        // Q trait bits, then first/surname/common name ids, each followed by a zero byte.
        const quint32 firstId = u32(p + nameBlock + 8);
        const quint32 surnameId = u32(p + nameBlock + 13);
        const quint32 commonId = u32(p + nameBlock + 18);
        if (p[nameBlock + 12] != 0 || p[nameBlock + 17] != 0 || p[nameBlock + 22] != 0)
            return std::nullopt;
        for (quint32 id : {firstId, surnameId, commonId}) {
            if (id != kMissing && id >= kNameIdLimit)
                return std::nullopt;
        }

        PersonFields person;
        if (const auto date = decodeDate(m_db, birth)) {
            person.birth = *date;
            person.age = m_clock.year() - date->year()
                         - ((m_clock.month() < date->month()
                             || (m_clock.month() == date->month() && m_clock.day() < date->day()))
                                ? 1
                                : 0);
        }
        person.nation = u16(p + birth + kNationFromBirth);
        for (int i = 0; i < SavePersonalityCount; ++i)
            person.personality[i] = uint8_t(p[birth + kPersonalityFromBirth + i]);

        QString common;
        if (commonId != kMissing) {
            common = m_pools.common.nameAt(m_db, commonId);
            if (common.isEmpty())
                common = m_pools.first.nameAt(m_db, commonId);
            if (common.isEmpty())
                common = m_pools.surnames.nameAt(m_db, commonId);
        }
        const QString first = m_pools.first.nameAt(m_db, firstId);
        const QString last = m_pools.surnames.nameAt(m_db, surnameId);
        if (!common.isEmpty())
            person.name = common;
        else if (!first.isEmpty() && !last.isEmpty())
            person.name = first + QLatin1Char(' ') + last;
        else if (!first.isEmpty())
            person.name = first;
        else if (!last.isEmpty())
            person.name = last;
        else if (legalLength > 0)
            person.name = QString::fromUtf8(p + blockStart + kLegalNameFromBlockStart, legalLength);
        return person;
    }

    const QByteArray &m_db;
    const NamePools &m_pools;
    QDate m_clock;
};

inline uint8_t scale(uint8_t raw)
{
    return uint8_t(std::max(1, (int(raw) + 2) / 5));
}

} // namespace

bool parseFogOfWar(const QByteArray &section, QHash<quint32, int> *out)
{
    if (section.size() < 21 || !section.startsWith(QByteArray("\x03\x01" "tad.", 6)))
        return false;
    const quint32 count = u32(section.constData() + 17);
    if (21 + qint64(count) * 5 != section.size())
        return false;
    out->clear();
    out->reserve(qsizetype(count));
    for (quint32 i = 0; i < count; ++i) {
        const char *entry = section.constData() + 21 + qint64(i) * 5;
        const int knowledge = uint8_t(entry[0]);
        const quint32 reference = u32(entry + 1);
        if (reference == 0 || knowledge > 100)
            return false;
        out->insert(reference - 1, knowledge);
    }
    return true;
}

bool isKnownBuild(const QString &build)
{
    static const QStringList known = {QStringLiteral("27.0.0+2486695"), QStringLiteral("27.0.0+2493426")};
    return known.contains(build);
}

bool readSave(const QString &path, SaveData *out, QString *error, const ProgressCallback &progress)
{
    const auto report = [&](int percent) {
        if (progress)
            progress(percent);
    };
    const QString fileName = QFileInfo(path).fileName();
    Container container;
    if (!container.open(path, error))
        return false;
    report(2);

    SaveData data;
    QByteArray summary;
    if (container.readSection(QStringLiteral("save_game_summary"), &summary, nullptr))
        data.build = findBuild(summary);
    if (!data.build.startsWith(QLatin1String("27.")))
        return fail(error, QStringLiteral("%1 is not a Football Manager 2027 save (version %2)")
                               .arg(fileName, data.build.isEmpty() ? QStringLiteral("unknown") : data.build));

    const SummaryFacts facts = readSummary(summary, data.build);
    data.managerName = facts.managerName;
    data.clubName = facts.clubName;
    std::optional<QDate> clock = facts.clock;
    if (!clock) {
        QByteArray gameInfo;
        if (!container.readSection(QStringLiteral("game_info"), &gameInfo, error))
            return false;
        clock = findGameDate(gameInfo);
    }
    if (!clock)
        return fail(error, QStringLiteral("%1: the game date was not found").arg(fileName));
    data.gameDate = *clock;

    QByteArray fog;
    if (container.readSection(QStringLiteral("fog_of_war"), &fog, nullptr))
        data.knowledgeFound = parseFogOfWar(fog, &data.knowledgeByPindex);
    report(5);

    QByteArray db;
    if (!container.readSection(QStringLiteral("game_db"), &db, error))
        return false;
    report(25);

    NamePools pools;
    if (!locateNamePools(db, &pools))
        return fail(error, QStringLiteral("%1: the name tables were not found").arg(fileName));
    std::vector<Candidate> records;
    if (!locatePlayerRecords(db, pools.end, &records, error)) {
        if (error)
            *error = fileName + QStringLiteral(": ") + *error;
        return false;
    }
    report(45);

    ClubTeams clubs;
    QString clubError;
    const bool clubsOk = readClubTeams(db, &clubs, &clubError);

    PersonDecoder persons(db, pools, data.gameDate);
    const char *p = db.constData();
    const auto *ub = reinterpret_cast<const uint8_t *>(p);
    data.players.reserve(qsizetype(records.size()));
    for (size_t i = 0; i < records.size(); ++i) {
        const Candidate &c = records[i];
        const bool lastRecord = i + 1 == records.size();
        const qint64 windowEnd = lastRecord ? db.size() : records[i + 1].offset;
        SavePlayer player;
        player.uid = c.uid;
        player.pindex = c.pindex;
        player.recordOffset = c.offset;
        const char *r = p + c.offset;
        player.homeReputation = u16(r + kHomeRepOffset);
        player.currentReputation = u16(r + kCurrentRepOffset);
        player.worldReputation = u16(r + kWorldRepOffset);
        player.teamId = u32(r + kTeamIdOffset);
        for (int k = 0; k < kRatingsCount; ++k)
            player.positions[k] = ub[c.offset + kRatingsOffset + k];
        int out54 = 0;
        for (int k = 0; k < kAttributeBytes; ++k) {
            const uint8_t raw = ub[c.offset + kAttributesOffset + k];
            if (k == kLeftFootByte)
                player.leftFoot = scale(raw);
            else if (k == kRightFootByte)
                player.rightFoot = scale(raw);
            else
                player.attributes[out54++] = scale(raw);
        }
        player.heightCm = ub[c.offset + kHeightOffset];

        const std::optional<quint32> nextUid =
            lastRecord ? std::nullopt : std::optional<quint32>(u32(p + windowEnd + kUidOffset));
        const auto closing = closingHeader(db, c.pindex, c.offset + kDecodeExtent, windowEnd, c.uid, nextUid);
        if (closing)
            player.uniqueId = closing->second;
        const qint64 personEnd = closing ? closing->first : windowEnd;
        if (const auto person = persons.decode(c.offset + kPersonWindowStart, personEnd)) {
            player.personFound = true;
            player.name = person->name;
            player.birthDate = person->birth;
            player.age = person->age;
            player.nationId = person->nation;
            player.personality = person->personality;
        }
        if (clubsOk && player.teamId != kMissing) {
            if (const ClubInfo *club = clubs.clubForTeam(player.teamId)) {
                player.clubUid = club->uid;
                player.clubUniqueId = club->uniqueId;
                player.clubName = club->name;
                player.clubShortName = club->shortName;
            }
            if (const ClubInfo *fielding = clubs.fieldingClubForTeam(player.teamId))
                player.fieldingClubUid = fielding->uid;
        }
        data.players.append(std::move(player));
        if (i % 4096 == 0)
            report(45 + int(50 * double(i) / double(records.size())));
    }
    // A patch that moved fields shows up as players without a person block,
    // Unique ID or club: refuse rather than import wrong data.
    qsizetype withPerson = 0, withUniqueId = 0, withClub = 0;
    for (const SavePlayer &player : data.players) {
        withPerson += player.personFound;
        withUniqueId += player.uniqueId != 0;
        withClub += player.clubUid != 0;
    }
    const double count = double(data.players.size());
    if (!clubsOk || withPerson < 0.95 * count || withUniqueId < 0.95 * count || withClub < 0.5 * count)
        return fail(error, QStringLiteral("%1: the save layout of build %2 differs from the known builds "
                                          "(players %3, names %4, ids %5, clubs %6%7)")
                               .arg(fileName, data.build)
                               .arg(data.players.size())
                               .arg(withPerson)
                               .arg(withUniqueId)
                               .arg(withClub)
                               .arg(clubsOk ? QString() : QStringLiteral("; ") + clubError));
    report(100);
    *out = std::move(data);
    return true;
}

} // namespace fm::fm27save
