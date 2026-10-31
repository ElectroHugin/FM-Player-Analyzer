#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

namespace fm {

// Per-FM-version contract between the game's export files and the database:
// which export headers map to which columns, which are ignored and which must
// be present.

// FM HTML export header abbreviation -> full column name (~59 entries) for the
// given FM version; the schema contract between HTML exports and the database.
// Unknown/empty ids fall back to the default version.
const QHash<QString, QString> &attributeMapping(const QString &versionId);
// Convenience overload for the default version.
const QHash<QString, QString> &attributeMapping();

// Export headers FM routinely writes that the app intentionally ignores, so the
// unknown-column warning stays quiet for them. Unknown/empty ids fall back to
// the default version.
const QSet<QString> &ignoredExportColumns(const QString &versionId);

// Export headers an import cannot do without, in a fixed order: the player's
// unique id first, then the name. Unknown/empty ids fall back to the default
// version.
const QStringList &requiredImportColumns(const QString &versionId);

// Other spellings of export headers, alias -> header used by the schema (FM27:
// the Player Database calls the player column "Name", the squad "Player").
// Unknown/empty ids fall back to the default version.
const QHash<QString, QString> &exportHeaderAliases(const QString &versionId);
// Renames alias headers in place, unless the file also has the header itself.
void applyHeaderAliases(QStringList *headers, const QString &versionId);

// Nationality as the app stores it: the game's three-letter code ("GER").
// FM27 shows full names in some views ("Germany", "Ivory Coast"); known names
// become their code, anything else is returned trimmed and unchanged.
QString nationCode(const QString &value);

} // namespace fm
