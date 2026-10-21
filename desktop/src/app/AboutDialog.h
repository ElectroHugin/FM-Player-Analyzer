#pragma once

#include <QDialog>
#include <QPixmap>

class QColor;

namespace fm {

class ThemeManager;

// Help → About: app name and version, the author (ElectroHugin) with the
// raven logo, license, and where to get support. The support e-mail address
// is not stored as plain text in the binary and is shown only as a painted
// image, so it can't be harvested or copied — a human reads and types it.
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(const ThemeManager &theme, QWidget *parent = nullptr);

    // The ElectroHugin raven (an alpha mask resource) tinted in `color`,
    // `size` logical pixels square, sharp on high-DPI screens.
    static QPixmap brandMark(const QColor &color, int size, qreal devicePixelRatio);

private:
    // Version, Qt and OS in one block for bug reports.
    static QString systemInfo();
};

} // namespace fm
