#pragma once

#include <QDate>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace fm {

// Raw table read from an FM export (HTML or CSV), before any column mapping.
struct ImportTable {
    QStringList headers;
    QList<QStringList> rows;
    int malformedRows = 0; // rows whose cell count did not match the header
    // In-game date of the data, when the file carries one (FM27 save).
    QDate gameDate;
    // UIDs of players the user's scouting knows (FM27 save): followed by the
    // import history like the own squad (see ImportHistory).
    QStringList knownUids;
    // Save-only metadata. Presence means the save supplied the information,
    // including an empty list; CSV/HTML leave existing save information intact.
    QHash<QString, QStringList> naturalPositionsByUid;
    // Club name as written into the rows -> the club's database Unique ID
    // (FM27 save). The game may rename clubs between saves (e.g. after a data
    // update); the import follows the own and the second team by this id.
    QHash<QString, quint32> clubIdByName;
};

// Why reading or importing an export failed. The core keeps an English
// message for logs and tests; the UI translates the code (with its argument).
enum class ImportErrorCode {
    None,
    Other,                 // no translatable text (e.g. a database error): show the message
    FileOpen,              // arg: system error text
    UnsupportedFormat,     // arg: format, e.g. "csv"
    NoTable,               // HTML: no <table>
    NoHeaderCells,         // HTML: first row has no <th>
    NoHeaderRow,           // HTML table / CSV file without a header row
    TooFewColumns,         // arg: column count
    RequiredColumnMissing, // arg: export header, e.g. "UID"
    NoImportableRows,      // every row lacked a UID
    NotSingleRow,          // single-player update; arg: row count
    TargetNotFound,        // single-player update; arg: target UID
    SaveUnreadable,        // FM27 save game (.fm); arg: English detail
};

struct ImportError {
    ImportErrorCode code = ImportErrorCode::None;
    QString arg;
    QString message; // English, for logs and tests
};

// Reads an export file into a table, choosing the reader by file extension
// (".csv" -> CsvReader, ".fm" -> FM27 save via Fm27SaveImport, anything else
// -> HTML). Fails with UnsupportedFormat if
// the FM version's profile does not list that format. progress(done, total)
// is forwarded to the HTML parser.
bool readImportTable(const QString &filePath, const QString &fmVersionId, ImportTable *out,
                     ImportError *error, const std::function<void(int, int)> &progress = {});

// Export format of a file by extension: "csv", "fm" (save game) or "html".
QString importFormatForFile(const QString &filePath);

} // namespace fm
