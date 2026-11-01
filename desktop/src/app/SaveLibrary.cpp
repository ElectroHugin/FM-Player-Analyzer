#include "SaveLibrary.h"

#include "AppContext.h"
#include "core/Constants.h"
#include "core/Database.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>

namespace fm {
namespace SaveLibrary {

namespace {

// Translation context "SaveLibrary" for this free-function module (see
// ImportRunner: a plain helper would hide the strings from lupdate).
struct SaveText {
    Q_DECLARE_TR_FUNCTIONS(SaveLibrary)
};

// The database runs in WAL mode: a save on disk can consist of up to three files.
QStringList sidecars(const QString &dbPath)
{
    return {dbPath, dbPath + QStringLiteral("-wal"), dbPath + QStringLiteral("-shm")};
}

void readInfo(SaveInfo &info)
{
    const QString connName =
        QStringLiteral("saveinfo_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(info.filePath);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=500"));
        if (db.open()) {
            QSqlQuery settings(db);
            if (settings.exec(QStringLiteral("SELECT key, value FROM settings WHERE key IN "
                                             "('fm_version','user_club','full_club_name',"
                                             "'club_logo_path')"))) {
                QString userClub;
                QString fullName;
                while (settings.next()) {
                    const QString key = settings.value(0).toString();
                    const QString value = settings.value(1).toString();
                    if (key == QLatin1String("fm_version"))
                        info.fmVersionId = value;
                    else if (key == QLatin1String("user_club"))
                        userClub = value;
                    else if (key == QLatin1String("full_club_name"))
                        fullName = value;
                    else if (key == QLatin1String("club_logo_path"))
                        info.logoPath = value;
                }
                info.clubName = fullName.isEmpty() ? userClub : fullName;
            }
            QSqlQuery count(db);
            if (count.exec(QStringLiteral("SELECT COUNT(*) FROM players")) && count.next())
                info.playerCount = count.value(0).toInt();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connName);
    if (info.fmVersionId.isEmpty())
        info.fmVersionId = defaultFmVersionId();
}

bool isOpenSave(AppContext &context, const QString &name)
{
    return context.hasDatabase() && context.currentDbName() == name;
}

} // namespace

SaveInfo current(AppContext &context)
{
    SaveInfo info;
    info.name = context.currentDbName();
    info.filePath = context.paths().databaseFile(info.name);
    info.fmVersionId = context.fmVersionId();
    const QString fullName = context.database().setting(QStringLiteral("full_club_name"));
    info.clubName = fullName.isEmpty() ? context.userClub() : fullName;
    info.logoPath = context.database().setting(QStringLiteral("club_logo_path"));
    info.playerCount = static_cast<int>(context.store().size());
    return info;
}

QList<SaveInfo> scan(AppContext &context)
{
    QList<SaveInfo> saves;
    for (const QString &name : context.availableDatabases()) {
        SaveInfo info;
        info.name = name;
        info.filePath = context.paths().databaseFile(name);
        info.modified = QFileInfo(info.filePath).lastModified();
        info.lastOpened = context.config().databaseLastOpened(name);
        readInfo(info);
        saves.append(info);
    }
    std::sort(saves.begin(), saves.end(), [](const SaveInfo &a, const SaveInfo &b) {
        const QDateTime ta = a.lastOpened.isValid() ? a.lastOpened : a.modified;
        const QDateTime tb = b.lastOpened.isValid() ? b.lastOpened : b.modified;
        if (ta != tb)
            return ta > tb;
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return saves;
}

QPixmap crest(const SaveInfo &save, int size, qreal dpr)
{
    const int px = qRound(size * dpr);
    QPixmap pixmap(px, px);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(dpr);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    if (!save.logoPath.isEmpty()) {
        const QPixmap logo(save.logoPath);
        if (!logo.isNull()) {
            const QPixmap scaled = logo.scaled(QSize(px, px), Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
            const QSizeF logoSize = QSizeF(scaled.size()) / dpr;
            painter.drawPixmap(QPointF((size - logoSize.width()) / 2.0,
                                       (size - logoSize.height()) / 2.0),
                               scaled);
            return pixmap;
        }
    }

    const QString source = save.clubName.isEmpty() ? save.name : save.clubName;
    QString initials;
    for (const QString &word : source.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (word.at(0).isLetterOrNumber())
            initials += word.at(0).toUpper();
        if (initials.size() == 3)
            break;
    }
    if (initials.isEmpty())
        initials = QStringLiteral("?");
    // Stable, muted hue per name so saves are told apart at a glance.
    const int hue = int(qHash(source) % 360);
    painter.setPen(Qt::NoPen);
    painter.setBrush(save.clubName.isEmpty() ? QColor(0x55, 0x50, 0x63)
                                             : QColor::fromHsl(hue, 120, 92));
    painter.drawEllipse(QRectF(0, 0, size, size));
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSizeF(initials.size() > 2 ? 8.0 : 9.5);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, initials);
    return pixmap;
}


QString shortVersionLabel(const QString &fmVersionId)
{
    return fmVersionId.toUpper();
}

QString validateName(AppContext &context, const QString &rawName)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty())
        return SaveText::tr("Bitte einen Namen eingeben.");
    static const QString forbidden = QStringLiteral("\\/:*?\"<>|");
    for (const QChar c : name) {
        if (forbidden.contains(c) || c.unicode() < 32)
            return SaveText::tr("Der Name darf diese Zeichen nicht enthalten: \\ / : * ? \" < > |");
    }
    if (name.startsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char('.')))
        return SaveText::tr("Der Name darf nicht mit einem Punkt beginnen oder enden.");
    if (name.size() > 80)
        return SaveText::tr("Der Name ist zu lang (höchstens 80 Zeichen).");
    for (const QString &existing : context.availableDatabases()) {
        if (existing.compare(name, Qt::CaseInsensitive) == 0)
            return SaveText::tr("Ein Spielstand mit diesem Namen existiert bereits.");
    }
    return {};
}

bool rename(AppContext &context, const QString &oldName, const QString &newName,
            QString *errorOut)
{
    if (isOpenSave(context, oldName)) {
        if (errorOut)
            *errorOut = SaveText::tr("Der geöffnete Spielstand kann nicht umbenannt werden. "
                           "Öffne vorher einen anderen.");
        return false;
    }
    const QString error = validateName(context, newName);
    if (!error.isEmpty()) {
        if (errorOut)
            *errorOut = error;
        return false;
    }
    const QStringList from = sidecars(context.paths().databaseFile(oldName));
    const QStringList to = sidecars(context.paths().databaseFile(newName.trimmed()));
    for (int i = 0; i < from.size(); ++i) {
        if (QFile::exists(from.at(i)) && !QFile::rename(from.at(i), to.at(i))) {
            if (errorOut)
                *errorOut = SaveText::tr("Die Datei %1 konnte nicht umbenannt werden.")
                                .arg(QDir::toNativeSeparators(from.at(i)));
            return false;
        }
    }
    context.config().renameDatabaseEntry(oldName, newName.trimmed());
    return true;
}

bool duplicate(AppContext &context, const QString &name, const QString &newName,
               QString *errorOut)
{
    const QString error = validateName(context, newName);
    if (!error.isEmpty()) {
        if (errorOut)
            *errorOut = error;
        return false;
    }
    // VACUUM INTO writes a consistent copy including committed WAL frames.
    const QString target = context.paths().databaseFile(newName.trimmed());
    const QString connName =
        QStringLiteral("duplicate_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    bool ok = false;
    QString dbError;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(context.paths().databaseFile(name));
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("VACUUM INTO ?"));
            query.bindValue(0, target);
            ok = query.exec();
            if (!ok)
                dbError = query.lastError().text();
            db.close();
        } else {
            dbError = db.lastError().text();
        }
    }
    QSqlDatabase::removeDatabase(connName);
    if (!ok && errorOut)
        *errorOut = SaveText::tr("Der Spielstand konnte nicht kopiert werden: %1").arg(dbError);
    return ok;
}

bool remove(AppContext &context, const QString &name, QString *errorOut)
{
    if (isOpenSave(context, name)) {
        if (errorOut)
            *errorOut = SaveText::tr("Der geöffnete Spielstand kann nicht gelöscht werden. "
                           "Öffne vorher einen anderen.");
        return false;
    }
    const QString path = context.paths().databaseFile(name);
    QString backupError;
    if (!Database::createBackup(path, context.paths().backupsDir(), &backupError)) {
        if (errorOut)
            *errorOut = SaveText::tr("Vor dem Löschen konnte kein Backup angelegt werden, deshalb "
                           "wurde nichts gelöscht.\n\n%1")
                            .arg(backupError);
        return false;
    }
    for (const QString &file : sidecars(path)) {
        if (QFile::exists(file) && !QFile::remove(file)) {
            if (errorOut)
                *errorOut = SaveText::tr("Die Datei %1 konnte nicht gelöscht werden.")
                                .arg(QDir::toNativeSeparators(file));
            return false;
        }
    }
    context.config().removeDatabaseEntry(name);
    return true;
}

bool importFile(AppContext &context, const QString &sourcePath, const QString &newName,
                QString *errorOut)
{
    const QString error = validateName(context, newName);
    if (!error.isEmpty()) {
        if (errorOut)
            *errorOut = error;
        return false;
    }
    QDir().mkpath(context.paths().databasesDir());
    if (!QFile::copy(sourcePath, context.paths().databaseFile(newName.trimmed()))) {
        if (errorOut)
            *errorOut = SaveText::tr("Die Datei konnte nicht in den Datenordner kopiert werden.");
        return false;
    }
    return true;
}

} // namespace SaveLibrary
} // namespace fm
