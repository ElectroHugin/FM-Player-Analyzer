#include <QtTest>

#include "core/Utils.h"

using namespace fm;

class TestUtils : public QObject
{
    Q_OBJECT

private slots:
    void valueToFloat_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<double>("expected");

        QTest::newRow("millions") << QStringLiteral("€1.2M") << 1'200'000.0;
        QTest::newRow("thousands") << QStringLiteral("€500K") << 500'000.0;
        QTest::newRow("plain") << QStringLiteral("€750") << 750.0;
        QTest::newRow("range takes lower") << QStringLiteral("€500K - €800K") << 500'000.0;
        // Backlog #36: saves in other currencies must not collapse to 0.
        QTest::newRow("pounds") << QStringLiteral("£15M") << 15'000'000.0;
        QTest::newRow("dollars") << QStringLiteral("$500K") << 500'000.0;
        QTest::newRow("pound range") << QStringLiteral("£36M - £43M") << 36'000'000.0;
        QTest::newRow("not for sale") << QStringLiteral("Not for Sale") << 2'000'000'000.0;
        QTest::newRow("not for sale lowercase") << QStringLiteral("not for sale") << 2'000'000'000.0;
        QTest::newRow("empty") << QString() << 0.0;
        QTest::newRow("garbage") << QStringLiteral("Unknown") << 0.0;
        QTest::newRow("no euro sign") << QStringLiteral("2.5M") << 2'500'000.0;
    }

    void valueToFloat()
    {
        QFETCH(QString, input);
        QFETCH(double, expected);
        QCOMPARE(fm::valueToFloat(input), expected);
    }

    void isExplicitZeroValue_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("expected");

        QTest::newRow("euro zero") << QStringLiteral("€0") << true;
        QTest::newRow("plain zero") << QStringLiteral("0") << true;
        QTest::newRow("zero range") << QStringLiteral("€0 - €0") << true;
        QTest::newRow("empty is not explicit") << QString() << false;
        QTest::newRow("dash is not explicit") << QStringLiteral("-") << false;
        QTest::newRow("nonzero") << QStringLiteral("€500K") << false;
        QTest::newRow("not for sale") << QStringLiteral("Not for Sale") << false;
    }

    void isExplicitZeroValue()
    {
        QFETCH(QString, input);
        QFETCH(bool, expected);
        QCOMPARE(fm::isExplicitZeroValue(input), expected);
    }

    void isFreeAgent_data()
    {
        QTest::addColumn<QString>("club");
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("expected");

        QTest::newRow("no club") << QString() << QStringLiteral("€2M") << true;
        QTest::newRow("dash club") << QStringLiteral("-") << QStringLiteral("€2M") << true;
        QTest::newRow("explicit zero value") << QStringLiteral("Real Madrid") << QStringLiteral("€0") << true;
        QTest::newRow("clubbed and valued") << QStringLiteral("FC Bayern") << QStringLiteral("€65M") << false;
        QTest::newRow("clubbed, missing value") << QStringLiteral("FC Bayern") << QString() << false;
        QTest::newRow("free-agent labelled club, zero value")
            << QStringLiteral("Free Agents") << QStringLiteral("€0") << true;
        // Written by the departure dialog; the old value may still be set.
        QTest::newRow("departure tag FrA") << QStringLiteral("FrA") << QStringLiteral("€2M") << true;
        QTest::newRow("departure tag, other case")
            << QStringLiteral(" fra ") << QStringLiteral("€2M") << true;
    }

    void isFreeAgent()
    {
        QFETCH(QString, club);
        QFETCH(QString, value);
        QFETCH(bool, expected);
        QCOMPARE(fm::isFreeAgent(club, value), expected);
    }

    void containsFolded()
    {
        // Backlog #36: table name filters match like the player search.
        const QString muller = fm::foldForSearch(QStringLiteral("muller"));
        QVERIFY(fm::containsFolded(QStringLiteral("Thomas Müller"), muller));
        QVERIFY(fm::containsFolded(QStringLiteral("Thomas Muller"), muller)); // ASCII fast path
        QVERIFY(fm::containsFolded(QStringLiteral("THOMAS MULLER"), muller));
        QVERIFY(fm::containsFolded(QStringLiteral("Erling Håland"),
                                   fm::foldForSearch(QStringLiteral("haland"))));
        QVERIFY(fm::containsFolded(QStringLiteral("Gießen"),
                                   fm::foldForSearch(QStringLiteral("giessen"))));
        QVERIFY(!fm::containsFolded(QStringLiteral("Thomas Müller"),
                                    fm::foldForSearch(QStringLiteral("meier"))));
        QVERIFY(fm::containsFolded(QStringLiteral("anyone"), QString())); // empty = all
    }

    void csvHelpers()
    {
        // Backlog #36: Excel's list separator follows the regional settings.
        QCOMPARE(fm::csvSeparator(QLocale(QLocale::German, QLocale::Germany)), QLatin1Char(';'));
        QCOMPARE(fm::csvSeparator(QLocale(QLocale::English, QLocale::UnitedStates)),
                 QLatin1Char(','));
        QCOMPARE(fm::csvField(QStringLiteral("plain"), QLatin1Char(';')), QStringLiteral("plain"));
        QCOMPARE(fm::csvField(QStringLiteral("7,39"), QLatin1Char(';')), QStringLiteral("7,39"));
        QCOMPARE(fm::csvField(QStringLiteral("7,39"), QLatin1Char(',')), QStringLiteral("\"7,39\""));
        QCOMPARE(fm::csvField(QStringLiteral("a;b"), QLatin1Char(';')), QStringLiteral("\"a;b\""));
        QCOMPARE(fm::csvField(QStringLiteral("say \"hi\""), QLatin1Char(',')),
                 QStringLiteral("\"say \"\"hi\"\"\""));
    }

    void parsePositionString_complex()
    {
        const auto result = fm::parsePositionString(QStringLiteral("AM (RL), ST (C)"));
        const QSet<QString> expected = {QStringLiteral("AM (R)"), QStringLiteral("AM (L)"),
                                        QStringLiteral("ST (C)")};
        QCOMPARE(result, expected);
    }

    void parsePositionString_slashBases()
    {
        const auto result = fm::parsePositionString(QStringLiteral("D/WB (R)"));
        const QSet<QString> expected = {QStringLiteral("D (R)"), QStringLiteral("WB (R)")};
        QCOMPARE(result, expected);
    }

    void parsePositionString_bareStBecomesCentral()
    {
        const auto result = fm::parsePositionString(QStringLiteral("ST"));
        QCOMPARE(result, QSet<QString>{QStringLiteral("ST (C)")});
    }

    void parsePositionString_sidelessStaysBare()
    {
        const auto result = fm::parsePositionString(QStringLiteral("DM, M (C)"));
        const QSet<QString> expected = {QStringLiteral("DM"), QStringLiteral("M (C)")};
        QCOMPARE(result, expected);
    }

    void parsePositionString_empty()
    {
        QVERIFY(fm::parsePositionString(QString()).isEmpty());
    }

    void positionSortKey_pitchOrder()
    {
        // Goalkeeper to striker, right before left; a wider range of lines
        // sorts after the pure position of its most defensive line.
        const QStringList ordered = {
            QStringLiteral("GK"),
            QStringLiteral("D (R)"),
            QStringLiteral("D (RC)"),
            QStringLiteral("D (C)"),
            QStringLiteral("D (LC)"),
            QStringLiteral("D (L)"),
            QStringLiteral("D (RL), WB (L)"),
            QStringLiteral("D (C), DM"),
            QStringLiteral("D (RL), AM (R)"),
            QStringLiteral("D/WB/AM (L)"),
            QStringLiteral("DM, M (C)"),
            QStringLiteral("M/AM (R)"),
            QStringLiteral("M/AM (C)"),
            QStringLiteral("M/AM (L)"),
            QStringLiteral("M (L), AM (RL), ST (C)"),
            QStringLiteral("AM (R), ST (C)"),
            QStringLiteral("AM (C), ST (C)"),
            QStringLiteral("ST (C)"),
            QString(), // unknown position last
        };
        for (int i = 1; i < ordered.size(); ++i) {
            QVERIFY2(fm::positionSortKey(ordered[i - 1]) < fm::positionSortKey(ordered[i]),
                     qPrintable(ordered[i - 1] + QStringLiteral(" !< ") + ordered[i]));
        }
        QCOMPARE(fm::positionSortKey(QStringLiteral("ST")),
                 fm::positionSortKey(QStringLiteral("ST (C)")));
        QCOMPARE(fm::positionSortKey(QStringLiteral("???")), fm::positionSortKey(QString()));
    }

    void getLastName()
    {
        QCOMPARE(fm::getLastName(QStringLiteral("Erling Braut Haaland")),
                 QStringLiteral("Haaland"));
        QCOMPARE(fm::getLastName(QStringLiteral("Pelé")), QStringLiteral("Pelé"));
        QCOMPARE(fm::getLastName(QString()), QString());
    }

    void foldForSearch()
    {
        // Diacritics fold to their base letter, both directions match.
        QCOMPARE(fm::foldForSearch(QStringLiteral("Müller")), QStringLiteral("muller"));
        QCOMPARE(fm::foldForSearch(QStringLiteral("Muller")), QStringLiteral("muller"));
        QCOMPARE(fm::foldForSearch(QStringLiteral("Håland")), QStringLiteral("haland"));
        QCOMPARE(fm::foldForSearch(QStringLiteral("Pelé")), QStringLiteral("pele"));
        // Non-decomposing Latin letters and the ß ligature.
        QCOMPARE(fm::foldForSearch(QStringLiteral("Gießen")), QStringLiteral("giessen"));
        QCOMPARE(fm::foldForSearch(QStringLiteral("Łukasz")), QStringLiteral("lukasz"));
        QCOMPARE(fm::foldForSearch(QStringLiteral("Ødegaard")), QStringLiteral("odegaard"));
        QCOMPARE(fm::foldForSearch(QString()), QString());
    }

    void contrastRatio()
    {
        // Black on white is the WCAG maximum, 21:1.
        QCOMPARE(fm::contrastRatio(QColorConstants::Black, QColorConstants::White), 21.0);
        // Identical colors have ratio 1.
        QCOMPARE(fm::contrastRatio(QColorConstants::White, QColorConstants::White), 1.0);
    }

    void attributeCellStyle_tiers()
    {
        QCOMPARE(fm::attributeCellStyle(20).background.name(), QStringLiteral("#0da025"));
        QCOMPARE(fm::attributeCellStyle(18).background.name(), QStringLiteral("#0da025"));
        QCOMPARE(fm::attributeCellStyle(17).background.name(), QStringLiteral("#a2d31a"));
        QCOMPARE(fm::attributeCellStyle(14).background.name(), QStringLiteral("#d8d21e"));
        QCOMPARE(fm::attributeCellStyle(11).background.name(), QStringLiteral("#ca7b3a"));
        QCOMPARE(fm::attributeCellStyle(7).background.name(), QStringLiteral("#cf1e1e"));
        QVERIFY(!fm::attributeCellStyle(0).isValid());
    }

    void dwrsCellStyle_endpoints()
    {
        // vmin -> first LUT entry (red end of gist_rainbow: 255, 0, 41).
        const auto low = fm::dwrsCellStyle(30.0);
        QCOMPARE(low.background.red(), 255);
        QCOMPARE(low.background.green(), 0);
        // vmax -> last LUT entry (magenta end: 255, 0, 191).
        const auto high = fm::dwrsCellStyle(95.0);
        QCOMPARE(high.background.red(), 255);
        QCOMPARE(high.background.blue(), 191);
        // Out-of-range values clamp.
        QCOMPARE(fm::dwrsCellStyle(120.0).background, high.background);
        QCOMPARE(fm::dwrsCellStyle(0.0).background, low.background);
    }
};

QTEST_APPLESS_MAIN(TestUtils)
#include "test_utils.moc"
