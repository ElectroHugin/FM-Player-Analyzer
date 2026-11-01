#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace fm {

// Small built-in line icons (drawn with QPainter, so they work offline and
// follow the theme color). Names: home, pitch, list, trend, transfer, search,
// person, grid, gear, moon, upload, flag, people, plus, folder.
namespace LineIcons {

QIcon icon(const QString &name, const QColor &color, int size = 18, qreal dpr = 1.0);

// Sidebar group id (Navigation) -> icon name.
QString forGroup(const QString &groupId);

} // namespace LineIcons
} // namespace fm
