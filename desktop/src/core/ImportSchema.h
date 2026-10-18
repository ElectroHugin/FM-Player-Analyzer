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

} // namespace fm
