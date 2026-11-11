#pragma once
#include "RatingProfile.h"

namespace fm {

// Detached library state. Mutation methods validate before changing anything.
// The application owns activation and notifications for affected save files.
class RatingProfileLibrary {
public:
    const QMap<QString, RatingProfile> &profiles() const { return m_profiles; }
    QString seedBuiltins(const QList<RatingProfile> &profiles);
    QString insert(const RatingProfile &profile);
    QString update(const RatingProfile &profile, const QString &expectedDocumentRevision);
    QString duplicate(const QString &id, const QString &name, QString *newId);
    QString remove(const QString &id);
    QString restore(const QString &id);
    bool customized(const QString &id) const;
    std::optional<RatingProfile> formulaSnapshot(const QString &revision) const;

    static QString documentRevision(const RatingProfile &profile);
    QByteArray toJson() const;
    static std::optional<RatingProfileLibrary> fromJson(const QByteArray &data, QString *error);

    // Read/write only the given library path. QSaveFile keeps the previous file
    // intact on errors. Compare the loaded file digest under a writer lock to
    // avoid losing another process's changes, including metadata-only edits.
    static std::optional<RatingProfileLibrary> load(const QString &path, QString *error);
    QString save(const QString &path);

private:
    QMap<QString, RatingProfile> m_profiles;
    QMap<QString, RatingProfile> m_originals;
    QMap<QString, RatingProfile> m_snapshots;
    QByteArray m_loadedDigest;
    QString m_loadedPath;
};
} // namespace fm
