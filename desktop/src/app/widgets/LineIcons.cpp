#include "LineIcons.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace fm {
namespace LineIcons {

namespace {

// Paths on a 24x24 grid, stroked (no fill), 1.8 units wide.
QPainterPath pathFor(const QString &name)
{
    QPainterPath p;
    if (name == QLatin1String("home")) {
        p.moveTo(3, 11.5); p.lineTo(12, 4); p.lineTo(21, 11.5);
        p.moveTo(5.5, 10); p.lineTo(5.5, 20); p.lineTo(18.5, 20); p.lineTo(18.5, 10);
        p.moveTo(10, 20); p.lineTo(10, 14.5); p.lineTo(14, 14.5); p.lineTo(14, 20);
    } else if (name == QLatin1String("pitch")) {
        p.addRoundedRect(3, 4, 18, 16, 2, 2);
        p.moveTo(12, 4); p.lineTo(12, 20);
        p.addEllipse(QPointF(12, 12), 2.6, 2.6);
        p.moveTo(3, 9); p.lineTo(6, 9); p.lineTo(6, 15); p.lineTo(3, 15);
        p.moveTo(21, 9); p.lineTo(18, 9); p.lineTo(18, 15); p.lineTo(21, 15);
    } else if (name == QLatin1String("list")) {
        for (const qreal y : {6.0, 12.0, 18.0}) {
            p.addEllipse(QPointF(4.5, y), 0.9, 0.9);
            p.moveTo(8.5, y); p.lineTo(20, y);
        }
    } else if (name == QLatin1String("trend")) {
        p.moveTo(3, 18); p.lineTo(9, 12); p.lineTo(13, 15.5); p.lineTo(21, 7);
        p.moveTo(16, 7); p.lineTo(21, 7); p.lineTo(21, 12);
    } else if (name == QLatin1String("transfer")) {
        p.moveTo(4, 8); p.lineTo(19, 8); p.moveTo(15.5, 4.5); p.lineTo(19, 8); p.lineTo(15.5, 11.5);
        p.moveTo(20, 16); p.lineTo(5, 16); p.moveTo(8.5, 12.5); p.lineTo(5, 16); p.lineTo(8.5, 19.5);
    } else if (name == QLatin1String("search")) {
        p.addEllipse(QPointF(10.5, 10.5), 6, 6);
        p.moveTo(15, 15); p.lineTo(20.5, 20.5);
    } else if (name == QLatin1String("person")) {
        p.addEllipse(QPointF(12, 8), 3.8, 3.8);
        p.moveTo(4.5, 20.5);
        p.cubicTo(5.5, 15.5, 8.5, 14, 12, 14);
        p.cubicTo(15.5, 14, 18.5, 15.5, 19.5, 20.5);
    } else if (name == QLatin1String("people")) {
        p.addEllipse(QPointF(9, 8.5), 3.2, 3.2);
        p.moveTo(3, 19.5); p.cubicTo(3.8, 15.5, 6, 14, 9, 14); p.cubicTo(12, 14, 14.2, 15.5, 15, 19.5);
        p.moveTo(15, 5.6); p.cubicTo(17.5, 5.6, 18.6, 7.5, 18.2, 9.4);
        p.moveTo(17, 14.2); p.cubicTo(19.2, 14.6, 20.6, 16.4, 21, 19.5);
    } else if (name == QLatin1String("grid")) {
        p.addRoundedRect(4, 4, 7, 7, 1.5, 1.5);
        p.addRoundedRect(13, 4, 7, 7, 1.5, 1.5);
        p.addRoundedRect(4, 13, 7, 7, 1.5, 1.5);
        p.addRoundedRect(13, 13, 7, 7, 1.5, 1.5);
    } else if (name == QLatin1String("gear")) {
        p.addEllipse(QPointF(12, 12), 3, 3);
        p.addEllipse(QPointF(12, 12), 7, 7);
        for (int i = 0; i < 8; ++i) {
            QTransform t;
            t.translate(12, 12);
            t.rotate(i * 45);
            p.moveTo(t.map(QPointF(0, -7)));
            p.lineTo(t.map(QPointF(0, -9.5)));
        }
    } else if (name == QLatin1String("moon")) {
        p.moveTo(19.5, 14.5);
        p.cubicTo(18, 18.5, 13, 20.5, 9, 18.5);
        p.cubicTo(4.5, 16, 3.5, 10.5, 6.5, 7);
        p.cubicTo(7.8, 5.5, 9.5, 4.6, 11, 4.3);
        p.cubicTo(8.5, 8, 10, 13, 14, 14.8);
        p.cubicTo(16, 15.6, 18, 15.4, 19.5, 14.5);
    } else if (name == QLatin1String("upload")) {
        p.moveTo(12, 15); p.lineTo(12, 3.5);
        p.moveTo(7.5, 8); p.lineTo(12, 3.5); p.lineTo(16.5, 8);
        p.moveTo(4, 14.5); p.lineTo(4, 20); p.lineTo(20, 20); p.lineTo(20, 14.5);
    } else if (name == QLatin1String("flag")) {
        p.moveTo(5, 21); p.lineTo(5, 4);
        p.moveTo(5, 4.5); p.cubicTo(9, 2.5, 12, 6.5, 19, 4.5);
        p.lineTo(19, 13); p.cubicTo(12, 15, 9, 11, 5, 13);
    } else if (name == QLatin1String("plus")) {
        p.moveTo(12, 5); p.lineTo(12, 19); p.moveTo(5, 12); p.lineTo(19, 12);
    } else if (name == QLatin1String("folder")) {
        p.moveTo(3, 6); p.lineTo(9, 6); p.lineTo(11, 8.5); p.lineTo(21, 8.5);
        p.lineTo(21, 19); p.lineTo(3, 19); p.closeSubpath();
    }
    return p;
}

} // namespace

QIcon icon(const QString &name, const QColor &color, int size, qreal dpr)
{
    const int px = qRound(size * dpr);
    QPixmap pixmap(px, px);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(dpr);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 24.0, size / 24.0);
    QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(pathFor(name));
    painter.end();
    return QIcon(pixmap);
}

QString forGroup(const QString &groupId)
{
    static const QHash<QString, QString> icons = {
        {QStringLiteral("overview"), QStringLiteral("home")},
        {QStringLiteral("national_overview"), QStringLiteral("home")},
        {QStringLiteral("lineup"), QStringLiteral("pitch")},
        {QStringLiteral("national_lineup"), QStringLiteral("pitch")},
        {QStringLiteral("squad"), QStringLiteral("list")},
        {QStringLiteral("national_squad"), QStringLiteral("people")},
        {QStringLiteral("development"), QStringLiteral("trend")},
        {QStringLiteral("national_development"), QStringLiteral("trend")},
        {QStringLiteral("transfers"), QStringLiteral("transfer")},
        {QStringLiteral("scouting"), QStringLiteral("search")},
        {QStringLiteral("players"), QStringLiteral("person")},
        {QStringLiteral("roles_tactics"), QStringLiteral("grid")},
        {QStringLiteral("settings"), QStringLiteral("gear")},
    };
    return icons.value(groupId);
}

} // namespace LineIcons
} // namespace fm
