#include "HtmlImporter.h"

#include "Attributes.h"
#include "Constants.h"
#include "Database.h"
#include "LegacyMigrator.h"
#include "Utils.h"

#include <QFile>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <iterator>

namespace fm {

namespace {

constexpr int kUpsertBatchSize = 2000;

// Columns FM routinely exports that the app intentionally ignores (legacy
// IGNORED_EXPORT_COLUMNS) — listed so the unknown-column warning does not cry
// wolf on every normal export.
const QSet<QString> &ignoredExportColumns()
{
    static const QSet<QString> ignored = {
        QStringLiteral("CON"), QStringLiteral("Ability"),
        QStringLiteral("Position/Role/Duty"), QStringLiteral("Pun")};
    return ignored;
}

// Decodes the handful of HTML entities FM exports actually contain.
QString decodeEntities(const QString &text)
{
    if (!text.contains(QLatin1Char('&')))
        return text;

    QString out;
    out.reserve(text.size());
    int i = 0;
    const int n = text.size();
    while (i < n) {
        const QChar ch = text.at(i);
        if (ch != QLatin1Char('&')) {
            out.append(ch);
            ++i;
            continue;
        }
        const int semi = text.indexOf(QLatin1Char(';'), i + 1);
        if (semi < 0 || semi - i > 10) { // not an entity
            out.append(ch);
            ++i;
            continue;
        }
        const QString entity = text.mid(i + 1, semi - i - 1);
        if (entity == QLatin1String("amp")) {
            out.append(QLatin1Char('&'));
        } else if (entity == QLatin1String("lt")) {
            out.append(QLatin1Char('<'));
        } else if (entity == QLatin1String("gt")) {
            out.append(QLatin1Char('>'));
        } else if (entity == QLatin1String("quot")) {
            out.append(QLatin1Char('"'));
        } else if (entity == QLatin1String("apos")) {
            out.append(QLatin1Char('\''));
        } else if (entity == QLatin1String("nbsp")) {
            out.append(QLatin1Char(' '));
        } else if (entity.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const uint code = entity.startsWith(QLatin1String("#x"), Qt::CaseInsensitive)
                                  ? entity.mid(2).toUInt(&ok, 16)
                                  : entity.mid(1).toUInt(&ok, 10);
            if (ok && code > 0)
                out.append(QChar(code));
            else
                out.append(text.mid(i, semi - i + 1));
        } else {
            out.append(text.mid(i, semi - i + 1));
            i = semi + 1;
            continue;
        }
        i = semi + 1;
    }
    return out;
}

// Text content of an HTML fragment: nested tags stripped, entities decoded,
// whitespace trimmed.
QString cellText(QStringView html, qsizetype from, qsizetype to)
{
    const QStringView raw = html.sliced(from, to - from);
    // Fast path — virtually every FM cell is plain text ("15", a name, ...):
    // no tags, no entities -> one trimmed copy instead of three strings.
    if (!raw.contains(u'<') && !raw.contains(u'&'))
        return raw.trimmed().toString();

    QString out;
    out.reserve(raw.size());
    bool inTag = false;
    for (const QChar ch : raw) {
        if (inTag) {
            if (ch == QLatin1Char('>'))
                inTag = false;
        } else if (ch == QLatin1Char('<')) {
            inTag = true;
        } else {
            out.append(ch);
        }
    }
    return decodeEntities(out).trimmed();
}

// ASCII lower-casing for tag-name comparison (tag names are ASCII).
inline char16_t asciiLower(QChar ch)
{
    const char16_t c = ch.unicode();
    return (c >= u'A' && c <= u'Z') ? char16_t(c + (u'a' - u'A')) : c;
}

// html[at...] starts with the lowercase ASCII word `name`, case-insensitively.
bool startsWithCi(QStringView html, qsizetype at, QLatin1StringView name)
{
    if (at < 0 || at + name.size() > html.size())
        return false;
    for (qsizetype i = 0; i < name.size(); ++i) {
        if (asciiLower(html[at + i]) != char16_t(name[i].unicode()))
            return false;
    }
    return true;
}

inline bool isTagBoundary(QChar ch)
{
    return ch == QLatin1Char('>') || ch.isSpace() || ch == QLatin1Char('/');
}

// Finds the next "<name" markup within [pos, end): the index of its '<', or -1.
// With requireBoundary the name must be followed (still before `end`) by '>',
// whitespace or '/', so "<th" does not match "<thead". Scans '<' to '<' with a
// plain character search — far cheaper than a case-insensitive substring
// search per candidate — and never looks past `end` (unbounded searches for a
// tag that does not recur made large exports O(n²) once, see v1.1.0).
qsizetype findMarkup(QStringView html, QLatin1StringView name, qsizetype pos, qsizetype end,
                     bool requireBoundary)
{
    const QStringView scope = html.first(end);
    while (pos < end) {
        const qsizetype lt = scope.indexOf(u'<', pos);
        if (lt < 0)
            return -1;
        const qsizetype after = lt + 1 + name.size();
        if (startsWithCi(html, lt + 1, name)
            && (!requireBoundary || (after < end && isTagBoundary(html[after])))) {
            return lt;
        }
        pos = lt + 1;
    }
    return -1;
}

qsizetype findTag(QStringView html, QLatin1StringView name, qsizetype pos, qsizetype end)
{
    return findMarkup(html, name, pos, end, /*requireBoundary*/ true);
}

// Next table cell tag ("<td" or "<th") within [pos, end), in ONE scan (instead
// of one search per tag type); *isHeader tells which one.
qsizetype findCell(QStringView html, qsizetype pos, qsizetype end, bool *isHeader)
{
    const QStringView scope = html.first(end);
    while (pos < end) {
        const qsizetype lt = scope.indexOf(u'<', pos);
        if (lt < 0)
            return -1;
        const qsizetype after = lt + 3;
        if (after < end && asciiLower(html[lt + 1]) == u't') {
            const char16_t kind = asciiLower(html[lt + 2]);
            if ((kind == u'd' || kind == u'h') && isTagBoundary(html[after])) {
                *isHeader = kind == u'h';
                return lt;
            }
        }
        pos = lt + 1;
    }
    return -1;
}

// True if the player uid is a newgen id ("r-" prefix).
bool isNewgenUid(const QString &uid)
{
    return uid.startsWith(QLatin1String("r-"));
}

} // namespace

bool HtmlImporter::extractTable(const QString &html, HtmlTable *out, QString *errorOut,
                                const std::function<void(int, int)> &progress)
{
    const auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };

    const QStringView view(html);
    const qsizetype htmlSize = view.size();
    const qsizetype tableStart = findTag(view, QLatin1StringView("table"), 0, htmlSize);
    if (tableStart < 0)
        return fail(QStringLiteral("No <table> element found in file."));
    qsizetype tableEnd = findMarkup(view, QLatin1StringView("/table"), tableStart, htmlSize,
                                    /*requireBoundary*/ false);
    if (tableEnd < 0)
        tableEnd = htmlSize;

    out->headers.clear();
    out->rows.clear();
    out->malformedRows = 0;

    qsizetype pos = tableStart;
    bool haveHeader = false;
    int rowCounter = 0;
    while (true) {
        const qsizetype rowStart = findTag(view, QLatin1StringView("tr"), pos, tableEnd);
        if (rowStart < 0 || rowStart >= tableEnd)
            break;
        // The row ends at "</tr" (no boundary check), or at the table end.
        qsizetype rowEnd = findMarkup(view, QLatin1StringView("/tr"), rowStart, tableEnd,
                                      /*requireBoundary*/ false);
        if (rowEnd < 0)
            rowEnd = tableEnd;

        QStringList cells;
        if (haveHeader)
            cells.reserve(out->headers.size());
        qsizetype cellPos = rowStart;
        bool headerRow = false;
        while (true) {
            bool isTh = false;
            const qsizetype cellStart = findCell(view, cellPos, rowEnd, &isTh);
            if (cellStart < 0)
                break;

            const qsizetype contentStart = view.indexOf(u'>', cellStart);
            if (contentStart < 0 || contentStart >= rowEnd)
                break;
            qsizetype contentEnd =
                findMarkup(view, isTh ? QLatin1StringView("/th") : QLatin1StringView("/td"),
                           contentStart, rowEnd, /*requireBoundary*/ false);
            if (contentEnd < 0) {
                // Unclosed cell: ends at the next cell or the row end.
                bool nextIsTh = false;
                const qsizetype next = findCell(view, contentStart + 1, rowEnd, &nextIsTh);
                contentEnd = next >= 0 ? next : rowEnd;
            }
            cells << cellText(view, contentStart + 1, contentEnd);
            if (isTh)
                headerRow = true;
            cellPos = contentEnd;
        }

        // Report parse progress by byte position every few thousand rows so a
        // very large export does not look frozen while it is being scanned.
        if (progress && (++rowCounter & 0x1FFF) == 0)
            progress(static_cast<int>(rowEnd), static_cast<int>(tableEnd));

        if (!haveHeader) {
            // The first row must be the header row (legacy takes trs[0] <th>s).
            if (!headerRow || cells.isEmpty())
                return fail(QStringLiteral("No <th> elements found in header row."));
            out->headers = cells;
            haveHeader = true;
        } else if (!cells.isEmpty()) {
            if (cells.size() == out->headers.size())
                out->rows.append(std::move(cells));
            else
                ++out->malformedRows;
        }

        pos = rowEnd + 1;
    }

    if (!haveHeader)
        return fail(QStringLiteral("No header row found in table."));
    return true;
}

void HtmlImporter::applyColumn(Player &player, const QString &fullColumnName, const QString &value)
{
    if (fullColumnName == QLatin1String("Name")) {
        player.name = value;
    } else if (fullColumnName == QLatin1String("Age")) {
        player.age = value.toInt();
    } else if (fullColumnName == QLatin1String("Club")) {
        player.club = value;
    } else if (fullColumnName == QLatin1String("Nationality")) {
        player.nationality = value;
    } else if (fullColumnName == QLatin1String("Second Nationality")) {
        player.secondNationality = value;
    } else if (fullColumnName == QLatin1String("Position")) {
        player.positionRaw = value;
    } else if (fullColumnName == QLatin1String("Personality")) {
        player.personality = value;
    } else if (fullColumnName == QLatin1String("Media Handling")) {
        player.mediaHandling = value;
    } else if (fullColumnName == QLatin1String("Agreed Playing Time")) {
        player.agreedPlayingTime = value;
    } else if (fullColumnName == QLatin1String("Wage")) {
        player.wageRaw = value;
    } else if (fullColumnName == QLatin1String("Transfer Value")) {
        player.transferValueRaw = value;
        player.transferValue = valueToFloat(value);
    } else if (fullColumnName == QLatin1String("Average Rating")) {
        bool ok = false;
        const double v = value.toDouble(&ok);
        player.averageRating = ok ? v : 0.0;
    } else if (fullColumnName == QLatin1String("Height")) {
        player.heightRaw = value;
        player.heightCm = LegacyMigrator::parseHeightCm(value);
    } else if (fullColumnName == QLatin1String("Left Foot")) {
        player.leftFoot = value;
    } else if (fullColumnName == QLatin1String("Right Foot")) {
        player.rightFoot = value;
    } else if (fullColumnName == QLatin1String("Preferred Foot")) {
        player.preferredFoot = value;
    } else {
        const int attrIndex = attrIndexByName(fullColumnName);
        if (attrIndex >= 0) {
            int lo = 0, hi = 0;
            LegacyMigrator::parseAttrValue(value, &lo, &hi);
            player.attrLo[attrIndex] = static_cast<uint8_t>(lo);
            player.attrHi[attrIndex] = static_cast<uint8_t>(hi);
        }
        // "Registration"/"Information" and unknown columns are ignored.
    }
}

ImportResult HtmlImporter::importHtml(const QString &html, Database &db,
                                      const std::vector<Player> &existingPlayers,
                                      std::function<void(int, int)> progress,
                                      const QString &fmVersionId,
                                      const std::function<void(int, int)> &parseProgress,
                                      std::vector<Player> *updatedPlayers)
{
    HtmlTable table;
    QString parseError;
    if (!extractTable(html, &table, &parseError, parseProgress)) {
        ImportResult result;
        result.error = parseError;
        return result;
    }
    return importTable(table, db, existingPlayers, progress, fmVersionId, updatedPlayers);
}

ImportResult HtmlImporter::importTable(const HtmlTable &table, Database &db,
                                       const std::vector<Player> &existingPlayers,
                                       const std::function<void(int, int)> &progress,
                                       const QString &fmVersionId,
                                       std::vector<Player> *updatedPlayers)
{
    ImportResult result;
    const QHash<QString, QString> &mapping = attributeMapping(fmVersionId);

    // Every full import bumps the per-database upload counter; each imported
    // player is stamped with the new value so data-freshness can be derived.
    const int newCounter =
        db.setting(QStringLiteral("update_counter"), QStringLiteral("0")).toInt() + 1;

    if (table.headers.size() < 10) {
        result.error = QStringLiteral("Too few columns (%1), expected at least 10.")
                           .arg(table.headers.size());
        return result;
    }

    // --- Deduplicate headers: only the FIRST occurrence of a name is used
    // (legacy renames later duplicates and drops them). ---
    QSet<QString> seenHeaders;
    std::vector<bool> useColumn(static_cast<size_t>(table.headers.size()), true);
    int uidCol = -1, nameCol = -1;
    for (int i = 0; i < table.headers.size(); ++i) {
        const QString &header = table.headers.at(i);
        if (seenHeaders.contains(header)) {
            useColumn[static_cast<size_t>(i)] = false;
            continue;
        }
        seenHeaders.insert(header);
        if (header == QLatin1String("UID"))
            uidCol = i;
        else if (header == QLatin1String("Name"))
            nameCol = i;
    }
    if (uidCol < 0) {
        result.error = QStringLiteral("Required column 'UID' not found.");
        return result;
    }
    if (nameCol < 0) {
        result.error = QStringLiteral("Required column 'Name' not found.");
        return result;
    }

    // Unknown-column warning (legacy: not in attribute_mapping ∪ {UID} ∪ ignored).
    for (const QString &header : table.headers) {
        if (!mapping.contains(header) && header != QLatin1String("UID")
            && !ignoredExportColumns().contains(header)) {
            if (!result.unknownColumns.contains(header))
                result.unknownColumns << header;
        }
    }

    result.malformedRows = table.malformedRows;

    // --- UID sanity: drop empty UIDs, deduplicate (keep LAST occurrence). ---
    struct RowRef {
        int rowIndex;
        QString uid; // trimmed; corrected in the unification step
    };
    QHash<QString, int> rowByUid; // uid -> index into rowRefs
    std::vector<RowRef> rowRefs;
    rowRefs.reserve(static_cast<size_t>(table.rows.size()));

    for (int r = 0; r < table.rows.size(); ++r) {
        const QString uid = table.rows.at(r).at(uidCol).trimmed();
        if (uid.isEmpty()) {
            ++result.emptyUidRows;
            continue;
        }
        const auto it = rowByUid.constFind(uid);
        if (it != rowByUid.constEnd()) {
            const QString dupName = table.rows.at(rowRefs[it.value()].rowIndex).at(nameCol).trimmed();
            if (!result.duplicateUidNames.contains(dupName))
                result.duplicateUidNames << dupName;
            rowRefs[it.value()].rowIndex = r; // keep the last occurrence
            continue;
        }
        rowByUid.insert(uid, static_cast<int>(rowRefs.size()));
        rowRefs.push_back({r, uid});
    }

    if (rowRefs.empty()) {
        result.error = QStringLiteral("No importable rows found (all rows were missing a UID).");
        return result;
    }
    result.rowsParsed = static_cast<int>(rowRefs.size());

    // --- Lookup maps over the existing database state. ---
    QHash<QString, int> existingByUid; // uid -> index into existingPlayers
    QHash<QString, QString> numericIdToName, rIdToName;
    for (size_t i = 0; i < existingPlayers.size(); ++i) {
        const Player &p = existingPlayers[i];
        existingByUid.insert(p.uid, static_cast<int>(i));
        if (isNewgenUid(p.uid))
            rIdToName.insert(p.uid, p.name);
        else
            numericIdToName.insert(p.uid, p.name);
    }

    // Fill-empty merge of app-managed fields (legacy merge_player_records).
    const auto mergeAppManaged = [](Player &good, const Player &bad) {
        if (good.assignedRoles.isEmpty())
            good.assignedRoles = bad.assignedRoles;
        if (good.primaryRole.isEmpty())
            good.primaryRole = bad.primaryRole;
        if (good.naturalPositions.isEmpty())
            good.naturalPositions = bad.naturalPositions;
        if (good.preferredSide.isEmpty())
            good.preferredSide = bad.preferredSide;
        if (good.agreedPlayingTime.isEmpty())
            good.agreedPlayingTime = bad.agreedPlayingTime;
    };

    // uid -> merged-away duplicate whose app-managed data must be folded in.
    QHash<QString, Player> mergedSources;
    QList<int> removedIds; // ids of the merged-away duplicates (deleted rows)

    // Everything from here on — ID unification, every player batch and the
    // upload counter — is ONE transaction: an error anywhere (early return)
    // rolls the whole import back instead of leaving a partial one behind.
    ScopedTransaction transaction(db);
    if (!transaction.isActive()) {
        result.error = db.errorString();
        return result;
    }

    // --- ID-unification engine (legacy scenarios 1 + 2). ---
    for (RowRef &ref : rowRefs) {
        const QString incomingName = table.rows.at(ref.rowIndex).at(nameCol).trimmed();

        // Identity change: known UID arrives with a different name.
        const auto storedName = isNewgenUid(ref.uid) ? rIdToName.constFind(ref.uid)
                                                     : numericIdToName.constFind(ref.uid);
        const auto &nameMap = isNewgenUid(ref.uid) ? rIdToName : numericIdToName;
        if (storedName != nameMap.constEnd() && storedName.value() != incomingName) {
            result.identityChanges << QStringLiteral("%1: '%2' → '%3'")
                                          .arg(ref.uid, storedName.value(), incomingName);
        }

        if (!isNewgenUid(ref.uid)) {
            // Scenario 1: numeric UID exported for a known newgen (missing
            // "r-" prefix). Only numeric X -> existing r-X is a valid fix.
            const QString candidate = QStringLiteral("r-") + ref.uid;
            const auto rIt = rIdToName.constFind(candidate);
            if (rIt != rIdToName.constEnd()) {
                if (rIt.value() == incomingName) {
                    const QString numericUid = ref.uid;
                    ref.uid = candidate;
                    // A stale record under the bare numeric ID with the same
                    // name is merged away for good.
                    if (numericIdToName.value(numericUid) == incomingName) {
                        const int badIdx = existingByUid.value(numericUid, -1);
                        const int goodIdx = existingByUid.value(candidate, -1);
                        if (badIdx >= 0 && goodIdx >= 0) {
                            if (!db.mergePlayerInto(existingPlayers[badIdx].id,
                                                    existingPlayers[goodIdx].id)) {
                                result.error = db.errorString();
                                return result;
                            }
                            mergedSources.insert(candidate, existingPlayers[badIdx]);
                            removedIds << existingPlayers[badIdx].id;
                            existingByUid.remove(numericUid);
                            numericIdToName.remove(numericUid);
                        }
                    }
                } else {
                    result.idNameConflicts << QStringLiteral("%1 (UID %2) vs. %3 (%4)")
                                                  .arg(incomingName, ref.uid, rIt.value(),
                                                       candidate);
                }
            }
        } else {
            // Scenario 2: the file has the correct r-ID but the database
            // (also) holds a corrupted numeric record with the same name.
            const QString numericPart = ref.uid.mid(2);
            if (numericIdToName.value(numericPart) == incomingName
                && !numericPart.isEmpty()) {
                const int badIdx = existingByUid.value(numericPart, -1);
                if (badIdx >= 0) {
                    const int goodIdx = existingByUid.value(ref.uid, -1);
                    if (goodIdx >= 0) {
                        if (!db.mergePlayerInto(existingPlayers[badIdx].id,
                                                existingPlayers[goodIdx].id)) {
                            result.error = db.errorString();
                            return result;
                        }
                        mergedSources.insert(ref.uid, existingPlayers[badIdx]);
                        removedIds << existingPlayers[badIdx].id;
                    } else {
                        // No r-record yet: rename keeps history and every
                        // app-managed column intact.
                        if (!db.renamePlayerUid(existingPlayers[badIdx].id, ref.uid)) {
                            result.error = db.errorString();
                            return result;
                        }
                        existingByUid.insert(ref.uid, badIdx);
                        rIdToName.insert(ref.uid, incomingName);
                    }
                    existingByUid.remove(numericPart);
                    numericIdToName.remove(numericPart);
                }
            }
        }
    }

    // --- Build and upsert the player batch. ---
    // Column -> full name mapping resolved once.
    QList<QPair<int, QString>> columnTargets; // (column index, full name)
    for (int c = 0; c < table.headers.size(); ++c) {
        if (!useColumn[static_cast<size_t>(c)] || c == uidCol)
            continue;
        const auto it = mapping.constFind(table.headers.at(c));
        if (it != mapping.constEnd())
            columnTargets.append({c, it.value()});
    }

    const int total = static_cast<int>(rowRefs.size());
    int done = 0;
    std::vector<Player> batch;
    batch.reserve(kUpsertBatchSize);
    std::vector<Player> written; // final rows (with ids), only if updatedPlayers
    // Roles filled in from a merged-away duplicate — the only way an import
    // changes assigned roles; everything else keeps player_roles untouched.
    std::vector<std::pair<int, QStringList>> mergedRoles;

    const auto flush = [&]() -> bool {
        if (batch.empty())
            return true;
        if (!db.upsertPlayers(batch, Database::RoleWrite::Keep)) {
            result.error = db.errorString();
            return false;
        }
        result.playersImported += static_cast<int>(batch.size());
        if (updatedPlayers)
            std::move(batch.begin(), batch.end(), std::back_inserter(written));
        batch.clear();
        if (progress)
            progress(done, total);
        return true;
    };

    for (const RowRef &ref : rowRefs) {
        const QStringList &cells = table.rows.at(ref.rowIndex);

        Player player;
        const int existingIdx = existingByUid.value(ref.uid, -1);
        if (existingIdx >= 0) {
            player = existingPlayers[static_cast<size_t>(existingIdx)];
            player.uid = ref.uid; // rename case: keep id, adopt the r-uid
        } else {
            ++result.newPlayers;
        }
        const auto mergedIt = mergedSources.constFind(ref.uid);
        if (mergedIt != mergedSources.constEnd()) {
            const QStringList rolesBefore = player.assignedRoles;
            mergeAppManaged(player, mergedIt.value());
            if (player.assignedRoles != rolesBefore)
                mergedRoles.push_back({player.id, player.assignedRoles});
        }

        player.uid = ref.uid;
        player.lastSeenUpdate = newCounter; // present in this upload → fresh
        for (const auto &[column, fullName] : columnTargets)
            applyColumn(player, fullName, cells.at(column).trimmed());
        if (player.name.isEmpty())
            player.name = cells.at(nameCol).trimmed();

        result.affectedUids << ref.uid;
        batch.push_back(std::move(player));
        ++done;
        if (static_cast<int>(batch.size()) >= kUpsertBatchSize && !flush())
            return result;
    }
    if (!flush())
        return result;
    if (!db.replacePlayerRoles(mergedRoles)) {
        result.error = db.errorString();
        return result;
    }

    // The bumped counter is part of the same transaction, so a failed import
    // never advances the freshness clock.
    if (!db.setSetting(QStringLiteral("update_counter"), QString::number(newCounter))
        || !transaction.commit()) {
        result.error = db.errorString();
        return result;
    }
    result.updateCounter = newCounter;
    result.success = true;

    if (updatedPlayers) {
        // Bring the caller's list to the post-import DB state without a full
        // reload. existingPlayers may alias *updatedPlayers; it is no longer
        // read from here on. Order mirrors Database::loadPlayers() (rowid order):
        // updated rows keep their position, new rows (ascending fresh ids) are
        // appended. Merged-away rows go first — a new row may reuse their id.
        std::vector<Player> &players = *updatedPlayers;
        if (!removedIds.isEmpty()) {
            const QSet<int> removed(removedIds.cbegin(), removedIds.cend());
            players.erase(std::remove_if(players.begin(), players.end(),
                                         [&removed](const Player &p) {
                                             return removed.contains(p.id);
                                         }),
                          players.end());
        }
        QHash<int, size_t> rowById;
        rowById.reserve(static_cast<int>(players.size()));
        for (size_t i = 0; i < players.size(); ++i)
            rowById.insert(players[i].id, i);
        for (Player &player : written) {
            const auto it = rowById.constFind(player.id);
            if (it != rowById.constEnd())
                players[it.value()] = std::move(player);
            else
                players.push_back(std::move(player));
        }
    }
    return result;
}

ImportResult HtmlImporter::importFile(const QString &filePath, Database &db,
                                      const std::vector<Player> &existingPlayers,
                                      std::function<void(int, int)> progress,
                                      const QString &fmVersionId,
                                      const std::function<void(int, int)> &parseProgress,
                                      std::vector<Player> *updatedPlayers)
{
    HtmlTable table;
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            ImportResult result;
            result.error = QStringLiteral("Could not open file: %1").arg(file.errorString());
            return result;
        }
        // The decoded file (2 bytes per char) is only needed for parsing; it is
        // released at the end of this scope, before the database work starts.
        const QString html = QString::fromUtf8(file.readAll());
        QString parseError;
        if (!extractTable(html, &table, &parseError, parseProgress)) {
            ImportResult result;
            result.error = parseError;
            return result;
        }
    }
    return importTable(table, db, existingPlayers, progress, fmVersionId, updatedPlayers);
}

QString HtmlImporter::forceUpdateSinglePlayer(const QString &html, Database &db,
                                              const std::vector<Player> &existingPlayers,
                                              const QString &targetUid, QString *errorOut,
                                              const QString &fmVersionId)
{
    const QHash<QString, QString> &mapping = attributeMapping(fmVersionId);
    const auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return QString();
    };

    HtmlTable table;
    QString parseError;
    if (!extractTable(html, &table, &parseError))
        return fail(parseError);
    if (table.rows.size() != 1)
        return fail(QStringLiteral("The file must contain exactly ONE player, "
                                   "but %1 rows were found.")
                        .arg(table.rows.size()));

    const Player *existing = nullptr;
    for (const Player &p : existingPlayers) {
        if (p.uid == targetUid) {
            existing = &p;
            break;
        }
    }
    if (!existing)
        return fail(QStringLiteral("Target player %1 not found.").arg(targetUid));

    Player player = *existing; // app-managed fields preserved
    QSet<QString> seenHeaders;
    QString fileName;
    for (int c = 0; c < table.headers.size(); ++c) {
        const QString &header = table.headers.at(c);
        if (seenHeaders.contains(header))
            continue;
        seenHeaders.insert(header);
        const auto it = mapping.constFind(header);
        if (it == mapping.constEnd())
            continue;
        const QString value = table.rows.at(0).at(c).trimmed();
        if (it.value() == QLatin1String("Name"))
            fileName = value;
        applyColumn(player, it.value(), value);
    }
    player.uid = targetUid; // everything is keyed to the confirmed target
    // Targeted refresh: mark the player fresh as of the latest recorded upload
    // (does not advance the global counter).
    player.lastSeenUpdate =
        db.setting(QStringLiteral("update_counter"), QStringLiteral("0")).toInt();

    std::vector<Player> batch{std::move(player)};
    if (!db.upsertPlayers(batch))
        return fail(db.errorString());
    return fileName;
}

} // namespace fm
