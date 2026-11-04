#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

namespace fm {

class Database;

// Storage of one save's profile inside the save's own SQLite file.
//
// A profile is everything that makes a save look and rate the way its manager
// set it up: weights and analysis options, the complete color scheme including
// day/night mode, the import folder, roles and tactics (definitions), club
// logo and national flag. Keeping it in the .db file makes a save
// self-contained: duplicating, renaming, backing up or importing a save takes
// its profile along, and two saves never share a setting by accident.
//
// Two tables, created on demand (no schema-version bump of Database):
//   profile_settings  (key TEXT PRIMARY KEY, value TEXT) WITHOUT ROWID
//       Flat "Section/key" pairs, the same keys config.ini used, so values
//       migrate 1:1. Loaded completely in one query when a save opens.
//   profile_documents (name TEXT PRIMARY KEY, encoding, size, data BLOB, updated_at)
//       Larger items, read only when needed: "definitions_<fm version>"
//       (zlib-compressed JSON), "club_logo" / "national_flag" (PNG, stored raw).
//
// The club and national-team settings (user club, favorites, bridges, page
// state, ...) remain in the long-standing `settings` table (Database::setting);
// together with the two tables above they form the profile. See
// desktop/PROFILE.md for the full map.
//
// All writes run in a transaction (a savepoint inside an open one), so a
// failure never leaves half a profile on disk.
class ProfileStore
{
public:
    // Format of the profile tables. Stored as "Profile/format".
    static constexpr int kFormatVersion = 1;

    // Document names.
    static QString definitionsDocument(const QString &fmVersionId); // "definitions_fm27"
    static QString clubLogoDocument();     // "club_logo"
    static QString nationalFlagDocument(); // "national_flag"

    // Marker keys in profile_settings (section "Profile").
    static QString formatKey();  // "Profile/format"
    static QString seedKey();    // "Profile/seed": where the first values came from
    static QString createdKey(); // "Profile/created_at" (ISO date)

    explicit ProfileStore(Database &db);

    // Creates both tables if missing. Idempotent and cheap.
    bool ensureTables();

    // True once the profile has been seeded (format marker present).
    bool isInitialized();

    // Every profile_settings row.
    bool loadSettings(QHash<QString, QString> &out);

    // Upserts and deletes rows in one transaction.
    bool writeSettings(const QHash<QString, QString> &upserts,
                       const QStringList &removals = {});

    // Documents. readDocument returns false with an empty errorString() when
    // the document simply does not exist.
    bool hasDocument(const QString &name);
    bool readDocument(const QString &name, QByteArray &out);
    // compress: zlib for text (JSON); images are stored as they are.
    bool writeDocument(const QString &name, const QByteArray &data, bool compress);
    bool removeDocument(const QString &name);
    // Stored (on-disk) bytes of all documents, for measurements.
    qint64 storedDocumentBytes();

    // Roles and tactics of the save for one FM version (the JSON the
    // Definitions class loads). The first call seeds the document: from
    // sharedJson when given (the definitions file all 2.0.x saves shared,
    // so existing saves keep their own roles and tactics), else from the
    // shipped defaults. Every call then applies a newer shipped data revision
    // (Definitions::applyShippedUpdate); out.beforeUpdate then holds the JSON
    // from before that update, so the caller can back it up.
    struct DefinitionsResult {
        QByteArray json;
        bool seeded = false;      // document was created now
        QByteArray beforeUpdate;  // set when a shipped update was applied
    };
    bool loadDefinitions(const QString &fmVersionId, const QByteArray &shippedJson,
                         const QByteArray &sharedJson, DefinitionsResult &out);
    bool saveDefinitions(const QString &fmVersionId, const QByteArray &json);

    QString errorString() const { return m_error; }

    // Reads one document straight from an SQLite connection that is not a
    // Database (the start screen scans closed saves). Missing table or row
    // yields false.
    static bool readDocument(const QString &connectionName, const QString &name,
                             QByteArray &out);

private:
    Database &m_db;
    QString m_error;
};

} // namespace fm
