#include "AboutDialog.h"

#include "core/Version.h"
#include "theming/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSysInfo>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace fm {

namespace {

// The support address, XOR-scrambled so it never appears as a plain string in
// the executable (a strings/grep harvest finds nothing). Decoded only to paint
// it; it is never put into a label text, tooltip, accessible name or clipboard.
QString supportAddress()
{
    static constexpr unsigned char kKey[] = {0x5A, 0x13, 0xC7, 0x2E, 0x91};
    static constexpr unsigned char kScrambled[] = {
        0x1F, 0x7F, 0xA2, 0x4D, 0xE5, 0x28, 0x7C, 0xAF, 0x5B, 0xF6, 0x33,
        0x7D, 0x87, 0x49, 0xFC, 0x3B, 0x7A, 0xAB, 0x00, 0xF2, 0x35, 0x7E};
    QString plain;
    plain.reserve(int(sizeof(kScrambled)));
    for (size_t i = 0; i < sizeof(kScrambled); ++i)
        plain.append(QChar(kScrambled[i] ^ kKey[i % sizeof(kKey)]));
    return plain;
}

// Paints `text` into a transparent pixmap: visible to a person, but no text a
// bot (or a careless copy & paste) could pick up.
QPixmap textImage(const QString &text, const QFont &font, const QColor &color, qreal dpr)
{
    const QFontMetrics metrics(font);
    const QSize size(metrics.horizontalAdvance(text) + 4, metrics.height() + 2);
    QPixmap pixmap(size * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(QRect(QPoint(0, 0), size), Qt::AlignCenter, text);
    return pixmap;
}

QFrame *separator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("aboutSeparator"));
    line->setFixedHeight(1);
    return line;
}

} // namespace

QPixmap AboutDialog::brandMark(const QColor &color, int size, qreal devicePixelRatio)
{
    const QImage mask(QStringLiteral(":/app/electrohugin.png"));
    QPixmap pixmap(QSize(size, size) * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRect(0, 0, size, size), mask);
    // Keep the raven's shape (alpha), replace its color.
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(QRect(0, 0, size, size), color);
    return pixmap;
}

QString AboutDialog::systemInfo()
{
    return QStringLiteral("%1 %2\nQt %3\n%4 (%5)")
        .arg(appName(), appVersion(), QString::fromLatin1(qVersion()),
             QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
}

AboutDialog::AboutDialog(const ThemeManager &theme, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Über %1").arg(appName()));

    const QColor fg = theme.text();
    QColor muted = fg;
    muted.setAlphaF(0.60);
    QColor line = fg;
    line.setAlphaF(0.14);
    setStyleSheet(QStringLiteral(
                      "QFrame#aboutSeparator { background-color: %1; border: none; }"
                      "QLabel#aboutTitle { font-size: 18pt; font-weight: 700; }"
                      "QLabel#aboutSmall { color: %2; font-size: 8.5pt; }"
                      "QLabel#aboutAddress { font-family: monospace; padding: 6px 10px;"
                      " border: 1px solid %1; border-radius: 8px; }"
                      "QFrame#aboutMailChip { border: 1px solid %1; border-radius: 8px; }"
                      "QToolButton#aboutToggle { border: none; background: transparent;"
                      " padding: 2px 0; }")
                      .arg(line.name(QColor::HexArgb), muted.name(QColor::HexArgb)));

    // Word-wrapped labels get a fixed width so their height is known up front;
    // the dialog then sizes itself to its content (also when the help unfolds).
    constexpr int kContentWidth = 424;
    const auto wrap = [](QLabel *label) {
        label->setWordWrap(true);
        label->setFixedWidth(kContentWidth);
    };

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 18);
    layout->setSpacing(6);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    // --- Identity ---
    auto *logo = new QLabel(this);
    logo->setPixmap(brandMark(fg, 88, devicePixelRatioF()));
    logo->setFixedSize(88, 88);
    layout->addWidget(logo, 0, Qt::AlignHCenter);
    layout->addSpacing(6);

    auto *title = new QLabel(appName(), this);
    title->setObjectName(QStringLiteral("aboutTitle"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto *version = new QLabel(tr("Version %1").arg(appVersion()), this);
    version->setObjectName(QStringLiteral("kpiCaption"));
    version->setAlignment(Qt::AlignCenter);
    version->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(version);
    layout->addSpacing(8);

    auto *tagline = new QLabel(
        tr("Rollenbewertung, Best XI, Kaufberatung und Talentanalyse für die "
           "Football-Manager-Reihe."),
        this);
    tagline->setAlignment(Qt::AlignCenter);
    wrap(tagline);
    layout->addWidget(tagline);

    auto *author = new QLabel(tr("Entwickelt von <b>ElectroHugin</b>"), this);
    author->setAlignment(Qt::AlignCenter);
    layout->addWidget(author);
    layout->addSpacing(12);
    layout->addWidget(separator(this));
    layout->addSpacing(10);

    // --- Support ---
    auto *supportTitle = new QLabel(tr("Support"), this);
    supportTitle->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(supportTitle);

    auto *issueText = new QLabel(
        tr("Fehler gefunden oder eine Idee? Bitte öffne ein Issue auf GitHub. So bleibt "
           "alles an einem Ort, und andere finden die Antwort später auch."),
        this);
    wrap(issueText);
    layout->addWidget(issueText);

    // Deliberately plain text, not a link (the app opens no web pages).
    auto *address =
        new QLabel(QStringLiteral("github.com/ElectroHugin/FM-Player-Analyzer/issues"), this);
    address->setObjectName(QStringLiteral("aboutAddress"));
    address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    address->setCursor(Qt::IBeamCursor);
    layout->addWidget(address);

    auto *issueToggle = new QToolButton(this);
    issueToggle->setObjectName(QStringLiteral("aboutToggle"));
    issueToggle->setText(tr("Was gehört in ein Issue?"));
    issueToggle->setCheckable(true);
    issueToggle->setArrowType(Qt::RightArrow);
    issueToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    issueToggle->setCursor(Qt::PointingHandCursor);
    layout->addWidget(issueToggle, 0, Qt::AlignLeft);

    auto *issueHelp = new QLabel(
        tr("• App-Version und Betriebssystem (Knopf „Systeminfo kopieren“ unten)<br>"
           "• Football-Manager-Version und welcher Export (Kader, Shortlist, Suche)<br>"
           "• Was du getan hast, was passiert ist und was du erwartet hättest<br>"
           "• Wenn möglich ein Screenshot oder die betroffene Exportdatei"),
        this);
    wrap(issueHelp);
    issueHelp->setVisible(false);
    layout->addWidget(issueHelp);
    connect(issueToggle, &QToolButton::toggled, this, [issueToggle, issueHelp](bool on) {
        issueToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        issueHelp->setVisible(on);
    });
    layout->addSpacing(8);

    auto *mailText = new QLabel(tr("Für alles andere per E-Mail:"), this);
    wrap(mailText);
    layout->addWidget(mailText);

    auto *mailChip = new QFrame(this);
    mailChip->setObjectName(QStringLiteral("aboutMailChip"));
    auto *chipLayout = new QHBoxLayout(mailChip);
    chipLayout->setContentsMargins(10, 6, 10, 6);
    auto *mailImage = new QLabel(mailChip);
    QFont mailFont = font();
    mailFont.setPointSizeF(mailFont.pointSizeF() * 1.1);
    mailImage->setPixmap(textImage(supportAddress(), mailFont, fg, devicePixelRatioF()));
    mailImage->setAccessibleName(tr("E-Mail-Adresse (als Bild)"));
    chipLayout->addWidget(mailImage);
    chipLayout->addStretch(1);
    layout->addWidget(mailChip);

    auto *mailHint = new QLabel(
        tr("Die Adresse ist absichtlich ein Bild, damit Spam-Bots sie nicht auslesen "
           "können. Bitte von Hand abtippen."),
        this);
    mailHint->setObjectName(QStringLiteral("aboutSmall"));
    wrap(mailHint);
    layout->addWidget(mailHint);
    layout->addSpacing(12);
    layout->addWidget(separator(this));
    layout->addSpacing(6);

    // --- Legal ---
    auto *legal = new QLabel(
        tr("© 2026 ElectroHugin · Freie Software unter der GNU GPL v3<br>"
           "Nicht offiziell mit Sports Interactive oder SEGA verbunden."),
        this);
    legal->setObjectName(QStringLiteral("aboutSmall"));
    legal->setAlignment(Qt::AlignCenter);
    wrap(legal);
    layout->addWidget(legal);
    layout->addSpacing(8);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Schließen"));
    buttons->button(QDialogButtonBox::Close)->setDefault(true);
    auto *copyInfo =
        buttons->addButton(tr("Systeminfo kopieren"), QDialogButtonBox::ActionRole);
    copyInfo->setToolTip(tr("Version, Qt und Betriebssystem für ein Issue in die "
                            "Zwischenablage kopieren"));
    connect(copyInfo, &QPushButton::clicked, this, [copyInfo] {
        QApplication::clipboard()->setText(systemInfo());
        copyInfo->setText(tr("Kopiert ✓"));
        QTimer::singleShot(1800, copyInfo, [copyInfo] {
            copyInfo->setText(tr("Systeminfo kopieren"));
        });
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace fm
