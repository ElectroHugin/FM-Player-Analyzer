#include "ImportTable.h"

#include "Constants.h"
#include "CsvReader.h"
#include "Fm27SaveImport.h"
#include "HtmlImporter.h"

#include <QFile>
#include <QFileInfo>

namespace fm {

QString importFormatForFile(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix();
    if (suffix.compare(QLatin1String("csv"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("csv");
    if (suffix.compare(QLatin1String("fm"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("fm");
    return QStringLiteral("html");
}

bool readImportTable(const QString &filePath, const QString &fmVersionId, ImportTable *out,
                     ImportError *error, const std::function<void(int, int)> &progress)
{
    const auto fail = [&](ImportErrorCode code, const QString &arg, const QString &message) {
        if (error)
            *error = {code, arg, message};
        return false;
    };

    const QString format = importFormatForFile(filePath);
    if (!fmDataVersion(fmVersionId).importFormats.contains(format))
        return fail(ImportErrorCode::UnsupportedFormat, format,
                    QStringLiteral("This FM version cannot import %1 files.").arg(format));

    if (format == QLatin1String("fm")) {
        // A save game (FM27): read directly, filtered by what the user may see.
        Fm27SaveImport::Options options;
        options.attributeMasking = Fm27SaveImport::attributeMasking();
        if (!Fm27SaveImport::buildTable(filePath, options, out, error, progress))
            return false;
        applyHeaderAliases(&out->headers, fmVersionId);
        return true;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(ImportErrorCode::FileOpen, file.errorString(),
                    QStringLiteral("Could not open file: %1").arg(file.errorString()));

    bool ok = false;
    if (format == QLatin1String("csv")) {
        ok = CsvReader::read(file.readAll(), out, error);
    } else {
        // The decoded file (2 bytes per char) is only needed for parsing; it
        // is released when this block ends, before the database work starts.
        const QString html = QString::fromUtf8(file.readAll());
        ok = HtmlImporter::extractTable(html, out, error, progress);
    }
    if (ok)
        applyHeaderAliases(&out->headers, fmVersionId);
    return ok;
}

} // namespace fm
