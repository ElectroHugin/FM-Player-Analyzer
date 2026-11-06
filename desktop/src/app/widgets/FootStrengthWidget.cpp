#include "FootStrengthWidget.h"

#include "../../core/Utils.h"

#include <QEvent>
#include <QFontMetrics>
#include <QPainter>

namespace fm {

namespace {
constexpr int kColumnWidth = 190;
constexpr int kSegmentWidth = 18;
constexpr int kSegmentGap = 5;
constexpr int kSegmentHeight = 4;
} // namespace

FootStrengthWidget::FootStrengthWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    setVisible(false);
}

QString FootStrengthWidget::levelName(int level)
{
    switch (level) {
    case 1: return tr("Sehr schwach");
    case 2: return tr("Schwach");
    case 3: return tr("Ordentlich");
    case 4: return tr("Ziemlich stark");
    case 5: return tr("Stark");
    case 6: return tr("Sehr stark");
    default: return {};
    }
}

void FootStrengthWidget::setFeet(const QString &leftFoot, const QString &rightFoot)
{
    m_leftRaw = leftFoot.trimmed();
    m_rightRaw = rightFoot.trimmed();
    rebuild();
}

void FootStrengthWidget::rebuild()
{
    m_feet.clear();
    const auto add = [this](const QString &caption, const QString &raw) {
        if (raw.isEmpty())
            return;
        Foot foot;
        foot.caption = caption;
        foot.level = footStrengthLevel(raw);
        foot.valueText = foot.level > 0 ? levelName(foot.level) : raw;
        m_feet.append(foot);
    };
    add(tr("Linker Fuß"), m_leftRaw);
    add(tr("Rechter Fuß"), m_rightRaw);
    setVisible(!m_feet.isEmpty());
    updateGeometry();
    update();
}

QSize FootStrengthWidget::sizeHint() const
{
    QFont captionFont = font();
    captionFont.setPointSizeF(font().pointSizeF() * 0.9);
    QFont valueFont = font();
    valueFont.setPointSizeF(font().pointSizeF() * 1.25);
    const int height = QFontMetrics(captionFont).height() + 2 + QFontMetrics(valueFont).height()
                       + 6 + kSegmentHeight + 2;
    return {kColumnWidth * int(qMax<qsizetype>(1, m_feet.size())), height};
}

void FootStrengthWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        rebuild();
    else if (event->type() == QEvent::FontChange || event->type() == QEvent::PaletteChange)
        updateGeometry();
}

void FootStrengthWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QFont captionFont = font();
    captionFont.setPointSizeF(font().pointSizeF() * 0.9);
    QFont valueFont = font();
    valueFont.setPointSizeF(font().pointSizeF() * 1.25);
    const QFontMetrics captionMetrics(captionFont);
    const QFontMetrics valueMetrics(valueFont);

    const QColor textColor = palette().color(QPalette::WindowText);
    QColor captionColor = textColor;
    captionColor.setAlphaF(0.65);
    QColor emptySegment = textColor;
    emptySegment.setAlphaF(0.18);

    int x = 0;
    for (const Foot &foot : m_feet) {
        int y = 0;
        painter.setFont(captionFont);
        painter.setPen(captionColor);
        painter.drawText(QRect(x, y, kColumnWidth - 8, captionMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, foot.caption);
        y += captionMetrics.height() + 2;

        painter.setFont(valueFont);
        painter.setPen(textColor);
        painter.drawText(QRect(x, y, kColumnWidth - 8, valueMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         valueMetrics.elidedText(foot.valueText, Qt::ElideRight, kColumnWidth - 8));
        y += valueMetrics.height() + 6;

        const QColor filled = attributeCellStyle(footStrengthAttributeValue(foot.level)).background;
        painter.setPen(Qt::NoPen);
        for (int i = 0; i < kFootStrengthLevels; ++i) {
            const QRectF segment(x + i * (kSegmentWidth + kSegmentGap), y, kSegmentWidth,
                                 kSegmentHeight);
            painter.setBrush(i < foot.level ? filled : emptySegment);
            painter.drawRoundedRect(segment, kSegmentHeight / 2.0, kSegmentHeight / 2.0);
        }
        x += kColumnWidth;
    }
}

} // namespace fm
