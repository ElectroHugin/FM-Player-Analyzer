#include "StrengthGridWidget.h"
#include "core/DwrsDisplay.h"

#include "../theming/ThemeManager.h"
#include "core/Constants.h"
#include "core/Utils.h"

#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QPainterPath>
#include <QSet>

#include <algorithm>

namespace fm {

StrengthGridWidget::StrengthGridWidget(ThemeManager &theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
{
    // Legacy mini-pitch: max-width 280px at a 68:105 pitch aspect ratio.
    setFixedSize(280, 280 * 105 / 68);
    setMouseTracking(true);
    connect(&m_theme, &ThemeManager::themeChanged, this,
            qOverload<>(&QWidget::update));
}

void StrengthGridWidget::setPitchWidth(int width)
{
    setFixedSize(width, width * 105 / 68);
}

void StrengthGridWidget::setMarkers(const QList<Marker> &markers)
{
    m_markerMode = true;
    m_markers = markers;
    m_markerRects.clear();
    m_strengths.clear();
    m_layout.clear();
    update();
}

void StrengthGridWidget::setData(const QHash<QString, SlotStrength> &strengths,
                                 const QHash<QString, QStringList> &layout)
{
    m_markerMode = false;
    m_markers.clear();
    m_strengths = strengths;
    m_layout = layout;
    update();
}

void StrengthGridWidget::setColorMode(ColorMode mode)
{
    m_colorMode = mode;
    update();
}

double StrengthGridWidget::shownMedian() const
{
    std::vector<double> values;
    if (m_markerMode) {
        for (const Marker &marker : m_markers)
            if (!marker.uid.isEmpty()) values.push_back(marker.value);
    } else {
        for (const SlotStrength &stats : m_strengths)
            if (stats.avg > 0.0) values.push_back(stats.avg);
    }
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

CellStyle StrengthGridWidget::cardStyle(double value, bool goalkeeper, double median, bool night) const
{
    if (m_colorMode == ColorMode::SquadComparison) {
        // Near the median: a calm grey in both themes, never the dark empty card.
        const CellStyle style = dwrsComparisonStyle(value - median);
        return style.isValid() ? style : dwrsComparisonNeutralStyle();
    }
    CellStyle style = dwrsCellStyleFor(value, goalkeeper);
    if (!style.isValid()) {
        style.background = night ? QColor(0x12, 0x16, 0x1c) : QColor(0xfb, 0xfb, 0xfd);
        style.text = night ? QColor(Qt::white) : QColor(0x1e, 0x22, 0x2b);
    }
    return style;
}

void StrengthGridWidget::clearData()
{
    m_markerMode = false;
    m_markers.clear();
    m_markerRects.clear();
    m_strengths.clear();
    m_layout.clear();
    update();
}

void StrengthGridWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Same pitch as the Best XI (TacticPitchWidget): dark green with mown
    // stripes, compact cards at their place, lines close up without holes.
    const bool night = m_theme.mode() != QLatin1String("day");
    const QColor pitchBg = night ? QColor(0x0f, 0x4a, 0x3c) : QColor(0x2e, 0x7d, 0x5b);
    const QColor stripe = night ? QColor(255, 255, 255, 10) : QColor(255, 255, 255, 18);
    const QColor markings = QColor(255, 255, 255, night ? 90 : 120);
    const QColor emptyCard = night ? QColor(0x12, 0x16, 0x1c) : QColor(0xfb, 0xfb, 0xfd);
    const QColor cardText = QColor(0x1e, 0x22, 0x2b);

    const QRectF pitch = rect().adjusted(1, 1, -1, -1);
    QPainterPath pitchPath;
    pitchPath.addRoundedRect(pitch, 10, 10);
    painter.fillPath(pitchPath, pitchBg);
    painter.setClipPath(pitchPath);
    const int stripes = 10;
    for (int i = 0; i < stripes; i += 2)
        painter.fillRect(QRectF(pitch.left(), pitch.top() + i * pitch.height() / stripes,
                                pitch.width(), pitch.height() / stripes),
                         stripe);
    const QRectF field = pitch.adjusted(6, 6, -6, -6);
    painter.setPen(QPen(markings, 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(field);
    const double cx = field.center().x();
    painter.drawLine(QPointF(field.left(), field.center().y()),
                     QPointF(field.right(), field.center().y()));
    painter.drawEllipse(field.center(), field.width() * 0.14, field.width() * 0.14);
    const double penW = field.width() * 0.44, penH = field.height() * 0.16;
    painter.drawRect(QRectF(cx - penW / 2, field.top(), penW, penH));
    painter.drawRect(QRectF(cx - penW / 2, field.bottom() - penH, penW, penH));
    const double goalW = field.width() * 0.20, goalH = field.height() * 0.06;
    painter.drawRect(QRectF(cx - goalW / 2, field.top(), goalW, goalH));
    painter.drawRect(QRectF(cx - goalW / 2, field.bottom() - goalH, goalW, goalH));

    if (m_markerMode && !m_markers.isEmpty()) {
        paintMarkers(painter, field, night);
        return;
    }
    if (m_layout.isEmpty()) {
        painter.setPen(Qt::white);
        painter.drawText(pitch, Qt::AlignCenter, tr("Keine Daten"));
        return;
    }

    const double cardW = field.width() * 0.188;
    const double cardH = field.height() * 0.115;
    static const double kRowY[] = {0.0, 0.085, 0.255, 0.42, 0.585, 0.75, 0.915};
    const auto xFraction = [](int displayColumn, const QSet<int> &occupied) {
        const bool closeUp = occupied.contains(1) && occupied.contains(3) && !occupied.contains(2);
        if (closeUp && (displayColumn == 1 || displayColumn == 3))
            return displayColumn == 1 ? 0.37 : 0.63;
        return 0.1 + 0.2 * displayColumn;
    };
    const auto cardRectAt = [&](int row, double xf) {
        const double x = field.left() + field.width() * xf;
        const double y = field.top() + field.height() * kRowY[qBound(1, row, 6)];
        return QRectF(x - cardW / 2, y - cardH / 2, cardW, cardH);
    };

    QFont avgFont = font();
    avgFont.setBold(true);
    avgFont.setPointSizeF(font().pointSizeF() * 1.0);
    QFont rangeFont = font();
    rangeFont.setPointSizeF(font().pointSizeF() * 0.72);

    const double median = shownMedian();
    const auto drawBox = [&](const QRectF &box, const QString &slot) {
        const SlotStrength stats = m_strengths.value(slot);
        QPainterPath path;
        path.addRoundedRect(box, 5, 5);
        if (stats.avg > 0.0) {
            const CellStyle style = cardStyle(stats.avg, slot.startsWith(QLatin1String("GK")), median, night);
            painter.fillPath(path, style.background);
            painter.setPen(QPen(QColor(0, 0, 0, 60), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
            painter.setPen(style.text);
            painter.setFont(avgFont);
            painter.drawText(box.adjusted(0, 1, 0, -box.height() * 0.42), Qt::AlignCenter,
                             formatDwrsPercent(stats.avg));
            painter.setFont(rangeFont);
            painter.drawText(box.adjusted(0, box.height() * 0.55, 0, -1), Qt::AlignCenter,
                             QStringLiteral("%1–%2").arg(formatDwrs(stats.min), formatDwrs(stats.max)));
        } else {
            painter.fillPath(path, emptyCard);
            painter.setPen(night ? QColor(Qt::white) : cardText);
            painter.setFont(rangeFont);
            painter.drawText(box, Qt::AlignCenter, QStringLiteral("–"));
        }
    };

    const PitchGeometry &geometry = pitchGeometry(m_fmVersionId);
    for (auto it = m_layout.constBegin(); it != m_layout.constEnd(); ++it) {
        const int row = geometry.drawingRow(it.key());
        if (row < 0)
            continue;
        QSet<int> occupied;
        for (const QString &slot : it.value()) {
            if (const PitchSlot *pitchSlot = geometry.slot(slot))
                occupied.insert(pitchSlot->displayColumn);
        }
        for (const QString &slot : it.value()) {
            if (slot == geometry.goalkeeper.key)
                continue;
            const PitchSlot *pitchSlot = geometry.slot(slot);
            if (!pitchSlot || pitchSlot->displayColumn < 0 || pitchSlot->displayColumn > 4)
                continue;
            drawBox(cardRectAt(row, xFraction(pitchSlot->displayColumn, occupied)), slot);
        }
    }
    drawBox(cardRectAt(6, 0.5), QStringLiteral("GK"));
}

void StrengthGridWidget::paintMarkers(QPainter &painter, const QRectF &field, bool night)
{
    const double median = shownMedian();
    const QColor emptyCard = night ? QColor(0x12, 0x16, 0x1c) : QColor(0xfb, 0xfb, 0xfd);
    const QColor cardText = QColor(0x1e, 0x22, 0x2b);
    const double cardW = 0.20;
    const double cardH = 0.125;

    QList<QPointF> anchors;
    for (const Marker &marker : std::as_const(m_markers))
        anchors.append(marker.anchor);
    const QList<QPointF> placed = spreadMarkers(anchors, QSizeF(cardW, cardH));
    const auto toField = [&](const QPointF &f) {
        return QPointF(field.left() + field.width() * f.x(), field.top() + field.height() * f.y());
    };
    for (qsizetype i = 0; i < m_markers.size(); ++i) {
        const QPointF from = toField(m_markers.at(i).anchor);
        const QPointF to = toField(placed.at(i));
        if (QLineF(from, to).length() < 3.0)
            continue;
        painter.setPen(QPen(QColor(255, 255, 255, 170), 1.0));
        painter.drawLine(from, to);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 210));
        painter.drawEllipse(from, 2.2, 2.2);
    }

    QFont headerFont = font();
    headerFont.setPointSizeF(font().pointSizeF() * 0.66);
    QFont valueFont = font();
    valueFont.setBold(true);
    QFont impactFont = font();
    impactFont.setPointSizeF(font().pointSizeF() * 0.68);

    m_markerRects.clear();
    for (qsizetype i = 0; i < m_markers.size(); ++i) {
        const Marker &marker = m_markers.at(i);
        const QPointF centre = toField(placed.at(i));
        const QRectF box(centre.x() - field.width() * cardW / 2,
                         centre.y() - field.height() * cardH / 2, field.width() * cardW,
                         field.height() * cardH);
        m_markerRects.append(box);
        QPainterPath path;
        path.addRoundedRect(box, 5, 5);
        const bool filled = !marker.uid.isEmpty();
        const CellStyle style = cardStyle(marker.value, marker.goalkeeper, median, night);
        // Role fit colours the card; the XI impact is a separate line of
        // plain text on a light band, so the two never read as one number.
        painter.fillPath(path, filled ? style.background : emptyCard);
        painter.setPen(QPen(QColor(0, 0, 0, 60), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        const double h = box.height();
        const QRectF headerLine(box.left() + 2, box.top() + 1, box.width() - 4, h * 0.27);
        const QRectF valueLine(box.left(), box.top() + h * 0.24, box.width(), h * 0.40);
        const QRectF impactBand(box.left() + 2, box.top() + h * 0.66, box.width() - 4,
                                h * 0.31);
        const QColor ink = filled ? style.text : (night ? QColor(Qt::white) : cardText);
        painter.setPen(ink);
        painter.setFont(headerFont);
        painter.drawText(headerLine, Qt::AlignCenter,
                         painter.fontMetrics().elidedText(marker.header, Qt::ElideRight,
                                                          static_cast<int>(headerLine.width())));
        painter.setFont(valueFont);
        painter.drawText(valueLine, Qt::AlignCenter,
                         filled ? formatDwrsPercent(marker.value)
                                : QStringLiteral("–"));
        if (!marker.impact.isEmpty()) {
            QPainterPath band;
            band.addRoundedRect(impactBand, 3, 3);
            // An opaque light band with dark text reads on every card colour.
            if (filled)
                painter.fillPath(band, QColor(255, 255, 255, 225));
            painter.setPen(filled ? cardText : ink);
            painter.setFont(impactFont);
            painter.drawText(impactBand, Qt::AlignCenter,
                             painter.fontMetrics().elidedText(
                                 marker.impact, Qt::ElideRight,
                                 static_cast<int>(impactBand.width())));
        }
    }
}

int StrengthGridWidget::markerAt(const QPoint &pos) const
{
    if (!m_markerMode)
        return -1;
    for (qsizetype i = m_markerRects.size() - 1; i >= 0; --i) {
        if (m_markerRects.at(i).contains(pos))
            return static_cast<int>(i);
    }
    return -1;
}

bool StrengthGridWidget::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const int i = markerAt(help->pos());
        if (i >= 0 && i < m_markers.size() && !m_markers.at(i).tooltip.isEmpty()) {
            const Marker &marker = m_markers.at(i);
            QString text = marker.tooltip;
            if (m_colorMode == ColorMode::SquadComparison && !marker.uid.isEmpty()) {
                const double median = shownMedian();
                text = tr("%1 zur Kadermitte (%2)")
                           .arg(formatDwrsDelta(marker.value - median), formatDwrs(median))
                       + QLatin1Char('\n') + text;
            }
            QToolTip::showText(help->globalPos(), text, this);
        }
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void StrengthGridWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int i = markerAt(event->pos());
    if (i >= 0 && !m_markers.at(i).uid.isEmpty()) {
        emit playerDoubleClicked(m_markers.at(i).uid);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void StrengthGridWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int i = markerAt(event->pos());
    setCursor(i >= 0 && !m_markers.at(i).uid.isEmpty() ? Qt::PointingHandCursor
                                                        : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void StrengthGridWidget::contextMenuEvent(QContextMenuEvent *event)
{
    const int i = markerAt(event->pos());
    if (i >= 0 && !m_markers.at(i).uid.isEmpty()) {
        emit playerContextMenuRequested(m_markers.at(i).uid, event->globalPos());
        event->accept();
        return;
    }
    QWidget::contextMenuEvent(event);
}

} // namespace fm
