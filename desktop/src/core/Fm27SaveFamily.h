#pragma once

// The files of one FM27 career on disk. Football Manager keeps a save's older
// versions next to it as "<name> (v02).fm", "<name> (v03).fm", ...; an
// autosave rotates through the same names. The live import follows the newest
// of them, so it always reads the latest state of the career the user chose.

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace fm::Fm27SaveFamily {

// "<dir>/Career (v02).fm" -> "Career"; "" when the path is not a .fm file.
QString baseName(const QString &savePath);

// Whether fileName ("Career (v03).fm") belongs to the career named base.
bool belongsTo(const QString &fileName, const QString &base);

struct Member {
    QString path;
    QDateTime modified;
    qint64 size = 0;
};

// The newest file of savePath's career in its folder (savePath itself
// included); an invalid Member (empty path) when none exists.
Member newest(const QString &savePath);

} // namespace fm::Fm27SaveFamily
