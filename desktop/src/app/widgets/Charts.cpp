#include "Charts.h"

#include "../theming/ThemeManager.h"

#include <QAreaSeries>
#include <QCategoryAxis>
#include <QChart>
#include <QChartView>
#include <QGraphicsLineItem>
#include <QGraphicsSimpleTextItem>
#include <QLegendMarker>
#include <QLineSeries>
#include <QLocale>
#include <QMouseEvent>
#include <QPolarChart>
#include <QToolTip>
#include <QVBoxLayout>
#include <QValueAxis>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fm {

namespace {

// QChartView::setChart does not delete the previous chart.
void swapChart(QChartView *view, QChart *chart, const ThemeManager &theme)
{
    // The chart itself is transparent, so the view behind it must carry the
    // theme's page color: its own default is the system's (light) base color,
    // on which the night theme's light legend and axis labels are unreadable.
    view->setBackgroundBrush(theme.background());
    view->setFrameShape(QFrame::NoFrame);
    QChart *old = view->chart();
    view->setChart(chart);
    delete old;
}

} // namespace

QList<QColor> chartTracePalette(const ThemeManager &theme)
{
    // Lead with the theme's interactive (accent) and primary colors, then
    // neutral/distinct tones — so the top role uses the club accent, the next
    // the club's main color, and further series stay readable.
    const QColor neutral = theme.mode() == QLatin1String("day") ? QColor(0x6B, 0x6B, 0x6B)
                                                                : QColor(0xCF, 0xCF, 0xCF);
    return {theme.interactive(), theme.primary(), neutral, QColor(0xFF, 0xA0, 0x00),
            QColor(0x4C, 0xAF, 0x50), QColor(0x42, 0xA5, 0xF5), QColor(0xAB, 0x47, 0xBC),
            QColor(0x26, 0xC6, 0xDA), QColor(0xEC, 0x40, 0x7A)};
}

namespace {

void applyChartTheme(QChart *chart, const ThemeManager &theme)
{
    chart->setBackgroundBrush(Qt::NoBrush);
    chart->setBackgroundPen(Qt::NoPen);
    chart->setPlotAreaBackgroundBrush(QColor(theme.secondaryBackground()));
    chart->setPlotAreaBackgroundVisible(true);
    chart->legend()->setLabelColor(theme.text());
    chart->setMargins(QMargins(4, 4, 4, 4));
}

void styleAxis(QAbstractAxis *axis, const ThemeManager &theme)
{
    axis->setLabelsColor(theme.text());
    axis->setTitleBrush(theme.text());
    const QColor grid = theme.mode() == QLatin1String("day") ? QColor(0, 0, 0, 50)
                                                             : QColor(255, 255, 255, 60);
    axis->setGridLineColor(grid);
    axis->setLinePenColor(grid);
}

} // namespace

// --- LineChartWidget ---

namespace {

// Distance between labelled snapshots, so that at most ~10 ticks carry a label.
int tickStep(int count)
{
    for (const int step : {1, 2, 5, 10, 20, 50, 100, 200, 500}) {
        if (count / step <= 10)
            return step;
    }
    return 1000;
}

// A "nice" value-axis interval giving roughly six ticks over the span.
double valueTickInterval(double span)
{
    for (const double interval : {0.5, 1.0, 2.0, 5.0, 10.0, 20.0}) {
        if (span / interval <= 7.0)
            return interval;
    }
    return 25.0;
}

QString formatValue(double value, LineChartWidget::ValueMode mode)
{
    const bool whole = std::abs(value - std::round(value)) < 0.05;
    if (mode == LineChartWidget::ValueMode::Percent)
        return QLocale().toString(value, 'f', whole ? 0 : 1) + QStringLiteral(" %");
    if (std::abs(value) < 0.05)
        return QStringLiteral("±0");
    const QString number = QLocale().toString(std::abs(value), 'f', whole ? 0 : 1);
    return (value < 0 ? QStringLiteral("−") : QStringLiteral("+")) + number;
}

} // namespace

LineChartWidget::LineChartWidget(ThemeManager &theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_view = new QChartView(this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setMinimumHeight(320);
    m_view->setMouseTracking(true);
    m_view->viewport()->setMouseTracking(true);
    m_view->viewport()->installEventFilter(this);
    layout->addWidget(m_view);
    connect(&m_theme, &ThemeManager::themeChanged, this, &LineChartWidget::rebuild);
    m_axisTitle = tr("Uploads vor dem aktuellen Stand");
    clearChart(tr("Keine Daten"));
}

void LineChartWidget::setAxisTitle(const QString &title)
{
    m_axisTitle = title;
}

void LineChartWidget::setSeries(QList<Series> series, QStringList snapshotLabels,
                                ValueMode mode, QList<int> markers)
{
    m_series = std::move(series);
    m_labels = std::move(snapshotLabels);
    m_mode = mode;
    m_markers = std::move(markers);
    m_emptyMessage = tr("Noch keine historischen DWRS-Daten vorhanden.");
    rebuild();
}

void LineChartWidget::clearChart(const QString &message)
{
    m_series.clear();
    m_labels.clear();
    m_markers.clear();
    m_emptyMessage = message;
    rebuild();
}

void LineChartWidget::rebuild()
{
    hideHover();
    m_crosshair = nullptr; // belongs to the chart replaced below

    auto *chart = new QChart;
    applyChartTheme(chart, m_theme);

    const QList<QColor> palette = chartTracePalette(m_theme);
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    int minX = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();

    QList<QLineSeries *> dataLines;
    for (int i = 0; i < m_series.size(); ++i) {
        const Series &series = m_series.at(i);
        auto *line = new QLineSeries(chart);
        line->setName(series.name);
        QPen pen(palette.at(i % palette.size()));
        pen.setWidthF(series.emphasized ? 3.5 : 2.0);
        line->setPen(pen);
        line->setPointsVisible(true);
        line->setMarkerSize(series.emphasized ? 8.0 : 6.0);
        for (const QPointF &point : series.points) {
            line->append(point);
            minY = std::min(minY, point.y());
            maxY = std::max(maxY, point.y());
            minX = std::min(minX, qRound(point.x()));
            maxX = std::max(maxX, qRound(point.x()));
        }
        chart->addSeries(line);
        dataLines << line;
    }
    if (minX > maxX) { // no series, or none with points
        chart->setTitle(m_emptyMessage);
        chart->setTitleBrush(m_theme.text());
        swapChart(m_view, chart, m_theme);
        return;
    }
    m_firstIndex = minX;
    m_lastIndex = maxX;

    if (m_mode == ValueMode::Delta) {
        minY = std::min(minY, 0.0);
        maxY = std::max(maxY, 0.0);
    }
    const double padding = std::max(1.0, (maxY - minY) * 0.12);
    double low = minY - padding;
    double high = maxY + padding;
    if (m_mode == ValueMode::Percent) {
        low = std::max(0.0, low);
        high = std::min(100.0, high);
    }

    const QColor guideColor = m_theme.mode() == QLatin1String("day") ? QColor(0, 0, 0, 90)
                                                                     : QColor(255, 255, 255, 90);
    const auto addGuide = [&](const QPointF &from, const QPointF &to, const QPen &pen,
                              const QString &name) {
        auto *guide = new QLineSeries(chart);
        guide->setName(name);
        guide->setPen(pen);
        guide->append(from);
        guide->append(to);
        chart->addSeries(guide);
        return guide;
    };
    QList<QLineSeries *> hiddenGuides;
    if (m_mode == ValueMode::Delta) {
        QPen zeroPen(guideColor);
        zeroPen.setWidthF(1.5);
        hiddenGuides << addGuide({minX - 0.5, 0.0}, {maxX + 0.5, 0.0}, zeroPen, QString());
    }
    bool markerInLegend = false;
    for (const int index : std::as_const(m_markers)) {
        if (index < minX || index > maxX)
            continue;
        QPen markerPen(QColor(0xFF, 0xA0, 0x00));
        markerPen.setStyle(Qt::DashLine);
        markerPen.setWidthF(1.5);
        QLineSeries *marker = addGuide({double(index), low}, {double(index), high}, markerPen,
                                       tr("Neuberechnung"));
        if (markerInLegend)
            hiddenGuides << marker; // one legend entry for all of them
        markerInLegend = true;
    }

    // X: snapshot indexes, labelled by their distance to the newest snapshot.
    const int now = m_labels.isEmpty() ? maxX : static_cast<int>(m_labels.size()) - 1;
    auto *xAxis = new QCategoryAxis(chart);
    xAxis->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
    xAxis->setStartValue(minX - 0.3);
    xAxis->setRange(minX - 0.3, maxX + 0.3);
    const int step = tickStep(maxX - minX + 1);
    for (int index = minX; index <= maxX; ++index) {
        if ((now - index) % step != 0)
            continue;
        xAxis->append(index == now ? tr("jetzt") : QStringLiteral("−%1").arg(now - index), index);
    }
    xAxis->setTitleText(m_axisTitle);

    auto *yAxis = new QValueAxis(chart);
    yAxis->setRange(low, high);
    const double interval = valueTickInterval(high - low);
    yAxis->setTickType(QValueAxis::TicksDynamic);
    yAxis->setTickAnchor(0.0);
    yAxis->setTickInterval(interval);
    if (m_mode == ValueMode::Percent)
        yAxis->setLabelFormat(QStringLiteral("%d%%"));
    else
        yAxis->setLabelFormat(interval < 1.0 ? QStringLiteral("%+.1f") : QStringLiteral("%+.0f"));
    styleAxis(xAxis, m_theme);
    styleAxis(yAxis, m_theme);
    chart->addAxis(xAxis, Qt::AlignBottom);
    chart->addAxis(yAxis, Qt::AlignLeft);
    for (QAbstractSeries *series : chart->series()) {
        series->attachAxis(xAxis);
        series->attachAxis(yAxis);
    }

    // Guides stay out of the legend; a data entry toggles its series.
    for (QLineSeries *guide : std::as_const(hiddenGuides)) {
        for (QLegendMarker *marker : chart->legend()->markers(guide))
            marker->setVisible(false);
    }
    for (QLineSeries *line : std::as_const(dataLines)) {
        for (QLegendMarker *marker : chart->legend()->markers(line)) {
            connect(marker, &QLegendMarker::clicked, this, [this, marker] {
                QAbstractSeries *series = marker->series();
                series->setVisible(!series->isVisible());
                marker->setVisible(true); // hiding a series hides its entry too
                QColor label = m_theme.text();
                label.setAlphaF(series->isVisible() ? 1.0f : 0.4f);
                marker->setLabelBrush(label);
                hideHover();
            });
        }
    }

    // Long role and player names fit a vertical legend beside the plot.
    chart->legend()->setAlignment(Qt::AlignRight);

    swapChart(m_view, chart, m_theme);

    m_crosshair = new QGraphicsLineItem(chart);
    QPen crosshairPen(guideColor);
    crosshairPen.setStyle(Qt::DashLine);
    m_crosshair->setPen(crosshairPen);
    m_crosshair->setZValue(10);
    m_crosshair->hide();
}

bool LineChartWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view->viewport()) {
        if (event->type() == QEvent::MouseMove)
            updateHover(static_cast<QMouseEvent *>(event)->position().toPoint());
        else if (event->type() == QEvent::Leave)
            hideHover();
    }
    return QWidget::eventFilter(watched, event);
}

void LineChartWidget::updateHover(const QPoint &viewPos)
{
    QChart *chart = m_view->chart();
    if (!chart || !m_crosshair || chart->series().isEmpty())
        return;
    const QPointF chartPos = chart->mapFromScene(m_view->mapToScene(viewPos));
    const QRectF plot = chart->plotArea();
    if (!plot.contains(chartPos)) {
        hideHover();
        return;
    }
    QAbstractSeries *reference = chart->series().first();
    const int index = std::clamp(qRound(chart->mapToValue(chartPos, reference).x()),
                                 m_firstIndex, m_lastIndex);
    if (index == m_hoverIndex)
        return;
    m_hoverIndex = index;

    const qreal x = chart->mapToPosition(QPointF(index, 0.0), reference).x();
    m_crosshair->setLine(x, plot.top(), x, plot.bottom());
    m_crosshair->show();

    // One row per visible series with a value at this snapshot, best first.
    const QList<QColor> palette = chartTracePalette(m_theme);
    QList<std::pair<double, QString>> rows;
    const QList<QAbstractSeries *> chartSeries = chart->series();
    for (int i = 0; i < m_series.size() && i < chartSeries.size(); ++i) {
        if (!chartSeries.at(i)->isVisible())
            continue;
        for (const QPointF &point : m_series.at(i).points) {
            if (qRound(point.x()) != index)
                continue;
            rows.append({point.y(),
                         QStringLiteral("<span style='color:%1'>●</span> %2: <b>%3</b>")
                             .arg(palette.at(i % palette.size()).name(),
                                  m_series.at(i).name.toHtmlEscaped(),
                                  formatValue(point.y(), m_mode))});
            break;
        }
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });

    const int now = m_labels.isEmpty() ? m_lastIndex : static_cast<int>(m_labels.size()) - 1;
    QString html = QStringLiteral("<b>%1</b>")
                       .arg(index == now ? tr("Aktueller Stand")
                                         : tr("Datenstand −%1").arg(now - index));
    if (index >= 0 && index < m_labels.size())
        html += QStringLiteral(" · %1").arg(m_labels.at(index).toHtmlEscaped());
    for (const auto &row : std::as_const(rows))
        html += QStringLiteral("<br>") + row.second;
    if (m_markers.contains(index))
        html += QStringLiteral("<br><i>%1</i>")
                    .arg(tr("Neuberechnung: herausgerechnet, frühere Werte sind auf die "
                            "neue Berechnung umgerechnet."));
    QToolTip::showText(m_view->viewport()->mapToGlobal(viewPos) + QPoint(18, 18), html,
                       m_view->viewport());
}

void LineChartWidget::hideHover()
{
    if (m_crosshair)
        m_crosshair->hide();
    if (m_hoverIndex >= 0)
        QToolTip::hideText();
    m_hoverIndex = -1;
}

// --- RadarChartWidget ---

RadarChartWidget::RadarChartWidget(ThemeManager &theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_view = new QChartView(this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setMinimumHeight(360);
    layout->addWidget(m_view);
    connect(&m_theme, &ThemeManager::themeChanged, this, &RadarChartWidget::rebuild);
    rebuild();
}

void RadarChartWidget::setData(const QStringList &categories,
                               const QList<QPair<QString, QList<double>>> &traces,
                               double maxValue)
{
    m_categories = categories;
    m_traces = traces;
    m_maxValue = maxValue;
    rebuild();
}

void RadarChartWidget::rebuild()
{
    auto *chart = new QPolarChart;
    applyChartTheme(chart, m_theme);

    const int n = static_cast<int>(m_categories.size());
    if (n == 0 || m_traces.isEmpty()) {
        chart->setTitle(tr("Spieler auswählen, um den Vergleich zu sehen."));
        chart->setTitleBrush(m_theme.text());
        swapChart(m_view, chart, m_theme);
        return;
    }

    auto *angularAxis = new QCategoryAxis(chart);
    angularAxis->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
    angularAxis->setRange(0, 360);
    for (int i = 0; i < n; ++i)
        angularAxis->append(m_categories.at(i), i * 360.0 / n);
    auto *radialAxis = new QValueAxis(chart);
    radialAxis->setRange(0, m_maxValue);
    radialAxis->setLabelFormat(QStringLiteral("%d"));
    styleAxis(angularAxis, m_theme);
    styleAxis(radialAxis, m_theme);
    chart->addAxis(angularAxis, QPolarChart::PolarOrientationAngular);
    chart->addAxis(radialAxis, QPolarChart::PolarOrientationRadial);

    const QList<QColor> palette = chartTracePalette(m_theme);
    int index = 0;
    for (const auto &[name, values] : m_traces) {
        auto *line = new QLineSeries(chart);
        line->setName(name);
        for (int i = 0; i < n && i < values.size(); ++i)
            line->append(i * 360.0 / n, values.at(i));
        if (!values.isEmpty())
            line->append(360.0, values.first()); // close the shape

        const QColor color = palette.at(index % palette.size());
        auto *area = new QAreaSeries(line);
        area->setName(name);
        QColor fill = color;
        fill.setAlphaF(0.2f);
        area->setBrush(fill);
        QPen pen(color);
        pen.setWidth(2);
        area->setPen(pen);

        chart->addSeries(area);
        // The legend swatch would take the faint fill; show the line color.
        for (QLegendMarker *marker : chart->legend()->markers(area)) {
            marker->setBrush(color);
            marker->setPen(QPen(color));
        }
        area->attachAxis(angularAxis);
        area->attachAxis(radialAxis);
        ++index;
    }

    swapChart(m_view, chart, m_theme);
}

} // namespace fm
