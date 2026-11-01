#include "SaveLibrary.h"

#include "AppContext.h"
#include "core/Constants.h"
#include "core/Database.h"
#include "core/Utils.h"
#include "theming/ThemePresets.h"

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
                                             "'club_logo_path','last_import_at',"
                                             "'favorite_tactic_1')"))) {
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
                    else if (key == QLatin1String("last_import_at"))
                        info.lastImport = QDateTime::fromString(value, Qt::ISODate);
                    else if (key == QLatin1String("favorite_tactic_1"))
                        info.tactic = value;
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
    info.tactic = context.database().setting(QStringLiteral("favorite_tactic_1"));
    info.lastImport = QDateTime::fromString(
        context.database().setting(QStringLiteral("last_import_at")), Qt::ISODate);
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
    const QString initials = save.clubName.isEmpty() ? QStringLiteral("?") : clubInitials(source);
    // Club colors where a club theme matches ("FC Bayern" ~ "FC Bayern München"),
    // else a stable muted hue per name so saves are told apart at a glance.
    static const QList<QColor> palette = {
        QColor(0x25, 0x63, 0xeb), QColor(0x0d, 0x94, 0x88), QColor(0x16, 0xa3, 0x4a),
        QColor(0xea, 0x58, 0x0c), QColor(0xdc, 0x26, 0x26), QColor(0xdb, 0x27, 0x77),
        QColor(0x4f, 0x46, 0xe5), QColor(0x47, 0x55, 0x69)};
    QColor fill = save.clubName.isEmpty()
                      ? QColor(0x55, 0x50, 0x63)
                      : palette.at(int(qHash(source.toLower()) % palette.size()));
    if (!save.clubName.isEmpty()) {
        const QString club = save.clubName.toLower();
        for (const ClubTheme &theme : clubThemes()) {
            const QString themeName = theme.displayName.toLower();
            if (theme.id != QLatin1String("neutral")
                && (themeName.startsWith(club) || club.startsWith(themeName))) {
                fill = QColor(theme.primary);
                break;
            }
        }
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawEllipse(QRectF(0, 0, size, size));
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(qMax(7, qRound(size * (initials.size() > 2 ? 0.30 : 0.36))));
    painter.setFont(font);
    painter.setPen(contrastRatio(fill, QColorConstants::White) >= 3.0 ? QColorConstants::White
                                                                      : QColorConstants::Black);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, initials);
    return pixmap;
}


QString clubInitials(const QString &name)
{
    // Letter-only words; a short leading abbreviation ("FC", "SC", "VfB")
    // is kept whole, then the first letter of each following word.
    QStringList words;
    for (const QString &word : name.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (word.at(0).isLetter())
            words << word;
    }
    if (words.isEmpty())
        return QStringLiteral("?");
    QString initials;
    int start = 0;
    if (words.size() > 1 && words.first().size() <= 3) {
        initials = words.first().toUpper();
        start = 1;
    }
    for (int i = start; i < words.size() && initials.size() < 3; ++i)
        initials += words.at(i).at(0).toUpper();
    return initials.left(3);
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
