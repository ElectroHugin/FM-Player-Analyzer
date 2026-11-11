#include <QtTest>
#include "core/DwrsDisplay.h"

using namespace fm;

class TestDwrsDisplay : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        setDwrsColorMode(DwrsColorMode::Tiers);
        setDwrsTierColors(defaultDwrsTierColors());
        setDwrsTierThresholds(defaultFieldTierThresholds(false), defaultGoalkeeperTierThresholds());
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
    void tiersFollowThresholdsAndRoleKind()
    {
        setDwrsDecimals(1);
        QCOMPARE(dwrsTier(54.9), 0);
        QCOMPARE(dwrsTier(55.0), 1);
        QCOMPARE(dwrsTier(64.96), 2); // shown as 65.0
        QCOMPARE(dwrsTier(83.9), 4);
        QCOMPARE(dwrsTier(84.0), 5);
        QCOMPARE(dwrsTier(50.0, true), 1); // goalkeeper thresholds start at 48
        QVERIFY(!dwrsCellStyle(60.0, QStringLiteral("AF")).isValid()); // Common: no fill
        QVERIFY(dwrsCellStyle(50.0, QStringLiteral("GK")).isValid() == false);
        QCOMPARE(dwrsCellStyle(90.0, QStringLiteral("AF")).background, defaultDwrsTierColors()[5]);
        setDwrsColorMode(DwrsColorMode::Rainbow);
        QCOMPARE(dwrsCellStyle(90.0, QStringLiteral("AF")).background, rainbowCellStyle(90.0).background);
    }
    void defaultTierColorsAreReadable()
    {
        for (int tier = 0; tier < kDwrsTierCount; ++tier) {
            const CellStyle style = dwrsTierStyle(tier);
            if (!style.isValid()) continue;
            QVERIFY2(contrastRatio(style.background, style.text) >= 4.5, qPrintable(style.background.name()));
        }
        for (const double delta : {-6.0, -2.0, 2.0, 6.0}) {
            const CellStyle style = dwrsComparisonStyle(delta);
            QVERIFY(style.isValid());
            QVERIFY(contrastRatio(style.background, style.text) >= 4.5);
        }
        QVERIFY(!dwrsComparisonStyle(1.5).isValid());
        QVERIFY(!dwrsComparisonStyle(-1.2).isValid());
    }
    void colorsRoundTrip()
    {
        auto colors = defaultDwrsTierColors();
        colors[3] = QColor(0x12, 0x34, 0x56);
        const auto restored = colorsFromString(colorsToString(colors));
        for (int i = 0; i < kDwrsTierCount; ++i) QCOMPARE(restored[i], colors[i]);
        QCOMPARE(colorsFromString(QStringLiteral("junk"))[0], defaultDwrsTierColors()[0]);
    }
};

QTEST_GUILESS_MAIN(TestDwrsDisplay)
#include "test_dwrsdisplay.moc"
