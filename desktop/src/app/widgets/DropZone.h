#pragma once

#include <QFrame>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QLabel;
class QMimeData;
class QMouseEvent;

namespace fm {

// Dashed area that accepts an FM export (HTML/CSV) by drag & drop or, on
// click, through a file dialog. Emits fileChosen with the local path.
class DropZone : public QFrame
{
    Q_OBJECT

public:
    explicit DropZone(QWidget *parent = nullptr);

    // Folder the file dialog starts in.
    void setStartDir(const QString &dir) { m_startDir = dir; }
    // Shows the chosen file inside the zone (empty = prompt text).
    void setFile(const QString &path);

    // True if the path looks like an importable FM export.
    static bool isImportableFile(const QString &path);
    // First importable local file of a drag's mime data, or empty.
    static QString importableFileFrom(const QMimeData *mime);

signals:
    void fileChosen(const QString &path);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void setHovered(bool hovered);

    QString m_startDir;
    QLabel *m_title;
    QLabel *m_detail;
};

} // namespace fm
