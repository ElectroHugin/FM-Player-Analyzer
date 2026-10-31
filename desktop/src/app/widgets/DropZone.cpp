#include "DropZone.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace fm {

DropZone::DropZone(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("dropZone"));
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(150);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(4);
    layout->addStretch(1);
    auto *arrow = new QLabel(QStringLiteral("⇪"), this);
    arrow->setObjectName(QStringLiteral("dropZoneIcon"));
    arrow->setAlignment(Qt::AlignCenter);
    layout->addWidget(arrow);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("dropZoneTitle"));
    m_title->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_title);
    m_detail = new QLabel(this);
    m_detail->setObjectName(QStringLiteral("kpiCaption"));
    m_detail->setAlignment(Qt::AlignCenter);
    m_detail->setWordWrap(true);
    layout->addWidget(m_detail);
    layout->addStretch(1);
    setFile({});
}

void DropZone::setFile(const QString &path)
{
    if (path.isEmpty()) {
        m_title->setText(tr("Export hierher ziehen"));
        m_detail->setText(tr("CSV oder HTML aus dem Football Manager – oder klicken, um "
                             "eine Datei auszuwählen"));
    } else {
        m_title->setText(QFileInfo(path).fileName());
        m_detail->setText(tr("Klicken, um eine andere Datei zu wählen"));
    }
}

bool DropZone::isImportableFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("html") || suffix == QLatin1String("htm")
           || suffix == QLatin1String("csv");
}

QString DropZone::importableFileFrom(const QMimeData *mime)
{
    if (!mime || !mime->hasUrls())
        return {};
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile() && isImportableFile(url.toLocalFile()))
            return url.toLocalFile();
    }
    return {};
}

void DropZone::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QFrame::mousePressEvent(event);
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("FM-Export (HTML oder CSV) auswählen"), m_startDir,
        tr("FM-Exporte (*.html *.htm *.csv);;Alle Dateien (*)"));
    if (!path.isEmpty()) {
        setFile(path);
        emit fileChosen(path);
    }
}

void DropZone::dragEnterEvent(QDragEnterEvent *event)
{
    if (!importableFileFrom(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        setHovered(true);
    }
}

void DropZone::dragLeaveEvent(QDragLeaveEvent *event)
{
    setHovered(false);
    QFrame::dragLeaveEvent(event);
}

void DropZone::dropEvent(QDropEvent *event)
{
    setHovered(false);
    const QString path = importableFileFrom(event->mimeData());
    if (path.isEmpty())
        return;
    event->acceptProposedAction();
    setFile(path);
    emit fileChosen(path);
}

void DropZone::setHovered(bool hovered)
{
    setProperty("hovered", hovered);
    style()->unpolish(this);
    style()->polish(this);
}

} // namespace fm
