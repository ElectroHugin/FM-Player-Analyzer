#include "ClubHeaderBar.h"

#include "../theming/ThemeManager.h"
#include "core/ThemeColors.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace fm {

namespace {

constexpr int kCrestHeight = 38;
constexpr qreal kSlant = 22.0;  // horizontal run of the slanted block edge
constexpr qreal kStripe = 6.0;  // width of the accent stripe

// The colored block: from x = 0 to `right` at the top, slanting back by
// kSlant towards the bottom.
QPainterPath blockPath(const QRectF &bar, qreal right)
{
    QPainterPath path;
    path.moveTo(bar.left(), bar.top());
    path.lineTo(right, bar.top());
    path.lineTo(right - kSlant, bar.bottom());
    path.lineTo(bar.left(), bar.bottom());
    path.closeSubpath();
    return path;
}

QPainterPath stripePath(const QRectF &bar, qreal right)
{
    QPainterPath path;
    path.moveTo(right + 3.0, bar.top());
    path.lineTo(right + 3.0 + kStripe, bar.top());
    path.lineTo(right + 3.0 + kStripe - kSlant, bar.bottom());
    path.lineTo(right + 3.0 - kSlant, bar.bottom());
    path.closeSubpath();
    return path;
}

// Monochrome silhouette of a crest (its alpha channel in `color`).
QImage silhouette(const QPixmap &crest, const QColor &color, int height)
{
    QImage image = crest.toImage()
                       .scaledToHeight(height, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), color);
    return image;
}

void paintBlock(QPainter &painter, const QRectF &bar, qreal right, const ThemeManager &theme,
                const QPixmap &crest)
{
    const ClubHeaderPalette palette = theme.headerPalette();
    painter.setRenderHint(QPainter::Antialiasing);
    const QPainterPath block = blockPath(bar, right);

    QLinearGradient gradient(bar.left(), bar.top(), right, bar.bottom());
    gradient.setColorAt(0.0, palette.start);
    gradient.setColorAt(1.0, palette.end);
    painter.fillPath(block, gradient);

    // A faint, oversized crest silhouette, cut by the block: identity without
    // competing with the crest itself or the name.
    if (theme.crestWatermark() && !crest.isNull()) {
        const int height = qRound(bar.height() * 2.3);
        QColor tint = palette.text;
        tint.setAlphaF(0.10);
        const QImage mark = silhouette(crest, tint, height);
        painter.save();
        painter.setClipPath(block);
        const qreal x = right - kSlant - mark.width() * 0.78;
        const qreal y = bar.center().y() - height * 0.42;
        painter.drawImage(QPointF(x, y), mark);
        painter.restore();
    }

    painter.fillPath(stripePath(bar, right), palette.stripe);
}

} // namespace

ClubHeaderBar::ClubHeaderBar(ThemeManager &theme, QWidget *parent)
    : QFrame(parent)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("headerBar"));
    setFixedHeight(62);

    m_identity = new QWidget(this);
    m_identity->setObjectName(QStringLiteral("headerIdentity"));
    auto *row = new QHBoxLayout(m_identity);
    row->setContentsMargins(20, 0, 8, 0);
    row->setSpacing(12);
    m_crest = new QLabel(m_identity);
    m_crest->setFixedHeight(kCrestHeight);
    m_crest->setAlignment(Qt::AlignCenter);
    // A soft shadow keeps a crest in the club's own colors apart from the block.
    auto *shadow = new QGraphicsDropShadowEffect(m_crest);
    shadow->setBlurRadius(10);
    shadow->setOffset(0, 1);
    shadow->setColor(QColor(0, 0, 0, 90));
    m_crest->setGraphicsEffect(shadow);
    row->addWidget(m_crest);
    auto *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(0);
    m_title = new QLabel(m_identity);
    m_title->setObjectName(QStringLiteral("headerTitle"));
    m_subtitle = new QLabel(m_identity);
    m_subtitle->setObjectName(QStringLiteral("headerSubtitle"));
    text->addStretch(1);
    text->addWidget(m_title);
    text->addWidget(m_subtitle);
    text->addStretch(1);
    row->addLayout(text);

    connect(&m_theme, &ThemeManager::themeChanged, this, [this] {
        applyColors();
        update();
    });
    applyColors();
}

QString ClubHeaderBar::initials(const QString &title)
{
    QString result;
    for (const QString &word : title.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (word.at(0).isLetter())
            result += word.at(0).toUpper();
        if (result.size() == 3)
            break;
    }
    return result.isEmpty() ? QStringLiteral("FM") : result;
}

void ClubHeaderBar::setIdentity(const QPixmap &crest, const QString &title,
                                const QString &subtitle)
{
    m_crestPixmap = crest;
    m_titleText = title;
    m_title->setText(title);
    m_subtitle->setText(subtitle);
    m_subtitle->setVisible(!subtitle.isEmpty());
    applyColors();
    update();
}

void ClubHeaderBar::applyColors()
{
    const bool gradient = m_theme.headerStyle() != QLatin1String("solid");
    QColor text;
    QColor muted;
    if (gradient) {
        const ClubHeaderPalette palette = m_theme.headerPalette();
        text = palette.text;
        muted = palette.mutedText;
    } else {
        text = readableTextOn(m_theme.header());
        muted = text;
        muted.setAlphaF(0.75);
    }
    // Own style sheets win over the app-wide one (QFrame#headerBar QLabel).
    m_title->setStyleSheet(QStringLiteral("color:%1; font-size:13.5pt; font-weight:700;")
                               .arg(text.name()));
    m_subtitle->setStyleSheet(
        QStringLiteral("color:%1; font-size:8.5pt; font-weight:500;")
            .arg(muted.name(QColor::HexArgb)));

    const qreal dpr = devicePixelRatioF();
    if (!m_crestPixmap.isNull()) {
        QPixmap scaled = m_crestPixmap.scaledToHeight(qRound(kCrestHeight * dpr),
                                                      Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(dpr);
        m_crest->setPixmap(scaled);
        m_crest->setFixedWidth(qCeil(scaled.width() / dpr));
        return;
    }
    // No crest: a monogram badge in the header's text color.
    const int size = kCrestHeight;
    QPixmap badge(QSize(size, size) * dpr);
    badge.setDevicePixelRatio(dpr);
    badge.fill(Qt::transparent);
    QPainter painter(&badge);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor ring = text;
    ring.setAlphaF(0.55);
    QColor fill = text;
    fill.setAlphaF(0.12);
    painter.setPen(QPen(ring, 1.5));
    painter.setBrush(fill);
    painter.drawEllipse(QRectF(1, 1, size - 2, size - 2));
    QFont font = this->font();
    font.setBold(true);
    font.setPixelSize(m_titleText.isEmpty() ? 12 : 12);
    painter.setFont(font);
    painter.setPen(text);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, initials(m_titleText));
    painter.end();
    m_crest->setPixmap(badge);
    m_crest->setFixedWidth(size);
}

void ClubHeaderBar::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event); // background + bottom border from the style sheet
    if (m_theme.headerStyle() == QLatin1String("solid"))
        return;
    QPainter painter(this);
    const QRectF bar(0, 0, width(), height() - 1); // keep the bottom border visible
    const qreal right = m_identity->geometry().right() + kBlockTail - kStripe - 6.0;
    paintBlock(painter, bar, right, m_theme, m_crestPixmap);
}

QPixmap ClubHeaderBar::renderPreview(const ThemeManager &theme, const QSize &size,
                                     const QPixmap &crest, const QString &title,
                                     const QString &subtitle, qreal dpr)
{
    QPixmap pixmap(size * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(theme.headerStyle() == QLatin1String("solid") ? theme.header() : theme.sidebar());
    QPainter painter(&pixmap);
    const QRectF bar(0, 0, size.width(), size.height());
    const ClubHeaderPalette palette = theme.headerPalette();
    const bool solid = theme.headerStyle() == QLatin1String("solid");
    const QColor text = solid ? readableTextOn(theme.header()) : palette.text;

    QFont titleFont;
    titleFont.setBold(true);
    titleFont.setPointSizeF(12.5);
    const QFontMetricsF metrics(titleFont);
    const qreal crestWidth = crest.isNull() ? kCrestHeight
                                            : crest.width() * qreal(kCrestHeight) / crest.height();
    const qreal textLeft = 18 + crestWidth + 12;
    const qreal right = textLeft + metrics.horizontalAdvance(title) + 8 + kBlockTail - kStripe - 6;
    if (!solid)
        paintBlock(painter, bar, right, theme, crest);

    const qreal crestTop = (size.height() - kCrestHeight) / 2.0;
    if (!crest.isNull()) {
        painter.drawPixmap(QRectF(18, crestTop, crestWidth, kCrestHeight), crest,
                           QRectF(crest.rect()));
    }
    painter.setFont(titleFont);
    painter.setPen(text);
    const qreal titleY = subtitle.isEmpty() ? size.height() / 2.0 + metrics.ascent() / 2.5
                                            : size.height() / 2.0 - 1;
    painter.drawText(QPointF(textLeft, titleY), title);
    if (!subtitle.isEmpty()) {
        QFont small;
        small.setPointSizeF(8.0);
        painter.setFont(small);
        painter.setPen(solid ? text : palette.mutedText);
        painter.drawText(QPointF(textLeft, titleY + 15), subtitle);
    }
    return pixmap;
}

} // namespace fm
