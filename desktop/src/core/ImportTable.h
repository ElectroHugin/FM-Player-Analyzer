#pragma once

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
};

struct ImportError {
    ImportErrorCode code = ImportErrorCode::None;
    QString arg;
    QString message; // English, for logs and tests
};

// Reads an export file into a table, choosing the reader by file extension
// (".csv" -> CsvReader, anything else -> HTML). Fails with UnsupportedFormat if
// the FM version's profile does not list that format. progress(done, total)
// is forwarded to the HTML parser.
bool readImportTable(const QString &filePath, const QString &fmVersionId, ImportTable *out,
                     ImportError *error, const std::function<void(int, int)> &progress = {});

// Export format of a file by extension: "csv" or "html".
QString importFormatForFile(const QString &filePath);

} // namespace fm
