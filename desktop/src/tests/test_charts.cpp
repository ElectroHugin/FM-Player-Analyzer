#include "core/AppConfig.h"
#include "theming/ThemeManager.h"
#include "widgets/Charts.h"

#include <QChart>
#include <QChartView>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QToolTip>
#include <QtTest>

using namespace fm;

// The development chart's hover: a crosshair over the nearest snapshot and one
// tooltip listing every series' value there.
class TestCharts : public QObject
{
    Q_OBJECT

private:
    static void hoverAt(LineChartWidget &widget, double snapshot, double value)
    {
        auto *view = widget.findChild<QChartView *>();
        QVERIFY(view && view->chart() && !view->chart()->series().isEmpty());
        QChart *chart = view->chart();
        const QPointF chartPos = chart->mapToPosition({snapshot, value}, chart->series().first());
        const QPoint viewPos = view->mapFromScene(chart->mapToScene(chartPos));
        QMouseEvent move(QEvent::MouseMove, viewPos, view->viewport()->mapToGlobal(viewPos),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &move);
    }

private slots:
    void hoverListsAllSeriesAtTheSnapshot()
    {
        QTemporaryDir dir;
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        ThemeManager theme(config);
        LineChartWidget widget(theme);
        widget.resize(900, 520);
        widget.setSeries({{QStringLiteral("Alpha"), {{0, 50}, {1, 52}, {2, 55}}, true},
                          {QStringLiteral("Beta"), {{1, 60}, {2, 58}}, false}},
                         {QStringLiteral("t0"), QStringLiteral("t1"), QStringLiteral("t2")},
                         LineChartWidget::ValueMode::Percent, {1});
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));

        hoverAt(widget, 2, 55);
        QTRY_VERIFY(QToolTip::isVisible());
        QString text = QToolTip::text();
        QVERIFY2(text.contains(QStringLiteral("Aktueller Stand")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("t2")));
        QVERIFY(text.contains(QStringLiteral("Alpha")) && text.contains(QStringLiteral("55 %")));
        QVERIFY(text.contains(QStringLiteral("Beta")) && text.contains(QStringLiteral("58 %")));
        QVERIFY(text.indexOf(QStringLiteral("Beta")) < text.indexOf(QStringLiteral("Alpha")));

        // One snapshot back: relative label and the recalculation note.
        hoverAt(widget, 1, 52);
        QTRY_VERIFY(QToolTip::text().contains(QStringLiteral("−1")));
        text = QToolTip::text();
        QVERIFY(text.contains(QStringLiteral("t1")));
        QVERIFY(text.contains(QStringLiteral("Neuberechnung")));

        // Snapshot 0: Beta had no rating yet and is left out.
        hoverAt(widget, 0, 50);
        QTRY_VERIFY(QToolTip::text().contains(QStringLiteral("t0")));
        QVERIFY(!QToolTip::text().contains(QStringLiteral("Beta")));
    }

    void deltaValuesAreSigned()
    {
        QTemporaryDir dir;
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        ThemeManager theme(config);
        LineChartWidget widget(theme);
        widget.resize(900, 520);
        widget.setSeries({{QStringLiteral("Kader"), {{0, 0.0}, {1, -1.5}, {2, 2.0}}, true}},
                         {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
                         LineChartWidget::ValueMode::Delta);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));

        hoverAt(widget, 1, -1.5);
        QTRY_VERIFY(QToolTip::text().contains(QStringLiteral("b")));
        QVERIFY2(QToolTip::text().contains(QStringLiteral("−1")), qPrintable(QToolTip::text()));
        hoverAt(widget, 0, 0.0);
        QTRY_VERIFY(QToolTip::text().contains(QStringLiteral("±0")));
    }
};

QTEST_MAIN(TestCharts)
#include "test_charts.moc"
