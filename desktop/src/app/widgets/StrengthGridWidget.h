#pragma once

#include <QHash>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace fm {

class ThemeManager;

// Miniaturized, color-coded tactical pitch showing the average, min and max
// DWRS per position of the selected tactic. Colors are relative to the
// squad's own strength (red = weakest position, green = strongest).
// Port of legacy ui_components.display_strength_grid.
class StrengthGridWidget : public QWidget
{
    Q_OBJECT

public:
    struct SlotStrength {
        double avg = 0.0;
        double min = 0.0;
        double max = 0.0;
    };

    explicit StrengthGridWidget(ThemeManager &theme, QWidget *parent = nullptr);

    // layout: stratum name -> tactic slots (Definitions::tacticLayouts()[tactic]).
    void setData(const QHash<QString, SlotStrength> &strengths,
                 const QHash<QString, QStringList> &layout);
    void clearData();

    // Starter view (club overview, 2.2.3): one marker per starter at a
    // normalized pitch anchor (PitchGeometry::slotAnchors / combinedAnchor),
    // the starter's DWRS large, the change of the whole XI if he is out
    // small underneath. Overlapping markers are moved apart and keep a thin
    // line to their anchor. A marker without uid is an empty place.
    struct Marker {
        QString uid;         // "" = empty place
        QPointF anchor;      // normalized (0..1, attack at the top)
        QString header;      // role abbreviation(s)
        double value = 0.0;  // starter DWRS
        QString impact;      // secondary line
        QString tooltip;     // who comes on, who moves
    };
    void setMarkers(const QList<Marker> &markers);
    // Width in pixels at the pitch ratio (default 280).
    void setPitchWidth(int width);

    // FM version whose pitch geometry places the slots (empty = default).
    void setFmVersionId(const QString &versionId) { m_fmVersionId = versionId; }

    bool hasData() const { return !m_layout.isEmpty(); }

signals:
    void playerDoubleClicked(const QString &uid);
    void playerContextMenuRequested(const QString &uid, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override; // tooltips
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QColor relativeColor(double value, double minAvg, double maxAvg) const;
    void paintMarkers(QPainter &painter, const QRectF &field, bool night);
    int markerAt(const QPoint &pos) const;

    bool m_markerMode = false;
    QList<Marker> m_markers;
    QList<QRectF> m_markerRects; // painted rects, same order as m_markers

    ThemeManager &m_theme;
    QHash<QString, SlotStrength> m_strengths;
    QHash<QString, QStringList> m_layout;
    QString m_fmVersionId;
};

} // namespace fm
