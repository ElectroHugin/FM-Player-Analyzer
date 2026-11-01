#include "TacticPitchWidget.h"

#include "../theming/ThemeManager.h"
#include "core/Constants.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace fm {

namespace {

// "Kylian Mbappé Lottin" -> "K. Lottin" (football-style short form).
QString initialAndLastName(const QString &fullName)
{
    const QStringList words = fullName.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() < 2)
        return fullName;
    return QStringLiteral("%1. %2").arg(words.first().at(0)).arg(words.last());
}

// Draws text centered in rect, degrading gracefully instead of cutting the
// name off: full name -> "K. Mbappé" -> smaller font -> elide as last resort.
void drawFittedName(QPainter &painter, const QRectF &rect, const QString &fullName,
                    const QFont &baseFont)
{
    const int width = static_cast<int>(rect.width());
    QFont font = baseFont;
    painter.setFont(font);
    QString text = fullName;
    if (painter.fontMetrics().horizontalAdvance(text) > width) {
        text = initialAndLastName(fullName);
        while (painter.fontMetrics().horizontalAdvance(text) > width
               && font.pointSizeF() > baseFont.pointSizeF() * 0.72) {
            font.setPointSizeF(font.pointSizeF() - 0.5);
            painter.setFont(font);
        }
        if (painter.fontMetrics().horizontalAdvance(text) > width)
            text = painter.fontMetrics().elidedText(text, Qt::ElideRight, width);
    }
    painter.drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter, text);
}

} // namespace

TacticPitchWidget::TacticPitchWidget(ThemeManager &theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(theme)
{
    setMinimumSize(340, 340 * 105 / 68);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setMouseTracking(true); // pointing-hand cursor over player boxes
    connect(&m_theme, &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void TacticPitchWidget::setTeam(const QHash<QString, XiCell> &team,
                                const QHash<QString, QString> &positions,
                                const QHash<QString, QStringList> &layout,
                                const QHash<QString, QString> &roleNames, bool showApt)
{
    m_team = team;
    m_positions = positions;
    m_layout = layout;
    m_roleNames = roleNames;
    m_showApt = showApt;
    update();
}

void TacticPitchWidget::setSlotColors(const QHash<QString, QColor> &colors)
{
    m_slotColors = colors;
    update();
}

void TacticPitchWidget::setSlotSubtexts(const QHash<QString, QString> &subtexts)
{
    m_slotSubtexts = subtexts;
    update();
}

void TacticPitchWidget::clearData()
{
    m_team.clear();
    m_positions.clear();
    m_layout.clear();
    m_slotColors.clear();
    m_slotSubtexts.clear();
    update();
}

void TacticPitchWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const bool night = m_theme.mode() != QLatin1String("day");
    // Pitch in the style of FM's tactics screen: dark green with mown stripes,
    // compact player cards at their place on the pitch (not a full grid).
    const QColor pitchBg = night ? QColor(0x0f, 0x4a, 0x3c) : QColor(0x2e, 0x7d, 0x5b);
    const QColor stripe = night ? QColor(255, 255, 255, 10) : QColor(255, 255, 255, 18);
    const QColor markings = QColor(255, 255, 255, night ? 90 : 120);
    const QColor cardBody = night ? QColor(0x12, 0x16, 0x1c) : QColor(0xfb, 0xfb, 0xfd);
    const QColor cardBorder = night ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 40);
    const QColor textColor = night ? QColor(Qt::white) : QColor(0x1e, 0x22, 0x2b);
    const QColor secondaryText = night ? QColor(0xb8, 0xbe, 0xc8) : QColor(0x5a, 0x60, 0x6c);

    const QRectF pitch = rect().adjusted(1, 1, -1, -1);

    // --- Pitch + markings ---
    QPainterPath pitchPath;
    pitchPath.addRoundedRect(pitch, 12, 12);
    painter.fillPath(pitchPath, pitchBg);
    painter.setClipPath(pitchPath);
    const int stripes = 10;
    for (int i = 0; i < stripes; i += 2)
        painter.fillRect(QRectF(pitch.left(), pitch.top() + i * pitch.height() / stripes,
                                pitch.width(), pitch.height() / stripes),
                         stripe);
    const QRectF field = pitch.adjusted(8, 8, -8, -8);
    painter.setPen(QPen(markings, 1.5));
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

    if (m_layout.isEmpty() && !m_positions.contains(QStringLiteral("GK"))) {
        painter.setPen(textColor);
        painter.drawText(pitch, Qt::AlignCenter, tr("Keine Daten"));
        return;
    }

    // --- Card placement: 5 columns, 6 lines (strikers at the top, GK last) ---
    const double cardW = field.width() * 0.188;
    const double cardH = field.height() * 0.125;
    static const double kRowY[] = {0.0, 0.085, 0.255, 0.42, 0.585, 0.75, 0.915};
    const auto cardRect = [&](int row, int col) {
        const double x = field.left() + field.width() * (0.1 + 0.2 * (col - 1));
        const double y = field.top() + field.height() * kRowY[qBound(1, row, 6)];
        return QRectF(x - cardW / 2, y - cardH / 2, cardW, cardH);
    };
    // Header colour per line, like the role colours in FM's tactics screen.
    static const QColor kLineColors[] = {
        QColor(0x4a, 0x4f, 0x63), QColor(0x9b, 0x23, 0x6f), QColor(0x5e, 0x2e, 0x8f),
        QColor(0x27, 0x55, 0xa6), QColor(0x1e, 0x7a, 0x4a), QColor(0x0f, 0x72, 0x72),
        QColor(0x4a, 0x4f, 0x63)};

    QFont headerFont = font();
    headerFont.setBold(true);
    headerFont.setPointSizeF(font().pointSizeF() * 0.85);
    QFont nameFont = font();
    nameFont.setBold(true);
    nameFont.setPointSizeF(font().pointSizeF() * 0.92);
    QFont ratingFont = font();
    ratingFont.setBold(true);
    ratingFont.setPointSizeF(font().pointSizeF() * 1.05);
    QFont smallFont = font();
    smallFont.setPointSizeF(font().pointSizeF() * 0.78);

    m_boxRects.clear();
    const auto drawCard = [&](const QRectF &box, const QString &slot, int row) {
        m_boxRects.insert(slot, box);
        const XiCell cell = m_team.value(slot);
        QPainterPath path;
        path.addRoundedRect(box, 6, 6);
        painter.fillPath(path, m_slotColors.value(slot, cardBody));

        // FM27 ids carry the phase when an abbreviation exists in both
        // ("CFD-OOP"); the pitch belongs to one phase, so show the game's
        // abbreviation.
        QString roleAbbr = m_positions.value(slot);
        if (roleAbbr.endsWith(QLatin1String("-IP")))
            roleAbbr.chop(3);
        else if (roleAbbr.endsWith(QLatin1String("-OOP")))
            roleAbbr.chop(4);
        const QString apt = aptAbbreviations().value(cell.apt, cell.apt);
        const QString subtext = m_slotSubtexts.value(slot);

        // Header: role abbreviation on the line colour.
        const double headerH = box.height() * 0.28;
        const QRectF header(box.left(), box.top(), box.width(), headerH);
        painter.save();
        painter.setClipPath(path, Qt::IntersectClip);
        painter.fillRect(header, kLineColors[qBound(0, row, 6)]);
        painter.restore();
        painter.setPen(Qt::white);
        painter.setFont(headerFont);
        painter.drawText(header, Qt::AlignCenter,
                         painter.fontMetrics().elidedText(roleAbbr, Qt::ElideRight,
                                                          static_cast<int>(header.width() - 4)));

        // Rating with a slim bar, then the name, then the extra line.
        const QRectF body = box.adjusted(4, headerH + 1, -4, -2);
        const double lineH = body.height() / 3.0;
        QRectF line(body.left(), body.top(), body.width(), lineH);
        const double rating = cell.isFilled() ? cell.rating : 0.0;
        painter.setFont(ratingFont);
        painter.setPen(cell.isFilled() ? textColor : secondaryText);
        painter.drawText(line.adjusted(0, 0, -body.width() * 0.45, 0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         cell.isFilled() ? QStringLiteral("%1%").arg(qRound(rating))
                                         : QStringLiteral("–"));
        const QRectF bar(line.left() + body.width() * 0.52, line.center().y() - 2,
                         body.width() * 0.48, 4);
        painter.fillRect(bar, night ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 25));
        const QColor barColor = rating >= 75   ? QColor(0x3c, 0xc4, 0x6a)
                                : rating >= 65 ? QColor(0xc9, 0xb8, 0x2c)
                                               : QColor(0xd9, 0x6b, 0x2b);
        painter.fillRect(QRectF(bar.left(), bar.top(), bar.width() * qBound(0.0, rating / 100.0, 1.0),
                                bar.height()),
                         barColor);
        line.translate(0, lineH);
        painter.setPen(textColor);
        drawFittedName(painter, line, cell.isFilled() ? cell.name : tr("unbesetzt"), nameFont);
        const QString lastLine = !subtext.isEmpty() ? subtext : (m_showApt ? apt : QString());
        if (!lastLine.isEmpty()) {
            line.translate(0, lineH);
            painter.setPen(secondaryText);
            painter.setFont(smallFont);
            painter.drawText(line, Qt::AlignHCenter | Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(
                                 lastLine, Qt::ElideRight, static_cast<int>(line.width())));
        }
        painter.setPen(QPen(cardBorder, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    };

    const PitchGeometry &geometry = pitchGeometry(m_fmVersionId);
    for (auto it = m_layout.constBegin(); it != m_layout.constEnd(); ++it) {
        const int row = geometry.drawingRow(it.key());
        if (row < 0)
            continue;
        for (const QString &slot : it.value()) {
            if (slot == geometry.goalkeeper.key)
                continue;
            const PitchSlot *pitchSlot = geometry.slot(slot);
            if (!pitchSlot)
                continue;
            const int col = pitchSlot->displayColumn + 1;
            if (col < 1 || col > 5)
                continue;
            drawCard(cardRect(row, col), slot, row);
        }
    }

    if (m_positions.contains(QStringLiteral("GK")))
        drawCard(cardRect(6, 3), QStringLiteral("GK"), 6);
}

QString TacticPitchWidget::uidAt(const QPoint &pos) const
{
    for (auto it = m_boxRects.constBegin(); it != m_boxRects.constEnd(); ++it) {
        if (it.value().contains(pos))
            return m_team.value(it.key()).playerUid;
    }
    return QString();
}

void TacticPitchWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QString uid = uidAt(event->pos());
    if (!uid.isEmpty()) {
        emit playerDoubleClicked(uid);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TacticPitchWidget::mouseMoveEvent(QMouseEvent *event)
{
    setCursor(uidAt(event->pos()).isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    QWidget::mouseMoveEvent(event);
}

void TacticPitchWidget::contextMenuEvent(QContextMenuEvent *event)
{
    const QString uid = uidAt(event->pos());
    if (!uid.isEmpty()) {
        emit playerContextMenuRequested(uid, event->globalPos());
        event->accept();
        return;
    }
    QWidget::contextMenuEvent(event);
}

} // namespace fm
