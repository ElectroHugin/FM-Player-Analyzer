#include "ProfileStore.h"

#include "TacticLibrary.h"

#include "Database.h"
#include "Definitions.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace fm {

namespace {

enum Encoding : int { Raw = 0, Zlib = 1 };

bool decode(int encoding, const QByteArray &stored, QByteArray &out)
{
    if (encoding == Zlib) {
        out = qUncompress(stored);
        return !out.isEmpty() || stored.isEmpty();
    }
    out = stored;
    return true;
}

} // namespace

QString ProfileStore::definitionsDocument(const QString &fmVersionId)
{
    return QStringLiteral("definitions_") + fmVersionId;
}

QString ProfileStore::clubLogoDocument()
{
    return QStringLiteral("club_logo");
}

QString ProfileStore::nationalFlagDocument()
{
    return QStringLiteral("national_flag");
}

QString ProfileStore::formatKey()
{
    return QStringLiteral("Profile/format");
}

QString ProfileStore::seedKey()
{
    return QStringLiteral("Profile/seed");
}

QString ProfileStore::createdKey()
{
    return QStringLiteral("Profile/created_at");
}

ProfileStore::ProfileStore(Database &db)
    : m_db(db)
{
}

bool ProfileStore::ensureTables()
{
    QSqlQuery query(m_db.handle());
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS profile_settings ("
                                   "key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID"))
        || !query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS profile_documents ("
                                      "name TEXT PRIMARY KEY, encoding INTEGER NOT NULL, "
                                      "size INTEGER NOT NULL, data BLOB NOT NULL, "
                                      "updated_at TEXT NOT NULL)"))) {
        m_error = query.lastError().text();
        return false;
    }
    return true;
}

bool ProfileStore::isInitialized()
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT 1 FROM profile_settings WHERE key = ?"));
    query.bindValue(0, formatKey());
    return query.exec() && query.next();
}

bool ProfileStore::loadSettings(QHash<QString, QString> &out)
{
    out.clear();
    QSqlQuery query(m_db.handle());
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral("SELECT key, value FROM profile_settings"))) {
        m_error = query.lastError().text();
        return false;
    }
    while (query.next())
        out.insert(query.value(0).toString(), query.value(1).toString());
    return true;
}

bool ProfileStore::writeSettings(const QHash<QString, QString> &upserts,
                                 const QStringList &removals)
{
    if (upserts.isEmpty() && removals.isEmpty())
        return true;
    ScopedTransaction tx(m_db);
    if (!tx.isActive()) {
        m_error = m_db.errorString();
        return false;
    }
    QSqlQuery upsert(m_db.handle());
    upsert.prepare(QStringLiteral(
        "INSERT INTO profile_settings (key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    for (auto it = upserts.constBegin(); it != upserts.constEnd(); ++it) {
        upsert.bindValue(0, it.key());
        upsert.bindValue(1, it.value());
        if (!upsert.exec()) {
            m_error = upsert.lastError().text();
            return false;
        }
    }
    QSqlQuery remove(m_db.handle());
    remove.prepare(QStringLiteral("DELETE FROM profile_settings WHERE key = ?"));
    for (const QString &key : removals) {
        remove.bindValue(0, key);
        if (!remove.exec()) {
            m_error = remove.lastError().text();
            return false;
        }
    }
    if (!tx.commit()) {
        m_error = m_db.errorString();
        return false;
    }
    return true;
}

bool ProfileStore::hasDocument(const QString &name)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT 1 FROM profile_documents WHERE name = ?"));
    query.bindValue(0, name);
    return query.exec() && query.next();
}

bool ProfileStore::readDocument(const QString &name, QByteArray &out)
{
    m_error.clear();
    out.clear();
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT encoding, data FROM profile_documents WHERE name = ?"));
    query.bindValue(0, name);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return false;
    }
    if (!query.next())
        return false;
    if (!decode(query.value(0).toInt(), query.value(1).toByteArray(), out)) {
        m_error = QStringLiteral("Profile document '%1' is damaged.").arg(name);
        return false;
    }
    return true;
}

bool ProfileStore::writeDocument(const QString &name, const QByteArray &data, bool compress)
{
    const QByteArray stored = compress ? qCompress(data, 6) : data;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "INSERT INTO profile_documents (name, encoding, size, data, updated_at) "
        "VALUES (?, ?, ?, ?, ?) ON CONFLICT(name) DO UPDATE SET encoding = excluded.encoding, "
        "size = excluded.size, data = excluded.data, updated_at = excluded.updated_at"));
    query.bindValue(0, name);
    query.bindValue(1, compress ? Zlib : Raw);
    query.bindValue(2, data.size());
    query.bindValue(3, stored);
    query.bindValue(4, QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!query.exec()) {
        m_error = query.lastError().text();
        return false;
    }
    return true;
}

bool ProfileStore::removeDocument(const QString &name)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("DELETE FROM profile_documents WHERE name = ?"));
    query.bindValue(0, name);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return false;
    }
    return true;
}

qint64 ProfileStore::storedDocumentBytes()
{
    QSqlQuery query(m_db.handle());
    if (query.exec(QStringLiteral("SELECT COALESCE(SUM(LENGTH(data)), 0) FROM profile_documents"))
        && query.next())
        return query.value(0).toLongLong();
    return 0;
}

bool ProfileStore::loadDefinitions(const QString &fmVersionId, const QByteArray &shippedJson,
                                   const QByteArray &sharedJson, DefinitionsResult &out)
{
    out = DefinitionsResult();
    const QString name = definitionsDocument(fmVersionId);
    if (!readDocument(name, out.json)) {
        if (!m_error.isEmpty())
            return false; // damaged or unreadable: never overwrite it
        out.json = sharedJson.isEmpty() ? shippedJson : sharedJson;
        if (out.json.isEmpty()) {
            m_error = QStringLiteral("No roles and tactics available for %1.").arg(fmVersionId);
            return false;
        }
        if (!writeDocument(name, out.json, true))
            return false;
        out.seeded = true;
    }

    const QJsonObject shipped = QJsonDocument::fromJson(shippedJson).object();
    QJsonObject user = QJsonDocument::fromJson(out.json).object();
    if (!shipped.isEmpty() && !user.isEmpty() && Definitions::applyShippedUpdate(shipped, user, &out.renamedTactics)) {
        const QByteArray updated = QJsonDocument(user).toJson(QJsonDocument::Indented);
        if (!writeDocument(name, updated, true))
            return false;
        out.beforeUpdate = out.json;
        out.json = updated;
        for (const auto &rename : std::as_const(out.renamedTactics))
            TacticLibrary::renameTacticReferences(m_db, rename.first, rename.second);
    }
    return true;
}

bool ProfileStore::saveDefinitions(const QString &fmVersionId, const QByteArray &json)
{
    return writeDocument(definitionsDocument(fmVersionId), json, true);
}

bool ProfileStore::readDocument(const QString &connectionName, const QString &name,
                                QByteArray &out)
{
    out.clear();
    QSqlQuery query(QSqlDatabase::database(connectionName, false));
    query.prepare(QStringLiteral("SELECT encoding, data FROM profile_documents WHERE name = ?"));
    query.bindValue(0, name);
    if (!query.exec() || !query.next())
        return false;
    return decode(query.value(0).toInt(), query.value(1).toByteArray(), out);
}

} // namespace fm
