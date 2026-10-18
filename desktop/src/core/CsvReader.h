#pragma once

#include "ImportTable.h"

#include <QByteArray>
#include <QChar>
#include <QString>

namespace fm {

// Reads CSV exports into the same table structure as the HTML parser.
class CsvReader
{
public:
    // Parses raw file bytes. The delimiter (';', ',' or tab) is taken from the
    // header line; fields may be quoted ("" escapes a quote, line breaks inside
    // quotes are kept). A UTF-8 BOM is dropped; bytes that are not valid UTF-8
    // are read as Windows-1252. Cells are trimmed, blank lines skipped and rows
    // whose cell count differs from the header counted as malformedRows.
    static bool read(const QByteArray &bytes, ImportTable *out, ImportError *error = nullptr);

    // Decoding step of read(): UTF-8 (BOM removed) or Windows-1252 fallback.
    static QString decode(const QByteArray &bytes);

    // Delimiter detection step of read(), on the decoded text.
    static QChar detectDelimiter(const QString &text);
};

} // namespace fm
