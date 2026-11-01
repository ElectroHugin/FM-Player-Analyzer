#include "StrengthGridWidget.h"

#include "../theming/ThemeManager.h"
#include "core/Constants.h"

#include <QPainter>
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
    connect(&m_theme, &ThemeManager::themeChanged, this,
            qOverload<>(&QWidget::update));
}

void StrengthGridWidget::setData(const QHash<QString, SlotStrength> &strengths,
                                 const QHash<QString, QStringList> &layout)
{
    m_strengths = strengths;
    m_layout = layout;
    update();
}

void StrengthGridWidget::clearData()
{
    m_strengths.clear();
    m_layout.clear();
    update();
}

QColor StrengthGridWidget::relativeColor(double value, double minAvg, double maxAvg) const
{
    // Legacy red -> yellow -> green gradient, relative to the squad's own
    // spread of positional averages.
    const double delta = maxAvg - minAvg;
    if (delta <= 0.0)
        return QColor(100, 166, 100);

    const double normalized = std::clamp((value - minAvg) / delta, 0.0, 1.0);
    int red, green, blue;
    if (normalized < 0.5) {
        red = 221;
        green = 43 + static_cast<int>((240 - 43) * (normalized * 2));
        blue = 43;
    } else {
        red = 240 - static_cast<int>((240 - 98) * ((normalized - 0.5) * 2));
        green = 240 - static_cast<int>((240 - 186) * ((normalized - 0.5) * 2));
        blue = 43 + static_cast<int>((98 - 43) * ((normalized - 0.5) * 2));
    }
    return QColor(red, green, blue);
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

    // Relative color range over the occupied positions.
    double minAvg = 0.0, maxAvg = 0.0;
    bool haveAvg = false;
    for (auto it = m_strengths.constBegin(); it != m_strengths.constEnd(); ++it) {
        if (it.value().avg > 0.0) {
            if (!haveAvg) {
                minAvg = maxAvg = it.value().avg;
                haveAvg = true;
            } else {
                minAvg = std::min(minAvg, it.value().avg);
                maxAvg = std::max(maxAvg, it.value().avg);
            }
        }
    }

    QFont avgFont = font();
    avgFont.setBold(true);
    avgFont.setPointSizeF(font().pointSizeF() * 1.0);
    QFont rangeFont = font();
    rangeFont.setPointSizeF(font().pointSizeF() * 0.72);

    const auto drawBox = [&](const QRectF &box, const QString &slot) {
        const SlotStrength stats = m_strengths.value(slot);
        QPainterPath path;
        path.addRoundedRect(box, 5, 5);
        if (stats.avg > 0.0) {
            painter.fillPath(path, relativeColor(stats.avg, minAvg, maxAvg));
            painter.setPen(QPen(QColor(0, 0, 0, 60), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
            painter.setPen(cardText);
            painter.setFont(avgFont);
            painter.drawText(box.adjusted(0, 1, 0, -box.height() * 0.42), Qt::AlignCenter,
                             QStringLiteral("%1%").arg(qRound(stats.avg)));
            painter.setFont(rangeFont);
            painter.drawText(box.adjusted(0, box.height() * 0.55, 0, -1), Qt::AlignCenter,
                             QStringLiteral("%1–%2").arg(qRound(stats.min)).arg(qRound(stats.max)));
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

} // namespace fm
