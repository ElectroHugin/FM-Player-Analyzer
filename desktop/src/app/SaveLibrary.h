#pragma once

#include <QDateTime>
#include <QList>
#include <QPixmap>
#include <QString>

namespace fm {

class AppContext;

// What the start screen shows about one save (database) without loading it:
// read straight from the file's settings table.
struct SaveInfo {
    QString name;        // file name without ".db"
    QString filePath;
    QString fmVersionId; // e.g. "fm27"; FM24 for unmarked legacy saves
    QString clubName;    // full club name if set, else the managed club
    QString logoPath;    // club logo (per save), may be empty
    int playerCount = -1; // -1 = could not be read
    QDateTime lastOpened; // invalid = never opened by this version
    QDateTime modified;   // file modification time
};

namespace SaveLibrary {

// The open save, described from the live context (no file access).
SaveInfo current(AppContext &context);

// All saves in the data directory, most recently used first.
QList<SaveInfo> scan(AppContext &context);

// Club logo of the save, or a colored disc with the club's initials.
QPixmap crest(const SaveInfo &save, int size, qreal dpr);

// Short label of an FM version for chips ("FM27").
QString shortVersionLabel(const QString &fmVersionId);

// Checks a user-typed save name; returns an error text or an empty string.
QString validateName(AppContext &context, const QString &name);

// File operations on saves that are not open. Each returns false with a
// user-facing message in errorOut.
bool rename(AppContext &context, const QString &oldName, const QString &newName,
            QString *errorOut);
bool duplicate(AppContext &context, const QString &name, const QString &newName,
               QString *errorOut);
// Backs the save up into the backups folder first, then deletes it.
bool remove(AppContext &context, const QString &name, QString *errorOut);
// Copies an external .db file into the data directory under newName.
bool importFile(AppContext &context, const QString &sourcePath, const QString &newName,
                QString *errorOut);

} // namespace SaveLibrary
} // namespace fm
