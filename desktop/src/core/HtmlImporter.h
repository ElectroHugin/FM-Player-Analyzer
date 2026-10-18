#pragma once

#include "ImportTable.h"
#include "Player.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace fm {

class Database;

// Outcome of an import (port of legacy parse_and_update_data). The message
// lists are structured facts; the UI turns them into localized warnings.
struct ImportResult {
    bool success = false;
    QString error; // English, for logs and tests
    // Translatable reason for a failure; None/Other = show `error` as is.
    ImportErrorCode errorCode = ImportErrorCode::None;
    QString errorArg;

    int rowsParsed = 0;
    int playersImported = 0;
    int newPlayers = 0;   // rows that created a player (uid not in DB before)
    int malformedRows = 0;
    int emptyUidRows = 0;

    QStringList duplicateUidNames; // duplicate UIDs inside the file (last row kept)
    QStringList unknownColumns;    // exported columns the app does not know
    // Columns of the version's import schema (internal names) the file lacks;
    // "Registration"/"Information" are not reported.
    QStringList missingColumns;
    // "Name (UID 123) vs. Name2 (r-123)" — incoming numeric UID matches a
    // known newgen ID but the names differ; records left untouched.
    QStringList idNameConflicts;
    // "123: 'Old Name' -> 'New Name'" — FM likely re-issued the ID.
    QStringList identityChanges;

    // Players whose manual U21 decision expired because they turned 22
    // (Registration::expireU21Override).
    QStringList expiredU21Names;

    // Final UIDs (after ID unification) of every imported row, in file order.
    QStringList affectedUids;

    // Per-database upload counter value assigned to this import (0 if the
    // import failed before stamping). Every imported player's lastSeenUpdate is
    // set to this; drives data-freshness (see Freshness).
    int updateCounter = 0;
    // The import counted as a full export and advanced the counter; a partial
    // export only refreshed its own players (Freshness::isFullUpload).
    bool fullUpload = false;
};

// Parses FM HTML exports and merges them into the database. Port of legacy
// data_parser.py: header validation/deduplication, UID sanity checks and the
// ID-unification engine (missing "r-" prefix, corrupted numeric records).
class HtmlImporter
{
public:
    // Extracts the first <table> from the HTML. Returns false with errorOut
    // set if no usable table/header was found. progress(bytesDone, bytesTotal)
    // is called periodically while scanning so callers can show parse progress
    // on very large exports.
    static bool extractTable(const QString &html, ImportTable *out, QString *errorOut = nullptr,
                             const std::function<void(int, int)> &progress = {});
    // Same, reporting a translatable error code.
    static bool extractTable(const QString &html, ImportTable *out, ImportError *errorOut,
                             const std::function<void(int, int)> &progress);

    // Full import into db, atomic (one transaction). existingPlayers must
    // reflect the current DB state (ids matching). progress(done, total) is
    // called per processed row batch. fmVersionId selects the export layout
    // (empty = default version).
    //
    // updatedPlayers (optional): on success receives the complete post-import
    // player list — same content and order as a fresh Database::loadPlayers() —
    // so the caller can skip a full reload. It may point to the very vector
    // passed as existingPlayers (updated in place). Untouched on failure.
    static ImportResult importHtml(const QString &html, Database &db,
                                   const std::vector<Player> &existingPlayers,
                                   std::function<void(int, int)> progress = {},
                                   const QString &fmVersionId = QString(),
                                   const std::function<void(int, int)> &parseProgress = {},
                                   std::vector<Player> *updatedPlayers = nullptr);

    // Reads the file (HTML or CSV by extension, see readImportTable) and
    // imports it like importHtml; the decoded file is released right after
    // parsing, before the DB work.
    static ImportResult importFile(const QString &filePath, Database &db,
                                   const std::vector<Player> &existingPlayers,
                                   std::function<void(int, int)> progress = {},
                                   const QString &fmVersionId = QString(),
                                   const std::function<void(int, int)> &parseProgress = {},
                                   std::vector<Player> *updatedPlayers = nullptr);

    // Manual, user-confirmed update of ONE player from an HTML export that
    // contains exactly one row. Writes the file's data onto targetUid
    // regardless of the UID inside the file, preserving app-managed fields.
    // Returns the player name found in the file; error set on failure.
    static QString forceUpdateSinglePlayer(const QString &html, Database &db,
                                           const std::vector<Player> &existingPlayers,
                                           const QString &targetUid, QString *errorOut,
                                           const QString &fmVersionId = QString());
    // Same on an already read table (any export format); errorOut->code is
    // None on success.
    static QString forceUpdateSinglePlayer(const ImportTable &table, Database &db,
                                           const std::vector<Player> &existingPlayers,
                                           const QString &targetUid, ImportError *errorOut,
                                           const QString &fmVersionId = QString());

    // Applies one HTML column value (full column name, e.g. "Acceleration",
    // "Transfer Value") to a player. Unknown columns are ignored.
    static void applyColumn(Player &player, const QString &fullColumnName, const QString &value);

private:
    // Shared import body once the table is extracted.
    static ImportResult importTable(const ImportTable &table, Database &db,
                                    const std::vector<Player> &existingPlayers,
                                    const std::function<void(int, int)> &progress,
                                    const QString &fmVersionId,
                                    std::vector<Player> *updatedPlayers);
};

} // namespace fm
