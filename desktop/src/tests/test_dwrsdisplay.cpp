#include <QtTest>
#include "core/DwrsDisplay.h"

using namespace fm;

class TestDwrsDisplay : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        setDwrsColorScheme(*builtinDwrsColorScheme(QStringLiteral("rarity")));
        setDwrsGoalkeeperRoles({QStringLiteral("GK")});
    }
    void formatsWithLanguageSeparator()
    {
        setDwrsDecimals(1);
        setDwrsLocale(QLocale(QLocale::German));
        QCOMPARE(formatDwrs(86.34), QStringLiteral("86,3"));
        QCOMPARE(formatDwrsPercent(100.0), QStringLiteral("100,0%"));
        QCOMPARE(formatDwrsDelta(1.26), QStringLiteral("+1,3"));
        QCOMPARE(formatDwrsDelta(-0.04), QStringLiteral("0,0"));
        setDwrsLocale(QLocale(QLocale::English));
        QCOMPARE(formatDwrs(86.35), QStringLiteral("86.4"));
        setDwrsDecimals(0);
        QCOMPARE(formatDwrs(86.5), QStringLiteral("87"));
        QCOMPARE(formatDwrsDelta(-2.4), QStringLiteral("-2"));
        QCOMPARE(roundDwrs(86.49), 86.0);
    }
    void schemesFollowThresholdsAndRoleKind()
    {
        setDwrsDecimals(1);
        const auto rarity = *builtinDwrsColorScheme(QStringLiteral("rarity"));
        QCOMPARE(dwrsCellStyle(54.9, QStringLiteral("AF")).background, rarity.stops[0].color);
        QVERIFY(!dwrsCellStyle(60.0, QStringLiteral("AF")).isValid()); // Common: no fill
        QCOMPARE(dwrsCellStyle(64.96, QStringLiteral("AF")).background, rarity.stops[2].color); // shown 65.0
        QCOMPARE(dwrsCellStyle(90.0, QStringLiteral("AF")).background, rarity.stops[5].color);
        QVERIFY(!dwrsCellStyle(50.0, QStringLiteral("GK")).isValid()); // keeper thresholds start at 48
        setDwrsGoalkeeperRoles({QStringLiteral("SK-IP")});
        QVERIFY(!dwrsCellStyle(50.0, QStringLiteral("SK")).isValid()); // shown without phase suffix
        setDwrsColorScheme(*builtinDwrsColorScheme(originalDwrsColorSchemeId()));
        QCOMPARE(dwrsCellStyle(90.0, QStringLiteral("AF")).background, rainbowCellStyle(90.0).background);
        QCOMPARE(originalDwrsColorSchemeId(), QStringLiteral("original"));
    }
    void blendMixesNeighboursNearBorders()
    {
        DwrsColorScheme scheme;
        scheme.id = QStringLiteral("t");
        scheme.blend = 2.0;
        scheme.stops = {{0, 0, QColor(0, 0, 0)}, {50, 40, QColor(200, 200, 200)}};
        QCOMPARE(scheme.style(40.0, false).background, QColor(0, 0, 0));
        QCOMPARE(scheme.style(60.0, false).background, QColor(200, 200, 200));
        QCOMPARE(scheme.style(50.0, false).background, QColor(100, 100, 100));
        QCOMPARE(scheme.style(40.0, true).background, QColor(100, 100, 100)); // keeper border
        scheme.blend = 0.0;
        QCOMPARE(scheme.style(49.9, false).background, QColor(0, 0, 0));
        QCOMPARE(scheme.style(50.0, false).background, QColor(200, 200, 200));
    }
    void builtinColorsAreReadable()
    {
        for (const auto &scheme : builtinDwrsColorSchemes()) {
            QVERIFY2(scheme.validate().isEmpty(), qPrintable(scheme.id));
            for (const auto &stop : scheme.stops) {
                if (!stop.color.isValid()) continue;
                const CellStyle style = dwrsReadableStyle(stop.color);
                QVERIFY2(contrastRatio(style.background, style.text) >= 4.5, qPrintable(stop.color.name()));
            }
        }
        for (const double delta : {-6.0, -2.0, 2.0, 6.0}) {
            const CellStyle style = dwrsComparisonStyle(delta);
            QVERIFY(style.isValid());
            QVERIFY(contrastRatio(style.background, style.text) >= 4.5);
        }
        QVERIFY(!dwrsComparisonStyle(1.5).isValid());
    }
    void ownSchemesRoundTrip()
    {
        QTemporaryDir dir;
        const auto original = *builtinDwrsColorScheme(originalDwrsColorSchemeId());
        DwrsColorScheme own = original.editableCopy(QStringLiteral("own-1"), QStringLiteral("Mine"));
        QVERIFY(!own.rainbow);
        QVERIFY(!own.builtin);
        QCOMPARE(own.stops.size(), 6);
        own.stops[1].color = QColor();
        const QString path = dir.filePath(QStringLiteral("schemes.json"));
        QString error;
        QVERIFY(saveDwrsColorSchemes(path, {own}, &error));
        const auto loaded = loadDwrsColorSchemes(path, &error);
        QCOMPARE(loaded.size(), 1);
        QVERIFY(loaded[0].sameColors(own));
        QCOMPARE(loaded[0].name, QStringLiteral("Mine"));
        QVERIFY(loadDwrsColorSchemes(dir.filePath(QStringLiteral("missing.json")), &error).isEmpty());
        DwrsColorScheme bad = own;
        bad.stops[2].field = bad.stops[1].field; // not ascending
        QVERIFY(!bad.validate().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDwrsDisplay)
#include "test_dwrsdisplay.moc"
