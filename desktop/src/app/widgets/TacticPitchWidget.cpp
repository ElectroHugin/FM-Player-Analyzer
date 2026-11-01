#include "TacticPitchWidget.h"

#include "../theming/ThemeManager.h"
#include "core/Constants.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSet>

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

void TacticPitchWidget::setEditorSlots(const QHash<QString, QString> &roles,
                                       const QHash<QString, QString> &roleNames,
                                       bool showFreeSlots)
{
    m_editor = true;
    m_editorRoles = roles;
    m_roleNames = roleNames;
    m_showFreeSlots = showFreeSlots;
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

    if (!m_editor && m_layout.isEmpty() && !m_positions.contains(QStringLiteral("GK"))) {
        painter.setPen(textColor);
        painter.drawText(pitch, Qt::AlignCenter, tr("Keine Daten"));
        return;
    }

    // --- Card placement: 5 columns, 6 lines (strikers at the top, GK last) ---
    const double cardW = field.width() * 0.188;
    const double cardH = field.height() * 0.125;
    static const double kRowY[] = {0.0, 0.085, 0.255, 0.42, 0.585, 0.75, 0.915};
    // Dynamic line: two inside players without one in the middle (DCL+DCR,
    // DMCL+DMCR, STL+STR) close up towards the centre instead of leaving a
    // hole; wide players stay on the touchline.
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
    const auto cardRect = [&](int row, int col) { return cardRectAt(row, 0.1 + 0.2 * (col - 1)); };
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
    const PitchGeometry &geometry = pitchGeometry(m_fmVersionId);

    if (m_editor) {
        // Tactic editor: the slots in use as role cards, the free ones as
        // dashed "+" boxes — a grid that shows only what can still be added.
        const auto stripPhase = [](QString role) {
            if (role.endsWith(QLatin1String("-IP")))
                role.chop(3);
            else if (role.endsWith(QLatin1String("-OOP")))
                role.chop(4);
            return role;
        };
        // squeezed: free middle place between two cards that closed up.
        const auto drawSlot = [&](const QRectF &box, const QString &slot, const QString &label,
                                  int row, bool squeezed = false) {
            const QString role = m_editorRoles.value(slot);
            if (role.isEmpty() && !m_showFreeSlots)
                return;
            m_boxRects.insert(slot, box);
            QPainterPath path;
            path.addRoundedRect(box, 6, 6);
            if (role.isEmpty()) {
                // Free place: a small "+" marker, so it fits between the
                // cards of a line that closed up.
                const double d = box.height() * (squeezed ? 0.3 : 0.42);
                const double cy = squeezed ? box.bottom() - d * 0.2 : box.center().y() - d * 0.25;
                const QRectF circle(box.center().x() - d / 2, cy - d / 2, d, d);
                m_boxRects.insert(slot, circle.adjusted(-4, -4, 4, d * 0.6));
                painter.setBrush(QColor(255, 255, 255, night ? 22 : 36));
                painter.setPen(QPen(QColor(255, 255, 255, 150), 1.2, Qt::DashLine));
                painter.drawEllipse(circle);
                QFont plus = font();
                plus.setBold(true);
                plus.setPointSizeF(font().pointSizeF() * 1.25);
                painter.setFont(plus);
                painter.setPen(QColor(255, 255, 255, 220));
                painter.drawText(circle, Qt::AlignCenter, QStringLiteral("+"));
                painter.setFont(smallFont);
                painter.setPen(QColor(255, 255, 255, 170));
                painter.drawText(QRectF(box.left() - 10, circle.bottom() + 1, box.width() + 20,
                                        d * 0.55),
                                 Qt::AlignHCenter | Qt::AlignTop, label);
                return;
            }
            painter.fillPath(path, cardBody);
            const double headerH = box.height() * 0.32;
            const QRectF header(box.left(), box.top(), box.width(), headerH);
            painter.save();
            painter.setClipPath(path, Qt::IntersectClip);
            painter.fillRect(header, kLineColors[qBound(0, row, 6)]);
            painter.restore();
            painter.setPen(Qt::white);
            painter.setFont(headerFont);
            painter.drawText(header, Qt::AlignCenter,
                             painter.fontMetrics().elidedText(
                                 stripPhase(role), Qt::ElideRight,
                                 static_cast<int>(header.width() - 4)));
            const QRectF body = box.adjusted(4, headerH + 1, -4, -2);
            const QRectF nameLine(body.left(), body.top(), body.width(), body.height() * 0.6);
            // Role names are not person names: wrap onto two lines, shrink
            // a little if needed, never abbreviate words.
            painter.setPen(textColor);
            QFont roleFont = nameFont;
            const QString roleName = m_roleNames.value(role, role);
            const int flags = Qt::AlignCenter | Qt::TextWordWrap;
            for (int step = 0; step < 4; ++step) {
                painter.setFont(roleFont);
                const QRectF needed = painter.boundingRect(nameLine, flags, roleName);
                if (needed.height() <= nameLine.height() && needed.width() <= nameLine.width())
                    break;
                roleFont.setPointSizeF(roleFont.pointSizeF() * 0.9);
            }
            painter.setFont(roleFont);
            painter.drawText(nameLine, flags, roleName);
            painter.setPen(secondaryText);
            painter.setFont(smallFont);
            painter.drawText(QRectF(body.left(), nameLine.bottom(), body.width(),
                                    body.height() * 0.4),
                             Qt::AlignCenter, label);
            painter.setPen(QPen(cardBorder, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        };
        for (const PitchRow &pitchRow : geometry.rows) {
            const int row = geometry.drawingRow(pitchRow.stratum);
            if (row < 0)
                continue;
            QSet<int> occupied;
            for (const PitchSlot &slot : pitchRow.pitchSlots) {
                if (slot.inEditor && !m_editorRoles.value(slot.key).isEmpty())
                    occupied.insert(slot.displayColumn);
            }
            for (const PitchSlot &slot : pitchRow.pitchSlots) {
                if (!slot.inEditor || slot.displayColumn < 0 || slot.displayColumn > 4)
                    continue;
                const bool squeezed = slot.displayColumn == 2 && occupied.contains(1)
                                      && occupied.contains(3) && !occupied.contains(2);
                drawSlot(cardRectAt(row, xFraction(slot.displayColumn, occupied)), slot.key,
                         slot.key, row, squeezed);
            }
        }
        drawSlot(cardRect(6, 3), QStringLiteral("GK"), QStringLiteral("GK"), 6);
        return;
    }

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
            if (!pitchSlot)
                continue;
            if (pitchSlot->displayColumn < 0 || pitchSlot->displayColumn > 4)
                continue;
            drawCard(cardRectAt(row, xFraction(pitchSlot->displayColumn, occupied)), slot, row);
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

QString TacticPitchWidget::slotAt(const QPoint &pos) const
{
    for (auto it = m_boxRects.constBegin(); it != m_boxRects.constEnd(); ++it) {
        if (it.value().contains(pos))
            return it.key();
    }
    return QString();
}

void TacticPitchWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_editor && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
        const QString slot = slotAt(event->pos());
        if (!slot.isEmpty()) {
            emit slotClicked(slot, event->globalPosition().toPoint());
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
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
    const bool clickable = m_editor ? !slotAt(event->pos()).isEmpty()
                                    : !uidAt(event->pos()).isEmpty();
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
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
