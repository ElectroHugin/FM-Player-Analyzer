#include "core/AppConfig.h"
#include "theming/ThemeManager.h"
#include "widgets/Charts.h"

#include <QChart>
#include <QChartView>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QCategoryAxis>
#include <QLabel>
#include <QLocale>
#include <QtTest>

using namespace fm;

// The development chart's hover: a crosshair over the nearest snapshot and one
// panel listing every series' value there. The panel stays while the mouse is
// over the plot (a QToolTip closed by itself after a few seconds).
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
    static QLabel *panel(LineChartWidget &widget)
    {
        return widget.findChild<QLabel *>(QStringLiteral("chartHoverPanel"));
    }
    static QString panelText(LineChartWidget &widget)
    {
        QLabel *label = panel(widget);
        return label && label->isVisible() ? label->text() : QString();
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
        QTRY_VERIFY(!panelText(widget).isEmpty());
        QString text = panelText(widget);
        QVERIFY2(text.contains(QStringLiteral("Aktueller Stand")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("t2")));
        QVERIFY(text.contains(QStringLiteral("Alpha")) && text.contains(QStringLiteral("55 %")));
        QVERIFY(text.contains(QStringLiteral("Beta")) && text.contains(QStringLiteral("58 %")));
        QVERIFY(text.indexOf(QStringLiteral("Beta")) < text.indexOf(QStringLiteral("Alpha")));

        // One snapshot back: relative label and the recalculation note.
        hoverAt(widget, 1, 52);
        QTRY_VERIFY(panelText(widget).contains(QStringLiteral("−1")));
        text = panelText(widget);
        QVERIFY(text.contains(QStringLiteral("t1")));
        QVERIFY(text.contains(QStringLiteral("Neuberechnung")));

        // Snapshot 0: Beta had no rating yet and is left out.
        hoverAt(widget, 0, 50);
        QTRY_VERIFY(panelText(widget).contains(QStringLiteral("t0")));
        QVERIFY(!panelText(widget).contains(QStringLiteral("Beta")));
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
        QTRY_VERIFY(panelText(widget).contains(QStringLiteral("b")));
        QVERIFY2(panelText(widget).contains(QStringLiteral("−1")), qPrintable(panelText(widget)));
        hoverAt(widget, 0, 0.0);
        QTRY_VERIFY(panelText(widget).contains(QStringLiteral("±0")));
    }

    void gameDatesLabelAxisAndHeadThePanel()
    {
        QTemporaryDir dir;
        AppConfig config(dir.filePath(QStringLiteral("config.ini")));
        ThemeManager theme(config);
        LineChartWidget widget(theme);
        widget.resize(900, 520);
        const QDate first(2027, 1, 15), second(2027, 2, 1);
        widget.setSeries({{QStringLiteral("Alpha"), {{0, 50}, {1, 52}, {2, 55}}, true}},
                         {QStringLiteral("upload0"), QStringLiteral("upload1"), QStringLiteral("upload2")},
                         LineChartWidget::ValueMode::Percent, {}, {first, second, QDate()});
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));

        auto *view = widget.findChild<QChartView *>();
        QCategoryAxis *axis = nullptr;
        for (QAbstractAxis *candidate : view->chart()->axes(Qt::Horizontal))
            axis = axis ? axis : qobject_cast<QCategoryAxis *>(candidate);
        QVERIFY(axis);
        const QStringList labels = axis->categoriesLabels();
        QVERIFY2(labels.contains(QLocale().toString(first, QLocale::ShortFormat)), qPrintable(labels.join('|')));
        QVERIFY(labels.contains(QStringLiteral("jetzt"))); // newest snapshot has no date
        QVERIFY(!labels.contains(QStringLiteral("−2")));

        hoverAt(widget, 0, 50);
        QTRY_VERIFY(panelText(widget).contains(QStringLiteral("2027")));
        QVERIFY2(!panelText(widget).contains(QStringLiteral("upload0")), qPrintable(panelText(widget)));
        // Moving within the same snapshot keeps the panel open.
        hoverAt(widget, 0, 51);
        QVERIFY(!panelText(widget).isEmpty());
        hoverAt(widget, 2, 55);
        QTRY_VERIFY(panelText(widget).contains(QStringLiteral("upload2")));
    }
};

QTEST_MAIN(TestCharts)
#include "test_charts.moc"
