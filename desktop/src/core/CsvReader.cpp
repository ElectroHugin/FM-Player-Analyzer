#include "CsvReader.h"

#include <QStringDecoder>

namespace fm {

namespace {

// Windows-1252 code points for 0x80-0x9F (0 = undefined, kept as the C1 code);
// every other byte equals its Latin-1 code point.
constexpr char16_t kCp1252High[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
};

QString decodeWindows1252(QByteArrayView bytes)
{
    QString out;
    out.resize(bytes.size());
    QChar *dst = out.data();
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        const auto b = static_cast<unsigned char>(bytes[i]);
        char16_t cp = b;
        if (b >= 0x80 && b <= 0x9F && kCp1252High[b - 0x80] != 0)
            cp = kCp1252High[b - 0x80];
        dst[i] = QChar(cp);
    }
    return out;
}

} // namespace

QString CsvReader::decode(const QByteArray &bytes)
{
    QByteArrayView view(bytes);
    if (view.startsWith("\xEF\xBB\xBF"))
        view = view.sliced(3);
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = utf8.decode(view);
    if (!utf8.hasError())
        return text;
    return decodeWindows1252(view);
}

QChar CsvReader::detectDelimiter(const QString &text)
{
    // Count candidates in the header line, outside quotes.
    int semicolons = 0, commas = 0, tabs = 0;
    bool inQuotes = false;
    for (const QChar ch : text) {
        if (ch == QLatin1Char('"')) {
            inQuotes = !inQuotes;
        } else if (!inQuotes) {
            if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r'))
                break;
            if (ch == QLatin1Char(';'))
                ++semicolons;
            else if (ch == QLatin1Char(','))
                ++commas;
            else if (ch == QLatin1Char('\t'))
                ++tabs;
        }
    }
    if (tabs > semicolons && tabs > commas)
        return QLatin1Char('\t');
    if (semicolons >= commas && semicolons > 0)
        return QLatin1Char(';');
    return QLatin1Char(',');
}

bool CsvReader::read(const QByteArray &bytes, ImportTable *out, ImportError *error)
{
    out->headers.clear();
    out->rows.clear();
    out->malformedRows = 0;

    const QString text = decode(bytes);
    const QChar delimiter = detectDelimiter(text);

    bool haveHeader = false;
    QStringList cells;
    QString field;
    bool inQuotes = false;
    bool rowHasContent = false; // false for a blank line

    const auto endField = [&] {
        cells << field.trimmed();
        field.clear();
    };
    const auto endRow = [&] {
        endField();
        if (rowHasContent) {
            if (!haveHeader) {
                out->headers = cells;
                haveHeader = true;
            } else if (cells.size() == out->headers.size()) {
                out->rows.append(cells);
            } else {
                ++out->malformedRows;
            }
        }
        cells.clear();
        rowHasContent = false;
    };

    const qsizetype n = text.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QChar ch = text.at(i);
        if (inQuotes) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < n && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += ch;
            }
            continue;
        }
        if (ch == QLatin1Char('"')) {
            inQuotes = true;
            rowHasContent = true;
        } else if (ch == delimiter) {
            endField();
            rowHasContent = true;
        } else if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            if (ch == QLatin1Char('\r') && i + 1 < n && text.at(i + 1) == QLatin1Char('\n'))
                ++i;
            endRow();
        } else {
            field += ch;
            if (!ch.isSpace())
                rowHasContent = true;
        }
    }
    if (rowHasContent || !field.isEmpty())
        endRow();

    if (!haveHeader) {
        if (error)
            *error = {ImportErrorCode::NoHeaderRow, QString(),
                      QStringLiteral("No header row found in file.")};
        return false;
    }
    return true;
}

} // namespace fm
