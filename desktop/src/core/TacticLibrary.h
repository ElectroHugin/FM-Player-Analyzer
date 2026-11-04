#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace fm {

class Database;
class Definitions;

// Editing the tactics of a save's profile and exchanging them as files.
//
// Tactics live in the definitions document of the save's profile
// ("tactic_roles" + "tactic_layouts", see Definitions). Next to them the
// document remembers what the user did with the shipped example tactics, so a
// newer shipped data revision (Definitions::applyShippedUpdate) neither
// overwrites an edited template nor brings back a deleted one:
//   "edited_tactics":  names the user saved himself (kept as they are),
//   "deleted_tactics": names the user deleted or renamed (not restored).
//
// All functions on QJsonObject work on a definitions root
// (Definitions::root()); the caller sets it back with setRoot() and saves
// through AppContext::saveDefinitions().
namespace TacticLibrary {

inline constexpr int kExchangeFormatVersion = 1;
// File type of the exchange format (JSON inside).
QString exchangeSuffix(); // "fmpatactic"

// --- Editing ---

// Inserts or replaces a tactic. roles: {slot: role} (one line-up) or
// {"ip": {...}, "oop": {...}}. The layout (stratum -> slots) is derived from
// the slots of the line-up without ball when layout is empty.
void putTactic(QJsonObject &root, const QString &fmVersionId, const QString &name,
               const QJsonObject &roles, const QJsonObject &layout = {});
// Removes a tactic and remembers it as deleted. False if it does not exist.
bool removeTactic(QJsonObject &root, const QString &name);
// Renames a tactic: the old name counts as deleted, the new one as the
// user's own. False if old is missing or newName is taken.
bool renameTactic(QJsonObject &root, const QString &oldName, const QString &newName);
// stratum -> slots of a line-up (GK left out), as the Best XI expects it.
QJsonObject layoutFor(const QString &fmVersionId, const QJsonObject &lineUp);

// Favorite tactics and manual bridges (database settings) follow a renamed
// tactic, and are cleared for a deleted one.
void renameTacticReferences(Database &db, const QString &oldName, const QString &newName);
void removeTacticReferences(Database &db, const QString &name);
// Manual bridges point at slots; after an edit drop those whose slots are
// gone. ipSlots/oopSlots: the slots of the edited line-ups.
void pruneBridges(Database &db, const QString &tactic, const QStringList &ipSlots,
                  const QStringList &oopSlots);

// --- Exchange format ---

// One tactic as an exchange file: tactic, layout and the full definition of
// every role it uses (so the receiving profile can add roles it lacks).
QByteArray exportTactic(const Definitions &definitions, const QString &name);

struct ImportedRole {
    QString id;
    QString name;      // display name
    QString category;  // "Defense", ...
    QString phase;     // "ip", "oop" or empty
    QStringList positions;
    QStringList key;
    QStringList preferable;
};

struct ImportCheck {
    enum Status {
        Ok,
        InvalidFile,       // not JSON, not our format, damaged content
        TooNew,            // written by a newer format version
        WrongFmVersion,    // tactic of another FM version
        MissingRoles,      // uses roles the profile lacks and the file does not define
    };
    Status status = InvalidFile;
    QString error;              // details for InvalidFile (English, for the log)
    QString fileFmVersion;      // e.g. "fm27"
    QString name;               // tactic name in the file
    QJsonObject roles;          // {slot: role} or {"ip": .., "oop": ..}
    QJsonObject layout;
    QList<ImportedRole> rolesToAdd;   // defined in the file, missing here
    QStringList unknownRoles;         // missing here and not defined in the file
    QStringList differingRoles;       // same id, but another definition here
    bool nameTaken = false;           // a tactic with this name exists
};

// Checks an exchange file against the open profile.
ImportCheck checkImport(const QByteArray &data, const Definitions &definitions);

// Adds the checked tactic under finalName (replacing an existing one) and
// every role from rolesToAdd. Requires status Ok.
void applyImport(QJsonObject &root, const QString &fmVersionId, const ImportCheck &check,
                 const QString &finalName);

// "Name (2)", "Name (3)" ... the first name not in taken.
QString uniqueName(const QString &name, const QStringList &taken);

} // namespace TacticLibrary

} // namespace fm
