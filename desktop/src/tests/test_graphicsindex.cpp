// Optional portraits/logos from the FM graphics folder: config.xml parsing,
// compact index, cache invalidation. Builds a tiny fake graphics folder.
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/AppConfig.h"
#include "core/GraphicsIndex.h"

using namespace fm;

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

QByteArray config(const QByteArray &records)
{
    return "<record>\n\t<boolean id=\"preload\" value=\"false\"/>\n"
           "\t<boolean id=\"amap\" value=\"false\"/>\n\t<list id=\"maps\">\n"
           + records + "\t</list>\n</record>\n";
}

} // namespace

class TestGraphicsIndex : public QObject
{
    Q_OBJECT

private slots:
    void parsesKnownRecordsOnly()
    {
        const auto records = GraphicsIndex::parseConfig(config(
            "<record from=\"face_123\" to=\"graphics/pictures/person/123/portrait\"/>\n"
            "<record from=\"iconface_123\" to=\"graphics/pictures/person/123/icon\"/>\n"
            "<record from=\"77_club\" to=\"graphics/pictures/club/77/logo\"/>\n"
            "<record from=\"0\" to=\"graphics/pictures/club/none/logo/style/0\"/>\n"
            "<record from=\"5_nation\" to=\"graphics/pictures/nation/5/logo\"/>\n"
            "<record from=\"x\" to=\"graphics/pictures/club/abc/logo\"/>\n"));
        QCOMPARE(int(records.size()), 3);
        QCOMPARE(records[0].kind, GraphicsIndex::Kind::Portrait);
        QCOMPARE(records[0].id, 123u);
        QCOMPARE(records[0].from, QStringLiteral("face_123"));
        QCOMPARE(records[1].kind, GraphicsIndex::Kind::ClubLogo);
        QCOMPARE(records[2].kind, GraphicsIndex::Kind::NationLogo);
    }

    void parsesUids()
    {
        QCOMPARE(GraphicsIndex::parseId(QStringLiteral("2000350338")), 2000350338u);
        QCOMPARE(GraphicsIndex::parseId(QStringLiteral("r-12345")), 12345u);
        QCOMPARE(GraphicsIndex::parseId(QStringLiteral("")), 0u);
        QCOMPARE(GraphicsIndex::parseId(QStringLiteral("abc")), 0u);
        QCOMPARE(GraphicsIndex::parseId(QStringLiteral("99999999999")), 0u);
    }

    void scanFindsFilesAndCacheRoundTrips()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(QStringLiteral("graphics"));
        writeFile(root + QStringLiteral("/faces/config.xml"),
                  config("<record from=\"face_1\" to=\"graphics/pictures/person/1/portrait\"/>\n"
                         "<record from=\"face_42\" to=\"graphics/pictures/person/42/portrait\"/>\n"
                         "<record from=\"odd\" to=\"graphics/pictures/person/7/portrait\"/>\n"));
        writeFile(root + QStringLiteral("/faces/face_42.png"), "png");
        writeFile(root + QStringLiteral("/faces/odd.png"), "png");
        writeFile(root + QStringLiteral("/Logos/Clubs/Normal/config.xml"),
                  config("<record from=\"77_club\" to=\"graphics/pictures/club/77/logo\"/>\n"));
        writeFile(root + QStringLiteral("/Logos/Clubs/Normal/77_club.png"), "png");
        // A second pack mapping the same portrait: the first config (sorted) wins.
        writeFile(root + QStringLiteral("/zz/config.xml"),
                  config("<record from=\"other_42\" to=\"graphics/pictures/person/42/portrait\"/>\n"));

        GraphicsIndex index;
        QVERIFY(index.scan(root));
        QCOMPARE(index.stats().configFiles, 3);
        QCOMPARE(index.stats().portraits, 3);
        QCOMPARE(index.stats().clubLogos, 1);
        QCOMPARE(index.path(GraphicsIndex::Kind::Portrait, 42),
                 QDir(root).absolutePath() + QStringLiteral("/faces/face_42.png"));
        QCOMPARE(index.path(GraphicsIndex::Kind::Portrait, 7),
                 QDir(root).absolutePath() + QStringLiteral("/faces/odd.png"));
        QCOMPARE(index.path(GraphicsIndex::Kind::ClubLogo, 77),
                 QDir(root).absolutePath() + QStringLiteral("/Logos/Clubs/Normal/77_club.png"));
        QVERIFY(index.path(GraphicsIndex::Kind::Portrait, 5).isEmpty());
        QVERIFY(!index.contains(GraphicsIndex::Kind::ClubLogo, 42));

        const QString cache = dir.filePath(QStringLiteral("cache/graphics.bin"));
        QVERIFY(index.saveCache(cache));
        GraphicsIndex cached;
        QVERIFY(cached.loadCache(cache, root));
        QVERIFY(cached.stats().fromCache);
        QCOMPARE(cached.stats().portraits, 3);
        QCOMPARE(cached.path(GraphicsIndex::Kind::Portrait, 42),
                 index.path(GraphicsIndex::Kind::Portrait, 42));
        QCOMPARE(cached.path(GraphicsIndex::Kind::ClubLogo, 77),
                 index.path(GraphicsIndex::Kind::ClubLogo, 77));

        // Another root never uses this cache.
        GraphicsIndex wrongRoot;
        QVERIFY(!wrongRoot.loadCache(cache, dir.filePath(QStringLiteral("other"))));

        // A changed pack invalidates it.
        writeFile(root + QStringLiteral("/Logos/Clubs/Normal/config.xml"),
                  config("<record from=\"77_club\" to=\"graphics/pictures/club/77/logo\"/>\n"
                         "<record from=\"78_club\" to=\"graphics/pictures/club/78/logo\"/>\n"));
        GraphicsIndex stale;
        QVERIFY(!stale.loadCache(cache, root));
        QVERIFY(stale.isEmpty());
    }

    void missingFolderFails()
    {
        GraphicsIndex index;
        QVERIFY(!index.scan(QStringLiteral("/does/not/exist/graphics")));
        QVERIFY(index.isEmpty());
    }

    void offByDefault()
    {
        QTemporaryDir dir;
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        QVERIFY(!config.graphicsEnabled());
        QVERIFY(config.graphicsDir().isEmpty());
        QVERIFY(config.effectiveGraphicsDir(QStringLiteral("fm27"))
                    .endsWith(QStringLiteral("/Football Manager 27/graphics")));
        QVERIFY(config.setGraphicsEnabled(true));
        QVERIFY(config.setGraphicsDir(QStringLiteral("/x/graphics")));
        QVERIFY(config.graphicsEnabled());
        QCOMPARE(config.effectiveGraphicsDir(QStringLiteral("fm27")), QStringLiteral("/x/graphics"));
    }
};

QTEST_GUILESS_MAIN(TestGraphicsIndex)
#include "test_graphicsindex.moc"
