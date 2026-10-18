#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "core/CsvReader.h"
#include "core/Database.h"
#include "core/HtmlImporter.h"
#include "core/ImportTable.h"

using namespace fm;

namespace {

const QStringList kHeaders = {
    QStringLiteral("UID"),         QStringLiteral("Name"),           QStringLiteral("Age"),
    QStringLiteral("Club"),        QStringLiteral("Position"),       QStringLiteral("Personality"),
    QStringLiteral("Transfer Value"), QStringLiteral("Height"),      QStringLiteral("Acc"),
    QStringLiteral("Pac"),         QStringLiteral("Fin"),
};

// The same players for both export formats.
const QList<QStringList> kRows = {
    {QStringLiteral("1001"), QStringLiteral("Alpha Tester"), QStringLiteral("24"),
     QStringLiteral("FC Test"), QStringLiteral("D (C), DM"), QStringLiteral("Model Citizen"),
     QStringLiteral("€1.2M"), QStringLiteral("181 cm"), QStringLiteral("14"),
     QStringLiteral("15"), QStringLiteral("12-15")},
    {QStringLiteral("r-2002"), QStringLiteral("Bjørn Müller"), QStringLiteral("17"),
     QStringLiteral("FC \"Quote\" Test"), QStringLiteral("ST (C)"), QStringLiteral("Resolute"),
     QStringLiteral("Not for Sale"), QStringLiteral("175 cm"), QStringLiteral("9"),
     QStringLiteral("11"), QStringLiteral("16")},
};

QString htmlEscape(QString text)
{
    return text.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
        .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
        .replace(QLatin1Char('>'), QStringLiteral("&gt;"));
}

QByteArray htmlExport()
{
    QString html = QStringLiteral("<html><body><table>\n<tr>");
    for (const QString &h : kHeaders)
        html += QStringLiteral("<th>%1</th>").arg(htmlEscape(h));
    html += QStringLiteral("</tr>\n");
    for (const QStringList &row : kRows) {
        html += QStringLiteral("<tr>");
        for (const QString &cell : row)
            html += QStringLiteral("<td>%1</td>").arg(htmlEscape(cell));
        html += QStringLiteral("</tr>\n");
    }
    html += QStringLiteral("</table></body></html>");
    return html.toUtf8();
}

QString csvField(const QString &cell, QChar delimiter)
{
    if (!cell.contains(delimiter) && !cell.contains(QLatin1Char('"')))
        return cell;
    QString quoted = cell;
    quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QByteArray csvExport(QChar delimiter)
{
    QStringList lines;
    QStringList header;
    for (const QString &h : kHeaders)
        header << csvField(h, delimiter);
    lines << header.join(delimiter);
    for (const QStringList &row : kRows) {
        QStringList cells;
        for (const QString &cell : row)
            cells << csvField(cell, delimiter);
        lines << cells.join(delimiter);
    }
    return QByteArray("\xEF\xBB\xBF") + lines.join(QStringLiteral("\r\n")).toUtf8() + "\r\n";
}

QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return {};
    return path;
}

} // namespace

class TestCsvImport : public QObject
{
    Q_OBJECT

private slots:
    void delimiterDetection()
    {
        QCOMPARE(CsvReader::detectDelimiter(QStringLiteral("UID;Name;Age\n1;a,b;3")),
                 QLatin1Char(';'));
        QCOMPARE(CsvReader::detectDelimiter(QStringLiteral("UID,Name,Age\n1;2;3")),
                 QLatin1Char(','));
        QCOMPARE(CsvReader::detectDelimiter(QStringLiteral("UID\tName\tAge")), QLatin1Char('\t'));
        // Delimiters inside quotes do not count.
        QCOMPARE(CsvReader::detectDelimiter(QStringLiteral("\"a;b;c\",UID,Name")),
                 QLatin1Char(','));
    }

    void quotesAndLineBreaks()
    {
        ImportTable table;
        QVERIFY(CsvReader::read(QByteArray("A;B;C\n"
                                           "\"x;y\";\"say \"\"hi\"\"\";\"line1\nline2\"\n"
                                           "\n"
                                           "1;2\n"
                                           " p ; q ;r\n"),
                                &table));
        QCOMPARE(table.headers, (QStringList{"A", "B", "C"}));
        QCOMPARE(table.rows.size(), 2);
        QCOMPARE(table.rows.at(0),
                 (QStringList{"x;y", "say \"hi\"", QStringLiteral("line1\nline2")}));
        QCOMPARE(table.rows.at(1), (QStringList{"p", "q", "r"}));
        QCOMPARE(table.malformedRows, 1); // "1;2"; the blank line is skipped
    }

    void bomAndEncodings()
    {
        // UTF-8 with BOM: the BOM must not stick to the first header.
        ImportTable table;
        QVERIFY(CsvReader::read(QByteArray("\xEF\xBB\xBFUID,Name\n1,M\xC3\xBCller\n"), &table));
        QCOMPARE(table.headers.first(), QStringLiteral("UID"));
        QCOMPARE(table.rows.at(0).at(1), QStringLiteral("Müller"));

        // Not valid UTF-8 -> Windows-1252 (Latin-1 plus the 0x80-0x9F block).
        QVERIFY(CsvReader::read(QByteArray("UID;Name;Value\n1;M\xFCller;\x80" "5M\n"), &table));
        QCOMPARE(table.rows.at(0).at(1), QStringLiteral("Müller"));
        QCOMPARE(table.rows.at(0).at(2), QStringLiteral("€5M"));
    }

    void emptyFileHasNoHeader()
    {
        ImportTable table;
        ImportError error;
        QVERIFY(!CsvReader::read(QByteArray("\n\n"), &table, &error));
        QCOMPARE(error.code, ImportErrorCode::NoHeaderRow);
    }

    void formatByExtension()
    {
        QCOMPARE(importFormatForFile(QStringLiteral("C:/x/Squad.CSV")), QStringLiteral("csv"));
        QCOMPARE(importFormatForFile(QStringLiteral("squad.html")), QStringLiteral("html"));
        QCOMPARE(importFormatForFile(QStringLiteral("squad.htm")), QStringLiteral("html"));
        QCOMPARE(importFormatForFile(QStringLiteral("squad")), QStringLiteral("html"));
    }

    // The core promise: the same export as CSV and as HTML yields identical
    // players, for every delimiter.
    void csvMatchesHtml_data()
    {
        QTest::addColumn<QString>("delimiter");
        QTest::newRow("semicolon") << QStringLiteral(";");
        QTest::newRow("comma") << QStringLiteral(",");
        QTest::newRow("tab") << QStringLiteral("\t");
    }

    void csvMatchesHtml()
    {
        QFETCH(QString, delimiter);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString htmlPath = writeFile(dir, QStringLiteral("export.html"), htmlExport());
        const QString csvPath =
            writeFile(dir, QStringLiteral("export.csv"), csvExport(delimiter.at(0)));
        QVERIFY(!htmlPath.isEmpty() && !csvPath.isEmpty());

        const QString tag = QString::fromLatin1(QTest::currentDataTag());
        Database htmlDb(QStringLiteral("csv_test_html_") + tag);
        Database csvDb(QStringLiteral("csv_test_csv_") + tag);
        QVERIFY(htmlDb.open(dir.filePath(QStringLiteral("html.db"))));
        QVERIFY(csvDb.open(dir.filePath(QStringLiteral("csv.db"))));

        const ImportResult fromHtml = HtmlImporter::importFile(htmlPath, htmlDb, {});
        const ImportResult fromCsv = HtmlImporter::importFile(csvPath, csvDb, {});
        QVERIFY2(fromHtml.success, qPrintable(fromHtml.error));
        QVERIFY2(fromCsv.success, qPrintable(fromCsv.error));
        QCOMPARE(fromCsv.playersImported, fromHtml.playersImported);
        QCOMPARE(fromCsv.malformedRows, 0);
        QCOMPARE(fromCsv.unknownColumns, fromHtml.unknownColumns);
        QCOMPARE(fromCsv.missingColumns, fromHtml.missingColumns);

        const std::vector<Player> a = htmlDb.loadPlayers();
        const std::vector<Player> b = csvDb.loadPlayers();
        QCOMPARE(b.size(), a.size());
        QCOMPARE(static_cast<int>(a.size()), 2);
        for (size_t i = 0; i < a.size(); ++i) {
            QCOMPARE(b[i].uid, a[i].uid);
            QCOMPARE(b[i].name, a[i].name);
            QCOMPARE(b[i].age, a[i].age);
            QCOMPARE(b[i].club, a[i].club);
            QCOMPARE(b[i].positionRaw, a[i].positionRaw);
            QCOMPARE(b[i].personality, a[i].personality);
            QCOMPARE(b[i].transferValueRaw, a[i].transferValueRaw);
            QCOMPARE(b[i].transferValue, a[i].transferValue);
            QCOMPARE(b[i].heightRaw, a[i].heightRaw);
            QCOMPARE(b[i].heightCm, a[i].heightCm);
            QCOMPARE(b[i].lastSeenUpdate, a[i].lastSeenUpdate);
            QVERIFY(b[i].attrLo == a[i].attrLo);
            QVERIFY(b[i].attrHi == a[i].attrHi);
        }
        QCOMPARE(a[1].name, QStringLiteral("Bjørn Müller"));
        QCOMPARE(a[1].club, QStringLiteral("FC \"Quote\" Test"));
    }

    void missingColumnsReported()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir, QStringLiteral("e.csv"), csvExport(QLatin1Char(';')));
        Database db(QStringLiteral("csv_test_missing"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        const ImportResult result = HtmlImporter::importFile(path, db, {});
        QVERIFY2(result.success, qPrintable(result.error));
        QVERIFY(result.missingColumns.contains(QStringLiteral("Wage")));
        QVERIFY(result.missingColumns.contains(QStringLiteral("Vision")));
        QVERIFY(!result.missingColumns.contains(QStringLiteral("Registration")));
        QVERIFY(!result.missingColumns.contains(QStringLiteral("Information")));
        QVERIFY(!result.missingColumns.contains(QStringLiteral("Name")));
        QVERIFY(!result.missingColumns.contains(QStringLiteral("Unique ID")));
        QVERIFY(!result.missingColumns.contains(QStringLiteral("Acceleration")));
    }

    void errorCodes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db(QStringLiteral("csv_test_errors"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));

        QByteArray noUid = csvExport(QLatin1Char(','));
        noUid.replace("UID,", "Nr,");
        const ImportResult missing = HtmlImporter::importFile(
            writeFile(dir, QStringLiteral("nouid.csv"), noUid), db, {});
        QVERIFY(!missing.success);
        QCOMPARE(missing.errorCode, ImportErrorCode::RequiredColumnMissing);
        QCOMPARE(missing.errorArg, QStringLiteral("UID"));
        QVERIFY(!missing.error.isEmpty());

        const ImportResult noFile =
            HtmlImporter::importFile(dir.filePath(QStringLiteral("absent.csv")), db, {});
        QCOMPARE(noFile.errorCode, ImportErrorCode::FileOpen);

        const ImportResult noTable = HtmlImporter::importFile(
            writeFile(dir, QStringLiteral("plain.html"), "<p>nothing</p>"), db, {});
        QCOMPARE(noTable.errorCode, ImportErrorCode::NoTable);
    }

    void singlePlayerUpdateFromCsv()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db(QStringLiteral("csv_test_single"));
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        QVERIFY(HtmlImporter::importFile(
                    writeFile(dir, QStringLiteral("all.csv"), csvExport(QLatin1Char(';'))), db, {})
                    .success);

        QByteArray one = "UID;Name;Age;Club;Position;Personality;Transfer Value;Height;Acc;Pac;Fin\n"
                         "999;Alpha Renamed;30;FC Neu;ST (C);Resolute;€3M;181 cm;18;15;12\n";
        ImportTable table;
        ImportError error;
        QVERIFY(readImportTable(writeFile(dir, QStringLiteral("one.csv"), one), QString(), &table,
                                &error));
        const QString fileName = HtmlImporter::forceUpdateSinglePlayer(
            table, db, db.loadPlayers(), QStringLiteral("1001"), &error);
        QCOMPARE(error.code, ImportErrorCode::None);
        QCOMPARE(fileName, QStringLiteral("Alpha Renamed"));
        for (const Player &p : db.loadPlayers()) {
            if (p.uid == QLatin1String("1001")) {
                QCOMPARE(p.age, 30);
                QCOMPARE(p.club, QStringLiteral("FC Neu"));
            }
        }

        ImportTable two;
        QVERIFY(CsvReader::read(csvExport(QLatin1Char(';')), &two));
        HtmlImporter::forceUpdateSinglePlayer(two, db, db.loadPlayers(), QStringLiteral("1001"),
                                              &error);
        QCOMPARE(error.code, ImportErrorCode::NotSingleRow);
        QCOMPARE(error.arg, QStringLiteral("2"));
    }
};

QTEST_GUILESS_MAIN(TestCsvImport)
#include "test_csvimport.moc"
