#include "core/ColorTemplates.h"
#include "core/ThemeColors.h"
#include "core/Utils.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace fm;

namespace {

QHash<QString, QString> sampleColors(const QString &primary)
{
    QHash<QString, QString> colors;
    for (const QString &key : ColorTemplateStore::colorKeys())
        colors.insert(key, QStringLiteral("#808080"));
    colors.insert(QStringLiteral("night_primary_color"), primary);
    colors.insert(QStringLiteral("day_primary_color"), primary);
    return colors;
}

} // namespace

class TestThemeColors : public QObject
{
    Q_OBJECT

private slots:
    void withContrastReachesTarget_data()
    {
        QTest::addColumn<QString>("color");
        QTest::addColumn<QString>("background");
        QTest::addColumn<double>("ratio");
        // Dark club color on the dark base, neon on the light base, purple text.
        QTest::newRow("navy on night") << "#14213d" << "#262626" << 4.5;
        QTest::newRow("yellow on day") << "#ffe600" << "#f0f2f6" << 4.5;
        QTest::newRow("purple on white") << "#9d6bff" << "#ffffff" << 4.5;
        QTest::newRow("white on day ui") << "#f4f4f4" << "#f0f2f6" << 3.0;
        QTest::newRow("black on night ui") << "#101010" << "#262626" << 3.0;
    }
    void withContrastReachesTarget()
    {
        QFETCH(QString, color);
        QFETCH(QString, background);
        QFETCH(double, ratio);
        const QColor result = withContrast(QColor(color), QColor(background), ratio);
        QVERIFY2(contrastRatio(result, QColor(background)) >= ratio,
                 qPrintable(result.name()));
    }

    void withContrastKeepsGoodColors()
    {
        // Already readable: unchanged, so the club color stays the club color.
        const QColor red(QStringLiteral("#dc052d"));
        QCOMPARE(withContrast(red, QColor(QStringLiteral("#ffffff")), 4.5), red);
    }

    void headerTextKeepsContrast_data()
    {
        QTest::addColumn<QString>("primary");
        QTest::addColumn<QString>("secondary");
        QTest::addColumn<QString>("base");
        const QStringList bases = {QStringLiteral("#1f1f1f"), QStringLiteral("#e7e9ed")};
        const QList<QPair<QString, QString>> clubs = {
            {QStringLiteral("#6a1bf5"), QStringLiteral("#9d6bff")}, // neutral purple
            {QStringLiteral("#14213d"), QStringLiteral("#fca311")}, // dark
            {QStringLiteral("#f4f4f4"), QStringLiteral("#1b1b1b")}, // light
            {QStringLiteral("#ffe600"), QStringLiteral("#101010")}, // vivid
            {QStringLiteral("#6cabdd"), QStringLiteral("#1c2c5b")}, // pale blue
            {QStringLiteral("#e32219"), QStringLiteral("#e32219")}, // one color
            {QStringLiteral("#808080"), QStringLiteral("#808080")}, // mid grey
        };
        for (const QString &base : bases) {
            for (const auto &[primary, secondary] : clubs)
                QTest::newRow(qPrintable(primary + QStringLiteral(" on ") + base))
                    << primary << secondary << base;
        }
    }
    void headerTextKeepsContrast()
    {
        QFETCH(QString, primary);
        QFETCH(QString, secondary);
        QFETCH(QString, base);
        const ClubHeaderPalette palette =
            clubHeaderPalette(QColor(primary), QColor(secondary), QColor(base));
        QVERIFY(palette.text == QColor(Qt::white) || palette.text == QColor(Qt::black));
        // Both ends and the middle of the gradient.
        for (const QColor &fill :
             {palette.start, palette.end, mixColors(palette.start, palette.end, 0.5)}) {
            QVERIFY2(contrastRatio(palette.text, fill) >= 4.5,
                     qPrintable(QStringLiteral("%1 on %2").arg(palette.text.name(), fill.name())));
            QVERIFY(contrastRatio(palette.mutedText, fill) >= 3.0);
        }
        QVERIFY(contrastRatio(palette.stripe, QColor(base)) >= 1.8);
    }

    void headerTamesWhiteOnDarkBase()
    {
        const ClubHeaderPalette night = clubHeaderPalette(
            QColor(QStringLiteral("#ffffff")), QColor(QStringLiteral("#000000")),
            QColor(QStringLiteral("#1f1f1f")));
        QVERIFY(relativeLuminance(night.start) <= 0.62);
        const ClubHeaderPalette day = clubHeaderPalette(
            QColor(QStringLiteral("#ffffff")), QColor(QStringLiteral("#000000")),
            QColor(QStringLiteral("#e7e9ed")));
        QVERIFY(relativeLuminance(day.start) > 0.62); // light mode keeps it white
    }

    void headerGradientStaysInOneHue()
    {
        // Red and blue club: no red-to-blue gradient, the blue is the stripe.
        const ClubHeaderPalette palette = clubHeaderPalette(
            QColor(QStringLiteral("#c8102e")), QColor(QStringLiteral("#00b2a9")),
            QColor(QStringLiteral("#1f1f1f")));
        QVERIFY(qAbs(palette.start.hslHueF() - palette.end.hslHueF()) < 0.02);
        QVERIFY(qAbs(palette.stripe.hslHueF() - QColor(QStringLiteral("#00b2a9")).hslHueF())
                < 0.02);
    }

    void templatesRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("color_templates.json"));
        {
            ColorTemplateStore store(path);
            QVERIFY(store.load()); // missing file = empty store
            QVERIFY(store.templates().isEmpty());
            const QString a = store.save(QStringLiteral("Hafen Grün"), QStringLiteral("Liga A"),
                                         sampleColors(QStringLiteral("#0b6e4f")));
            const QString b = store.save(QStringLiteral("Eigenes Blau"), QString(),
                                         sampleColors(QStringLiteral("#14213d")));
            QVERIFY(ColorTemplateStore::isTemplateId(a));
            QVERIFY(ColorTemplateStore::isTemplateId(b));
            QVERIFY(a != b);
        }
        // A new store on the same file (another save, or after an update).
        ColorTemplateStore reopened(path);
        QVERIFY(reopened.load());
        QCOMPARE(reopened.templates().size(), 2);
        QCOMPARE(reopened.leagues(), QStringList{QStringLiteral("Liga A")});
        const ColorTemplate &green = reopened.templates().first();
        QCOMPARE(green.name, QStringLiteral("Hafen Grün"));
        QCOMPARE(green.colors.value(QStringLiteral("night_primary_color")),
                 QStringLiteral("#0b6e4f"));
        QCOMPARE(green.colors.size(), ColorTemplateStore::colorKeys().size());
    }

    void templatesOverwriteSameNameAndLeague()
    {
        QTemporaryDir dir;
        ColorTemplateStore store(dir.filePath(QStringLiteral("t.json")));
        const QString first = store.save(QStringLiteral("Heim"), QStringLiteral("Liga A"),
                                         sampleColors(QStringLiteral("#111111")));
        const QString again = store.save(QStringLiteral("heim"), QStringLiteral("Liga A"),
                                         sampleColors(QStringLiteral("#222222")));
        QCOMPARE(again, first);
        QCOMPARE(store.templates().size(), 1);
        QCOMPARE(store.find(first)->colors.value(QStringLiteral("day_primary_color")),
                 QStringLiteral("#222222"));
        // Same name in another league is another template.
        store.save(QStringLiteral("Heim"), QString(), sampleColors(QStringLiteral("#333333")));
        QCOMPARE(store.templates().size(), 2);
    }

    void templatesRejectIncompleteAndRemove()
    {
        QTemporaryDir dir;
        ColorTemplateStore store(dir.filePath(QStringLiteral("t.json")));
        QHash<QString, QString> incomplete = sampleColors(QStringLiteral("#123456"));
        incomplete.remove(QStringLiteral("day_text_color"));
        QVERIFY(store.save(QStringLiteral("X"), QString(), incomplete).isEmpty());
        QVERIFY(store.save(QStringLiteral("  "), QString(), sampleColors("#123456")).isEmpty());
        QVERIFY(store.templates().isEmpty());

        const QString id = store.save(QStringLiteral("Y"), QString(), sampleColors("#123456"));
        QVERIFY(store.remove(id));
        QVERIFY(store.templates().isEmpty());
        ColorTemplateStore reopened(store.filePath());
        QVERIFY(reopened.load());
        QVERIFY(reopened.templates().isEmpty());
    }

    void brokenFileKeepsStoreUsable()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("t.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ not json");
        file.close();
        ColorTemplateStore store(path);
        QVERIFY(!store.load());
        QVERIFY(!store.lastError().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestThemeColors)
#include "test_themecolors.moc"
