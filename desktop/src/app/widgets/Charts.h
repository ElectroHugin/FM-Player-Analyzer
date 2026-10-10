#pragma once

#include <QList>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QWidget>

class QChartView;
class QGraphicsLineItem;

namespace fm {

class ThemeManager;

// Theme-aware trace palette (legacy player_comparison palettes).
QList<QColor> chartTracePalette(const ThemeManager &theme);

// DWRS development chart on an evenly spaced snapshot axis: x is the index of
// an upload snapshot (core/Development), not a date. Tick labels count back
// from the newest snapshot ("now", "-5", ...). Hovering shows a crosshair with
// every visible series' value and the upload time; clicking a legend entry
// hides or shows its series.
class LineChartWidget : public QWidget
{
    Q_OBJECT

public:
    enum class ValueMode {
        Percent, // normalized DWRS 0-100
        Delta,   // change in percentage points (squad trend)
    };

    struct Series {
        QString name;
        QList<QPointF> points; // x = snapshot index, y = value
        bool emphasized = false;
    };

    explicit LineChartWidget(ThemeManager &theme, QWidget *parent = nullptr);

    // snapshotLabels: hover text per snapshot index (e.g. its upload time);
    // its last index is "now". markers: snapshot indexes drawn as dashed
    // vertical lines (recalculations).
    void setSeries(QList<Series> series, QStringList snapshotLabels,
                   ValueMode mode = ValueMode::Percent, QList<int> markers = {});
    void clearChart(const QString &message);
    // Title under the snapshot axis (default: uploads before the current state).
    void setAxisTitle(const QString &title);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();
    void updateHover(const QPoint &viewPos);
    void hideHover();

    ThemeManager &m_theme;
    QChartView *m_view = nullptr;
    QList<Series> m_series;
    QStringList m_labels;
    QList<int> m_markers;
    ValueMode m_mode = ValueMode::Percent;
    QString m_emptyMessage;
    QString m_axisTitle;
    int m_firstIndex = 0;
    int m_lastIndex = 0;
    int m_hoverIndex = -1;
    QGraphicsLineItem *m_crosshair = nullptr; // owned by the current chart
};

// Radar (polar) chart comparing players over attribute categories (0-20).
class RadarChartWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RadarChartWidget(ThemeManager &theme, QWidget *parent = nullptr);

    // categories: axis labels; traces: (player name, one value per category).
    void setData(const QStringList &categories,
                 const QList<QPair<QString, QList<double>>> &traces, double maxValue = 20.0);

private:
    void rebuild();

    ThemeManager &m_theme;
    QChartView *m_view = nullptr;
    QStringList m_categories;
    QList<QPair<QString, QList<double>>> m_traces;
    double m_maxValue = 20.0;
};

} // namespace fm
