#pragma once

#include <QHash>
#include <QString>

namespace fm {

// DWRS importance tiers and their default weights per FM version.

// DWRS importance category per attribute (field players / goalkeepers) for the
// given FM version. Key: full attribute name, value: category name.
const QHash<QString, QString> &globalStatCategories(const QString &versionId);
const QHash<QString, QString> &gkStatCategories(const QString &versionId);

// Default category weights per FM version (overridable via config.ini).
const QHash<QString, double> &weightDefaults(const QString &versionId);   // field players
const QHash<QString, double> &gkWeightDefaults(const QString &versionId); // goalkeepers

} // namespace fm
